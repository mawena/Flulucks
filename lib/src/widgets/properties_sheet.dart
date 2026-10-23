import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';
import '../ui/states.dart';
import '../ui/tokens.dart';

/// Ouvre la feuille des propriétés de [path] dans [sourceId].
Future<void> showProperties(BuildContext context, AppState state, String sourceId, String path) =>
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      builder: (_) => PropertiesSheet(state: state, sourceId: sourceId, path: path),
    );

/// Propriétés d'un élément : emplacement, type, taille (calculée pour un dossier), date, système de fichiers, et
/// droits et propriétaire sur ext4 ; pour un fichier, empreinte SHA-256 à la demande (progression, Annuler).
class PropertiesSheet extends StatefulWidget {
  const PropertiesSheet({super.key, required this.state, required this.sourceId, required this.path});

  final AppState state;
  final String sourceId;
  final String path;

  @override
  State<PropertiesSheet> createState() => _PropertiesSheetState();
}

class _PropertiesSheetState extends State<PropertiesSheet> {
  DetailsMsg? _details;
  FolderSizeMsg? _folder;
  String? _error;
  bool _hashing = false;
  String? _hash;
  String? _hashNote;

  AppState get state => widget.state;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void dispose() {
    if (_hashing || (_details?.type == 'dir' && _folder == null)) state.api.cancelHash(); // feuille fermée en cours de calcul
    super.dispose();
  }

  Future<void> _load() async {
    try {
      final d = await state.api.details(widget.sourceId, widget.path);
      if (!mounted) return;
      setState(() => _details = d);
      if (d.type == 'dir') {
        final s = await state.api.folderSize(widget.sourceId, widget.path);
        if (mounted) setState(() => _folder = s);
      }
    } on PlatformException catch (e) {
      if (mounted) setState(() => _error = e.message ?? Fr.genericError);
    }
  }

  Future<void> _computeHash() async {
    setState(() {
      _hashing = true;
      _hash = null;
      _hashNote = null;
    });
    try {
      final h = await state.api.sha256(widget.sourceId, widget.path);
      if (!mounted) return;
      setState(() {
        _hash = h;
        _hashNote = h == null ? Fr.hashCancelled : null;
      });
    } on PlatformException catch (e) {
      if (mounted) setState(() => _hashNote = e.message ?? Fr.genericError);
    } finally {
      if (mounted) setState(() => _hashing = false);
    }
  }

  Widget _row(String label, String value) => ListTile(
        dense: true,
        title: Text(label),
        subtitle: Text(value),
      );

  String _parentOf(String path) {
    final i = path.lastIndexOf('/');
    return i <= 0 ? '/' : path.substring(0, i);
  }

  Widget _hashSection() {
    if (_hashing) {
      final pct = state.hashProgress;
      return ListTile(
        title: const Text(Fr.hashLabel),
        subtitle: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const SizedBox(height: Space.xs),
          LinearProgressIndicator(value: pct == null ? null : pct / 100),
          if (pct != null) Text(Fr.percent(pct)),
        ]),
        trailing: TextButton(onPressed: state.api.cancelHash, child: const Text(Fr.cancel)),
      );
    }
    final h = _hash;
    if (h != null) {
      return ListTile(
        title: const Text(Fr.hashLabel),
        subtitle: SelectableText(h),
        trailing: IconButton(
          tooltip: Fr.copyHash,
          icon: const Icon(Icons.copy_rounded),
          onPressed: () async {
            await Clipboard.setData(ClipboardData(text: h));
            if (mounted) showMessage(context, Fr.hashCopied, kind: MessageKind.success);
          },
        ),
      );
    }
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      if (_hashNote != null) Padding(padding: const EdgeInsets.symmetric(horizontal: Space.l), child: Text(_hashNote!)),
      Padding(
        padding: const EdgeInsets.all(Space.l),
        child: OutlinedButton(onPressed: _computeHash, child: const Text(Fr.computeHash)),
      ),
    ]);
  }

  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: state, // progression de l'empreinte
        builder: (_, _) => SafeArea(child: SingleChildScrollView(child: _content())),
      );

  Widget _content() {
    final d = _details;
    final Widget body;
    if (_error != null) {
      body = ErrorState(
        message: _error!,
        onRetry: () {
          setState(() => _error = null);
          _load();
        },
      );
    } else if (d == null) {
      body = const DelayedProgress();
    } else {
      final f = _folder;
      final size = d.type == 'dir'
          ? (f == null ? Fr.computing : Fr.folderSize(f.size, f.files, f.dirs, truncated: f.truncated))
          : Fr.humanSize(d.size);
      final mode = d.mode;
      body = Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
        ListTile(title: Text(d.name.isEmpty ? '/' : d.name, style: Theme.of(context).textTheme.titleMedium)),
        _row(Fr.location, _parentOf(d.path)),
        _row(Fr.typeLabel, Fr.typeName(d.type)),
        _row(Fr.sizeLabel, size),
        if (d.mtime > 0) _row(Fr.modifiedLabel, Fr.shortDate(d.mtime)),
        if (d.fs.isNotEmpty) _row(Fr.fsLabel, d.fs),
        if (d.permissions != null && mode != null)
          _row(Fr.permissionsLabel, '${d.permissions} (${(mode & 0xFFF).toRadixString(8).padLeft(4, '0')})'),
        if (d.uid != null && d.gid != null) _row(Fr.ownerLabel, Fr.ownerLine(d.uid!, d.gid!)),
        if (d.type == 'file') _hashSection(),
      ]);
    }
    return body;
  }
}

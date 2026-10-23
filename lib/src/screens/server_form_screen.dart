import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/dialogs.dart';
import '../ui/feedback.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';

/// Fiche d'un serveur distant (phase 10) : protocole, nom, adresse, port (21 ou 22 par défaut), utilisateur, mot de
/// passe (mémorisé seulement si coché), dossier de départ. FTP : avertissement « en clair ». Un serveur existant
/// montre l'empreinte SFTP acceptée (à oublier si le serveur a été réinstallé) et peut être supprimé. Adresse
/// obligatoire, port de 1 à 65535 : vérifiés avant l'enregistrement.
class ServerFormScreen extends StatefulWidget {
  const ServerFormScreen({super.key, required this.state, this.server});

  final AppState state;

  /// Null : nouveau serveur.
  final RemoteServerMsg? server;

  @override
  State<ServerFormScreen> createState() => _ServerFormScreenState();
}

/// Erreur du port saisi, ou null s'il va de 1 à 65535.
String? portError(String text) {
  final p = int.tryParse(text.trim());
  return p == null || p < 1 || p > 65535 ? Fr.portInvalid : null;
}

class _ServerFormScreenState extends State<ServerFormScreen> {
  final _form = GlobalKey<FormState>();
  late String _protocol = widget.server?.protocol ?? 'sftp';
  late final _name = TextEditingController(text: widget.server?.name ?? '');
  late final _host = TextEditingController(text: widget.server?.host ?? '');
  late final _port = TextEditingController(text: '${widget.server?.port ?? 22}');
  late final _user = TextEditingController(text: widget.server?.user ?? '');
  final _password = TextEditingController();
  late final _start = TextEditingController(text: widget.server?.startPath ?? '/');
  late bool _remember = widget.server?.remember ?? false;
  late String? _hostKey = widget.server?.hostKey;
  late String? _keyInfo = widget.server?.keyInfo;
  final _keyPassphrase = TextEditingController();

  @override
  void dispose() {
    for (final c in [_name, _host, _port, _user, _password, _start, _keyPassphrase]) {
      c.dispose();
    }
    super.dispose();
  }

  static int _defaultPort(String p) => p == 'ftp' ? 21 : 22;

  void _setProtocol(String p) => setState(() {
        // port laissé par défaut : suit le protocole
        if (_port.text.trim() == '${_defaultPort(_protocol)}') _port.text = '${_defaultPort(p)}';
        _protocol = p;
      });

  void _snack(String m) => showMessage(context, m, kind: MessageKind.error);

  Future<void> _save() async {
    if (!_form.currentState!.validate()) return;
    final s = widget.server;
    final msg = RemoteServerMsg(
      id: s?.id ?? '',
      protocol: _protocol,
      name: _name.text.trim(),
      host: _host.text.trim(),
      port: int.parse(_port.text.trim()),
      user: _user.text.trim(),
      startPath: _start.text.trim().isEmpty ? '/' : _start.text.trim(),
      remember: _remember,
      hostKey: _hostKey,
      keyInfo: _keyInfo,
      hasPassword: false,
      connected: false,
    );
    final pw = _password.text.isEmpty ? null : Uint8List.fromList(utf8.encode(_password.text));
    final kp = _keyPassphrase.text.isEmpty ? null : Uint8List.fromList(utf8.encode(_keyPassphrase.text));
    try {
      await widget.state.saveServer(msg, pw, keyPassphrase: kp);
      if (mounted) Navigator.of(context).pop();
    } on PlatformException catch (e) {
      if (mounted) _snack(e.message ?? Fr.genericError);
    } finally {
      pw?.fillRange(0, pw.length, 0);
      kp?.fillRange(0, kp.length, 0);
    }
  }

  Future<void> _importKey() async {
    try {
      final info = await widget.state.importPrivateKey(widget.server!.id);
      if (info != null && mounted) setState(() => _keyInfo = info);
    } on PlatformException catch (e) {
      if (mounted) _snack(e.message ?? Fr.genericError);
    }
  }

  Future<void> _removeKey() async {
    await widget.state.forgetPrivateKey(widget.server!.id);
    if (mounted) setState(() => _keyInfo = null);
  }

  Future<void> _forgetHostKey() async {
    await widget.state.forgetHostKey(widget.server!.id);
    if (mounted) setState(() => _hostKey = null);
  }

  Future<void> _delete() async {
    final s = widget.server!;
    final ok = await confirmAction(
      context,
      title: Fr.deleteServer,
      body: Fr.deleteServerBody(s.name),
      confirmLabel: Fr.delete,
      destructive: true,
    );
    if (!ok || !mounted) return;
    await widget.state.deleteServer(s.id);
    if (mounted) Navigator.of(context).pop();
  }

  Future<void> _copyHostKey() async {
    await Clipboard.setData(ClipboardData(text: _hostKey!));
    if (mounted) showMessage(context, Fr.hashCopied, kind: MessageKind.success);
  }

  Widget _field(
    TextEditingController c,
    String label, {
    String? helper,
    bool obscure = false,
    TextInputType? type,
    String? Function(String)? validator,
  }) =>
      Padding(
        padding: const EdgeInsets.symmetric(vertical: Space.s),
        child: TextFormField(
          controller: c,
          obscureText: obscure,
          keyboardType: type,
          autocorrect: false,
          validator: validator == null ? null : (v) => validator(v ?? ''),
          decoration: InputDecoration(labelText: label, helperText: helper, helperMaxLines: 3, errorMaxLines: 3),
        ),
      );

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = widget.server;
    return Scaffold(
      appBar: AppBar(
        title: Text(s?.name ?? Fr.addServer),
        actions: [
          if (s != null)
            IconButton(tooltip: Fr.deleteServer, icon: const Icon(Icons.delete_rounded), onPressed: _delete),
        ],
      ),
      body: AdaptiveBody(
        maxWidth: Layout.maxForm,
        child: Form(
          key: _form,
          child: ListView(padding: const EdgeInsets.all(Space.l), children: [
            SegmentedButton<String>(
              segments: const [
                ButtonSegment(value: 'sftp', label: Text('SFTP')),
                ButtonSegment(value: 'ftp', label: Text('FTP')),
              ],
              selected: {_protocol},
              showSelectedIcon: false,
              onSelectionChanged: (v) => _setProtocol(v.first),
            ),
            if (_protocol == 'ftp')
              const Padding(
                padding: EdgeInsets.only(top: Space.m),
                child: StatusBanner(icon: Icons.lock_open_rounded, text: Fr.ftpClear),
              ),
            const SizedBox(height: Space.s),
            _field(_name, Fr.serverName),
            _field(_host, Fr.serverHost,
                type: TextInputType.url, validator: (v) => v.trim().isEmpty ? Fr.hostRequired : null),
            _field(_port, Fr.serverPort, type: TextInputType.number, validator: portError),
            _field(_user, Fr.serverUser, helper: _protocol == 'ftp' ? Fr.serverUserHelpFtp : null),
            _field(_password, Fr.password, obscure: true, helper: s?.hasPassword == true ? Fr.passwordKeep : null),
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text(Fr.rememberPassword),
              subtitle: const Text(Fr.rememberHelp),
              value: _remember,
              onChanged: (v) => setState(() => _remember = v ?? false),
            ),
            _field(_start, Fr.startFolder),
            if (_protocol == 'sftp' && s == null)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: Space.s),
                child: Text(Fr.keyAfterSave,
                    style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
              ),
            if (_protocol == 'sftp' && s != null) ...[
              ListTile(
                contentPadding: EdgeInsets.zero,
                leading: const Icon(Icons.key_rounded),
                title: const Text(Fr.privateKey),
                subtitle: SelectableText(_keyInfo ?? Fr.noPrivateKey),
              ),
              if (_keyInfo != null)
                _field(_keyPassphrase, Fr.keyPassphrase, obscure: true, helper: Fr.keyPassphraseHelp),
              Wrap(spacing: Space.s, children: [
                TextButton(onPressed: _importKey, child: const Text(Fr.importKey)),
                if (_keyInfo != null) TextButton(onPressed: _removeKey, child: const Text(Fr.removeKey)),
              ]),
            ],
            if (_hostKey != null) ...[
              ListTile(
                contentPadding: EdgeInsets.zero,
                leading: const Icon(Icons.fingerprint_rounded),
                title: const Text(Fr.hostKey),
                // empreinte à chasse fixe : comparable caractère par caractère avec celle du serveur
                subtitle: SelectableText(_hostKey!, style: const TextStyle(fontFamily: 'monospace')),
                trailing: IconButton(
                  tooltip: Fr.copyHash,
                  icon: const Icon(Icons.copy_rounded),
                  onPressed: _copyHostKey,
                ),
              ),
              Align(
                alignment: AlignmentDirectional.centerStart,
                child: TextButton(onPressed: _forgetHostKey, child: const Text(Fr.forgetHostKey)),
              ),
            ],
            const SizedBox(height: Space.l),
            FilledButton(onPressed: _save, child: const Text(Fr.save)),
          ]),
        ),
      ),
    );
  }
}

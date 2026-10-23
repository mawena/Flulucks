import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';

/// Fiche d'un serveur distant (phase 10) : protocole, nom, adresse, port (21 ou 22 par défaut), utilisateur, mot de
/// passe (mémorisé seulement si coché), dossier de départ. FTP : avertissement « en clair ». Un serveur existant
/// montre l'empreinte SFTP acceptée (à oublier si le serveur a été réinstallé) et peut être supprimé.
class ServerFormScreen extends StatefulWidget {
  const ServerFormScreen({super.key, required this.state, this.server});

  final AppState state;

  /// Null : nouveau serveur.
  final RemoteServerMsg? server;

  @override
  State<ServerFormScreen> createState() => _ServerFormScreenState();
}

class _ServerFormScreenState extends State<ServerFormScreen> {
  late String _protocol = widget.server?.protocol ?? 'sftp';
  late final _name = TextEditingController(text: widget.server?.name ?? '');
  late final _host = TextEditingController(text: widget.server?.host ?? '');
  late final _port = TextEditingController(text: '${widget.server?.port ?? 22}');
  late final _user = TextEditingController(text: widget.server?.user ?? '');
  final _password = TextEditingController();
  late final _start = TextEditingController(text: widget.server?.startPath ?? '/');
  late bool _remember = widget.server?.remember ?? false;
  late String? _hostKey = widget.server?.hostKey;

  @override
  void dispose() {
    for (final c in [_name, _host, _port, _user, _password, _start]) {
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

  void _snack(String m) => ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(m)));

  Future<void> _save() async {
    final s = widget.server;
    final msg = RemoteServerMsg(
      id: s?.id ?? '',
      protocol: _protocol,
      name: _name.text.trim(),
      host: _host.text.trim(),
      port: int.tryParse(_port.text.trim()) ?? 0,
      user: _user.text.trim(),
      startPath: _start.text.trim().isEmpty ? '/' : _start.text.trim(),
      remember: _remember,
      hostKey: _hostKey,
      keyInfo: s?.keyInfo,
      hasPassword: false,
      connected: false,
    );
    final pw = _password.text.isEmpty ? null : Uint8List.fromList(utf8.encode(_password.text));
    try {
      await widget.state.saveServer(msg, pw);
      if (mounted) Navigator.of(context).pop();
    } on PlatformException catch (e) {
      if (mounted) _snack(e.message ?? Fr.genericError);
    } finally {
      pw?.fillRange(0, pw.length, 0);
    }
  }

  Future<void> _forgetHostKey() async {
    await widget.state.forgetHostKey(widget.server!.id);
    if (mounted) setState(() => _hostKey = null);
  }

  Future<void> _delete() async {
    final s = widget.server!;
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text(Fr.deleteServer),
        content: Text(Fr.deleteServerBody(s.name)),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text(Fr.cancel)),
          TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text(Fr.delete)),
        ],
      ),
    );
    if (ok != true || !mounted) return;
    await widget.state.deleteServer(s.id);
    if (mounted) Navigator.of(context).pop();
  }

  Widget _field(TextEditingController c, String label, {String? helper, bool obscure = false, TextInputType? type}) =>
      Padding(
        padding: const EdgeInsets.symmetric(vertical: 6),
        child: TextField(
          controller: c,
          obscureText: obscure,
          keyboardType: type,
          autocorrect: false,
          decoration: InputDecoration(labelText: label, helperText: helper, border: const OutlineInputBorder()),
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
          if (s != null) IconButton(tooltip: Fr.deleteServer, icon: const Icon(Icons.delete), onPressed: _delete),
        ],
      ),
      body: ListView(padding: const EdgeInsets.all(16), children: [
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
          Padding(
            padding: const EdgeInsets.only(top: 12),
            child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Icon(Icons.warning_amber, color: theme.colorScheme.error),
              const SizedBox(width: 8),
              Expanded(child: Text(Fr.ftpClear, style: TextStyle(color: theme.colorScheme.error))),
            ]),
          ),
        const SizedBox(height: 8),
        _field(_name, Fr.serverName),
        _field(_host, Fr.serverHost, type: TextInputType.url),
        _field(_port, Fr.serverPort, type: TextInputType.number),
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
        if (_hostKey != null) ...[
          ListTile(
            contentPadding: EdgeInsets.zero,
            leading: const Icon(Icons.fingerprint),
            title: const Text(Fr.hostKey),
            subtitle: SelectableText(_hostKey!),
          ),
          Align(
            alignment: Alignment.centerLeft,
            child: TextButton(onPressed: _forgetHostKey, child: const Text(Fr.forgetHostKey)),
          ),
        ],
        const SizedBox(height: 16),
        FilledButton(onPressed: _save, child: const Text(Fr.save)),
      ]),
    );
  }
}

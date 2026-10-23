import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../screens/now_playing_screen.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';

/// Barre réduite de la musique en cours (phase 10), en bas de tous les écrans : titre, avancement, lecture/pause ;
/// toucher ouvre « Lecture en cours » ([onOpen]).
class MiniPlayer extends StatelessWidget {
  const MiniPlayer({super.key, required this.state, required this.player, required this.onOpen});

  final AppState state;
  final PlayerMsg player;
  final VoidCallback onOpen;

  @override
  Widget build(BuildContext context) {
    final p = player;
    final progress = p.durationMs > 0 ? (p.positionMs / p.durationMs).clamp(0.0, 1.0) : null;
    return Material(
      elevation: 3,
      color: Theme.of(context).colorScheme.surfaceContainerHigh,
      child: SafeArea(
        top: false,
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          LinearProgressIndicator(value: progress?.toDouble() ?? 0, minHeight: 2),
          ListTile(
            leading: const Icon(Icons.music_note),
            title: Text(p.title, maxLines: 1, overflow: TextOverflow.ellipsis),
            subtitle: Text(Fr.trackOf(p.index + 1, p.count)),
            onTap: onOpen,
            trailing: Row(mainAxisSize: MainAxisSize.min, children: [
              IconButton(
                tooltip: p.playing ? Fr.pause : Fr.play,
                icon: Icon(p.playing ? Icons.pause : Icons.play_arrow),
                onPressed: () => state.playerCommand(p.playing ? 'pause' : 'play'),
              ),
              IconButton(tooltip: Fr.stopPlayback, icon: const Icon(Icons.close), onPressed: () => state.playerCommand('stop')),
            ]),
          ),
        ]),
      ),
    );
  }
}

/// Cadre de l'application : l'écran courant, la barre réduite dessous quand de la musique est en cours (cachée
/// pendant « Lecture en cours »), et le message d'un arrêt subi.
class PlayerFrame extends StatefulWidget {
  const PlayerFrame({super.key, required this.state, required this.navigatorKey, required this.messengerKey, required this.child});

  final AppState state;
  final GlobalKey<NavigatorState> navigatorKey;
  final GlobalKey<ScaffoldMessengerState> messengerKey;
  final Widget child;

  @override
  State<PlayerFrame> createState() => _PlayerFrameState();
}

class _PlayerFrameState extends State<PlayerFrame> {
  bool _fullOpen = false;

  /// Tout le cadre dans une entrée d'Overlay : la barre, posée hors du navigateur, a aussi le sien (infobulles).
  late final _entry = OverlayEntry(builder: (_) => _content(), maintainState: true);

  @override
  void initState() {
    super.initState();
    widget.state.addListener(_changed);
  }

  @override
  void dispose() {
    widget.state.removeListener(_changed);
    super.dispose();
  }

  void _changed() {
    final m = widget.state.takePlayerMessage();
    if (m != null) widget.messengerKey.currentState?.showSnackBar(SnackBar(content: Text(m)));
    if (mounted) _entry.markNeedsBuild();
  }

  @override
  void didUpdateWidget(PlayerFrame old) {
    super.didUpdateWidget(old);
    _entry.markNeedsBuild();
  }

  Future<void> _open() async {
    final nav = widget.navigatorKey.currentState;
    if (nav == null) return;
    _fullOpen = true;
    _entry.markNeedsBuild();
    await nav.push(MaterialPageRoute<void>(builder: (_) => NowPlayingScreen(state: widget.state)));
    _fullOpen = false;
    if (mounted) _entry.markNeedsBuild();
  }

  @override
  Widget build(BuildContext context) => Overlay(initialEntries: [_entry]);

  Widget _content() {
    final p = widget.state.player;
    final bar = p != null && !_fullOpen;
    // structure constante : le navigateur sous Expanded n'est jamais recréé
    return Column(children: [
      Expanded(child: MediaQuery.removePadding(context: context, removeBottom: bar, child: widget.child)),
      if (bar) MiniPlayer(state: widget.state, player: p, onOpen: _open),
    ]);
  }
}

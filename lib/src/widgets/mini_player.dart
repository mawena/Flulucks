import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../screens/now_playing_screen.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';
import '../ui/tokens.dart';
import 'bottom_dock.dart';
import 'transfer_bar.dart';

/// Barre réduite de la musique en cours (phase 10), dans le dock du bas de tous les écrans : pochette, titre, rang,
/// avancement, lecture/pause, arrêt ; toucher ouvre « Lecture en cours » ([onOpen]). TalkBack annonce « Lecture en
/// cours : titre » ([Fr.playingNow]).
class MiniPlayer extends StatelessWidget {
  const MiniPlayer({super.key, required this.state, required this.player, required this.onOpen});

  final AppState state;
  final PlayerMsg player;
  final VoidCallback onOpen;

  @override
  Widget build(BuildContext context) {
    final p = player;
    final theme = Theme.of(context);
    final s = theme.colorScheme;
    final progress = p.durationMs > 0 ? (p.positionMs / p.durationMs).clamp(0.0, 1.0) : 0.0;
    return Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      ExcludeSemantics(child: LinearProgressIndicator(value: progress.toDouble(), minHeight: 2)),
      Row(children: [
        Expanded(
          child: Semantics(
            button: true,
            label: Fr.playingNow(p.title),
            excludeSemantics: true,
            child: InkWell(
              onTap: onOpen,
              child: ConstrainedBox(
                constraints: const BoxConstraints(minHeight: Layout.minTouch),
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(Space.l, Space.s, Space.s, Space.s),
                  child: Row(children: [
                    DecoratedBox(
                      decoration: BoxDecoration(color: s.primaryContainer, borderRadius: BorderRadius.circular(Radii.s)),
                      child: SizedBox.square(
                        dimension: 40,
                        child: Icon(Icons.music_note_rounded, color: s.onPrimaryContainer),
                      ),
                    ),
                    const SizedBox(width: Space.m),
                    Expanded(
                      child: Column(crossAxisAlignment: CrossAxisAlignment.start, mainAxisSize: MainAxisSize.min, children: [
                        Text(p.title, maxLines: 1, overflow: TextOverflow.ellipsis, style: theme.textTheme.titleSmall),
                        Text(
                          Fr.trackOf(p.index + 1, p.count),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: numeric(theme.textTheme.bodySmall!).copyWith(color: s.onSurfaceVariant),
                        ),
                      ]),
                    ),
                  ]),
                ),
              ),
            ),
          ),
        ),
        IconButton(
          tooltip: p.playing ? Fr.pause : Fr.play,
          icon: Icon(p.playing ? Icons.pause_rounded : Icons.play_arrow_rounded),
          onPressed: () => state.playerCommand(p.playing ? 'pause' : 'play'),
        ),
        IconButton(
          tooltip: Fr.stopPlayback,
          icon: const Icon(Icons.close_rounded),
          onPressed: () => state.playerCommand('stop'),
        ),
        const SizedBox(width: Space.xs),
      ]),
    ]);
  }
}

/// Cadre de l'application : l'écran courant, puis le dock du bas commun à tous les écrans (lecteur réduit quand de
/// la musique est en cours, caché pendant « Lecture en cours » ; opération en cours), et le message d'un arrêt subi.
/// Le dock s'efface quand le clavier est ouvert : il serait dessous, et l'écran perdrait sa hauteur en plus de
/// celle du clavier.
class PlayerFrame extends StatefulWidget {
  const PlayerFrame({super.key, required this.state, required this.navigatorKey, required this.child});

  final AppState state;
  final GlobalKey<NavigatorState> navigatorKey;
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
    // le cadre est sous le ScaffoldMessenger de l'application : le message s'affiche sur l'écran courant
    if (m != null && mounted) showMessage(context, m);
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
    final s = widget.state;
    final p = s.player;
    final keyboard = MediaQuery.viewInsetsOf(context).bottom > 0;
    final dock = keyboard
        ? const <Widget>[]
        : [
            if (p != null && !_fullOpen) MiniPlayer(key: const ValueKey('player'), state: s, player: p, onOpen: _open),
            if (s.transfer != null) TransferBar(key: const ValueKey('transfer'), state: s),
          ];
    // structure constante : le navigateur sous Expanded n'est jamais recréé
    return Column(children: [
      Expanded(
        child: MediaQuery.removePadding(
          context: context,
          removeBottom: dock.isNotEmpty,
          child: DockHost(child: widget.child),
        ),
      ),
      DockSurface(children: dock),
    ]);
  }
}

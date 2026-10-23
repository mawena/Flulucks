import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';

/// « Lecture en cours » (phase 10) : titre, rang dans la file, curseur de position et durée, précédent, recul et
/// avance de 10 s, lecture/pause, suivant. Se ferme quand la lecture s'arrête. Pochette de remplacement (les
/// fichiers ne sont pas lus pour leurs images), temps en chiffres tabulaires, curseur annoncé « Position 1:23 sur
/// 4:05 ».
class NowPlayingScreen extends StatefulWidget {
  const NowPlayingScreen({super.key, required this.state});

  final AppState state;

  @override
  State<NowPlayingScreen> createState() => _NowPlayingScreenState();
}

class _NowPlayingScreenState extends State<NowPlayingScreen> {
  /// Position tenue sous le doigt pendant un glissement du curseur, null sinon.
  double? _dragMs;
  bool _closing = false;

  AppState get state => widget.state;

  @override
  Widget build(BuildContext context) => ListenableBuilder(
        listenable: state,
        builder: (context, _) {
          final p = state.player;
          if (p == null) {
            if (!_closing) {
              _closing = true;
              WidgetsBinding.instance.addPostFrameCallback((_) {
                if (mounted) Navigator.of(context).maybePop();
              });
            }
            return const Scaffold();
          }
          final theme = Theme.of(context);
          final s = theme.colorScheme;
          final dur = p.durationMs.toDouble();
          final pos = (_dragMs ?? p.positionMs.toDouble()).clamp(0.0, dur > 0 ? dur : 0.0);
          final time = numeric(theme.textTheme.bodyMedium!).copyWith(color: s.onSurfaceVariant);
          return Scaffold(
            appBar: AppBar(
              title: const Text(Fr.nowPlaying),
              actions: [
                IconButton(
                  tooltip: Fr.stopPlayback,
                  icon: const Icon(Icons.stop_rounded),
                  onPressed: () => state.playerCommand('stop'),
                ),
              ],
            ),
            body: SafeArea(
              child: Center(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.all(Space.xl),
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: Layout.maxForm),
                    child: Column(mainAxisSize: MainAxisSize.min, children: [
                      // pochette de remplacement : icône dans un carré tonal
                      ExcludeSemantics(
                        child: DecoratedBox(
                          key: const ValueKey('artwork'),
                          decoration: BoxDecoration(
                            color: s.primaryContainer,
                            borderRadius: BorderRadius.circular(Radii.xl),
                          ),
                          child: SizedBox.square(
                            dimension: 200,
                            child: Icon(Icons.music_note_rounded, size: 96, color: s.onPrimaryContainer),
                          ),
                        ),
                      ),
                      const SizedBox(height: Space.xl),
                      Text(p.title, style: theme.textTheme.titleLarge, textAlign: TextAlign.center),
                      const SizedBox(height: Space.xs),
                      Text(
                        Fr.trackOf(p.index + 1, p.count),
                        style: numeric(theme.textTheme.bodyMedium!).copyWith(color: s.onSurfaceVariant),
                      ),
                      const SizedBox(height: Space.xl),
                      Slider(
                        value: pos,
                        max: dur > 0 ? dur : 1,
                        semanticFormatterCallback: (v) => Fr.positionOf(v.round(), p.durationMs),
                        onChanged: dur > 0 ? (v) => setState(() => _dragMs = v) : null,
                        onChangeEnd: (v) {
                          state.playerCommand('seek', positionMs: v.round());
                          setState(() => _dragMs = null);
                        },
                      ),
                      Padding(
                        padding: const EdgeInsets.symmetric(horizontal: Space.xl),
                        child: ExcludeSemantics(
                          child: Row(mainAxisAlignment: MainAxisAlignment.spaceBetween, children: [
                            Text(Fr.clock(pos.round()), style: time),
                            Text(Fr.clock(p.durationMs), style: time),
                          ]),
                        ),
                      ),
                      const SizedBox(height: Space.l),
                      Wrap(alignment: WrapAlignment.center, crossAxisAlignment: WrapCrossAlignment.center, children: [
                        IconButton(
                          tooltip: Fr.previousTrack,
                          icon: const Icon(Icons.skip_previous_rounded),
                          onPressed: () => state.playerCommand('previous'),
                        ),
                        IconButton(
                          tooltip: Fr.back10,
                          icon: const Icon(Icons.replay_10_rounded),
                          onPressed: () => state.playerCommand('back'),
                        ),
                        IconButton.filled(
                          iconSize: 40,
                          tooltip: p.playing ? Fr.pause : Fr.play,
                          icon: Icon(p.playing ? Icons.pause_rounded : Icons.play_arrow_rounded),
                          onPressed: () => state.playerCommand(p.playing ? 'pause' : 'play'),
                        ),
                        IconButton(
                          tooltip: Fr.forward10,
                          icon: const Icon(Icons.forward_10_rounded),
                          onPressed: () => state.playerCommand('forward'),
                        ),
                        IconButton(
                          tooltip: Fr.nextTrack,
                          icon: const Icon(Icons.skip_next_rounded),
                          onPressed: () => state.playerCommand('next'),
                        ),
                      ]),
                    ]),
                  ),
                ),
              ),
            ),
          );
        },
      );
}

import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../strings_fr.dart';

/// « Lecture en cours » (phase 10) : titre, rang dans la file, curseur de position et durée, précédent, recul et
/// avance de 10 s, lecture/pause, suivant. Se ferme quand la lecture s'arrête.
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
          final dur = p.durationMs.toDouble();
          final pos = (_dragMs ?? p.positionMs.toDouble()).clamp(0.0, dur > 0 ? dur : 0.0);
          return Scaffold(
            appBar: AppBar(
              title: const Text(Fr.nowPlaying),
              actions: [
                IconButton(tooltip: Fr.stopPlayback, icon: const Icon(Icons.stop), onPressed: () => state.playerCommand('stop')),
              ],
            ),
            body: SafeArea(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
                  Icon(Icons.music_note, size: 120, color: theme.colorScheme.primary),
                  const SizedBox(height: 24),
                  Text(p.title, style: theme.textTheme.titleLarge, textAlign: TextAlign.center),
                  const SizedBox(height: 4),
                  Text(Fr.trackOf(p.index + 1, p.count), style: theme.textTheme.bodyMedium),
                  const SizedBox(height: 24),
                  Slider(
                    value: pos,
                    max: dur > 0 ? dur : 1,
                    onChanged: dur > 0 ? (v) => setState(() => _dragMs = v) : null,
                    onChangeEnd: (v) {
                      state.playerCommand('seek', positionMs: v.round());
                      setState(() => _dragMs = null);
                    },
                  ),
                  Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 24),
                    child: Row(mainAxisAlignment: MainAxisAlignment.spaceBetween, children: [
                      Text(Fr.clock(pos.round())),
                      Text(Fr.clock(p.durationMs)),
                    ]),
                  ),
                  const SizedBox(height: 16),
                  Row(mainAxisAlignment: MainAxisAlignment.center, children: [
                    IconButton(tooltip: Fr.previousTrack, icon: const Icon(Icons.skip_previous), onPressed: () => state.playerCommand('previous')),
                    IconButton(tooltip: Fr.back10, icon: const Icon(Icons.replay_10), onPressed: () => state.playerCommand('back')),
                    IconButton.filled(
                      iconSize: 40,
                      tooltip: p.playing ? Fr.pause : Fr.play,
                      icon: Icon(p.playing ? Icons.pause : Icons.play_arrow),
                      onPressed: () => state.playerCommand(p.playing ? 'pause' : 'play'),
                    ),
                    IconButton(tooltip: Fr.forward10, icon: const Icon(Icons.forward_10), onPressed: () => state.playerCommand('forward')),
                    IconButton(tooltip: Fr.nextTrack, icon: const Icon(Icons.skip_next), onPressed: () => state.playerCommand('next')),
                  ]),
                ]),
              ),
            ),
          );
        },
      );
}

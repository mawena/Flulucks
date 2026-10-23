import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../widgets/properties_sheet.dart';

/// Visionneuse d'images (phase 10) : les images du dossier dans l'ordre de l'explorateur, glisser de l'une à
/// l'autre, zoom par pincement et double toucher. Les octets restent en mémoire (§ 11) : l'image courante et ses
/// deux voisines seulement.
class ImageViewerScreen extends StatefulWidget {
  const ImageViewerScreen({
    super.key,
    required this.state,
    required this.sourceId,
    required this.images,
    required this.initialIndex,
  });

  final AppState state;
  final String sourceId;
  final List<EntryMsg> images;
  final int initialIndex;

  @override
  State<ImageViewerScreen> createState() => _ImageViewerScreenState();
}

class _ImageViewerScreenState extends State<ImageViewerScreen> {
  late final PageController _pages = PageController(initialPage: widget.initialIndex);
  late int _index = widget.initialIndex;
  final _bytes = <String, Future<Uint8List>>{};
  bool _zoomed = false;
  bool _bars = true;

  EntryMsg get _current => widget.images[_index];

  @override
  void initState() {
    super.initState();
    _keepAround(_index);
  }

  @override
  void dispose() {
    _pages.dispose();
    super.dispose();
  }

  Future<Uint8List> _load(String path) => _bytes.putIfAbsent(path, () => widget.state.api.readImage(widget.sourceId, path));

  /// Charge l'image [i] et ses voisines, oublie les autres. Dossier distant : la suivante seulement (chaque image est
  /// un téléchargement qui occupe une connexion du serveur).
  void _keepAround(int i) {
    final remote = widget.state.sourceById(widget.sourceId)?.kind == 'remote';
    final keep = {
      for (var k = math.max(0, remote ? i : i - 1); k <= math.min(widget.images.length - 1, i + 1); k++) widget.images[k].path,
    };
    _bytes.removeWhere((p, _) => !keep.contains(p));
    for (final p in keep) {
      _load(p);
    }
  }

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
    } on PlatformException catch (e) {
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(e.message ?? Fr.genericError)));
    }
  }

  PreferredSizeWidget? _appBar() {
    if (!_bars) return null;
    final e = _current;
    return AppBar(
      backgroundColor: Colors.black54,
      foregroundColor: Colors.white,
      title: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(e.name, overflow: TextOverflow.ellipsis),
        Text(Fr.imagePosition(_index + 1, widget.images.length), style: const TextStyle(fontSize: 12)),
      ]),
      actions: [
        IconButton(
          tooltip: Fr.share,
          icon: const Icon(Icons.share),
          onPressed: () => _run(() => widget.state.api.share(widget.sourceId, [e.path])),
        ),
        IconButton(
          tooltip: Fr.openWith,
          icon: const Icon(Icons.open_in_new),
          onPressed: () => _run(() => widget.state.api.openWith(widget.sourceId, e.path)),
        ),
        IconButton(
          tooltip: Fr.properties,
          icon: const Icon(Icons.info_outline),
          onPressed: () => showProperties(context, widget.state, widget.sourceId, e.path),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        backgroundColor: Colors.black,
        extendBodyBehindAppBar: true,
        appBar: _appBar(),
        body: PageView.builder(
          controller: _pages,
          physics: _zoomed ? const NeverScrollableScrollPhysics() : const PageScrollPhysics(),
          itemCount: widget.images.length,
          onPageChanged: (i) => setState(() {
            _index = i;
            _zoomed = false;
            _keepAround(i);
          }),
          itemBuilder: (_, i) {
            final path = widget.images[i].path;
            return _ImagePage(
              key: ValueKey(path),
              bytes: _load(path),
              onTap: () => setState(() => _bars = !_bars),
              onZoomChanged: (z) {
                if (z != _zoomed) setState(() => _zoomed = z);
              },
            );
          },
        ),
      );
}

/// Une image : zoom de 1 à 8 par pincement, ×2,5 au point touché par un double toucher (un second revient à 1).
class _ImagePage extends StatefulWidget {
  const _ImagePage({super.key, required this.bytes, required this.onTap, required this.onZoomChanged});

  final Future<Uint8List> bytes;
  final VoidCallback onTap;
  final ValueChanged<bool> onZoomChanged;

  @override
  State<_ImagePage> createState() => _ImagePageState();
}

class _ImagePageState extends State<_ImagePage> {
  final _zoom = TransformationController();
  Offset _doubleTapAt = Offset.zero;

  @override
  void dispose() {
    _zoom.dispose();
    super.dispose();
  }

  bool get _zoomed => _zoom.value.getMaxScaleOnAxis() > 1.01;

  void _toggleZoom() {
    if (_zoomed) {
      _zoom.value = Matrix4.identity();
    } else {
      const s = 2.5;
      final p = _doubleTapAt;
      _zoom.value = Matrix4.identity()
        ..translateByDouble(-p.dx * (s - 1), -p.dy * (s - 1), 0, 1)
        ..scaleByDouble(s, s, 1, 1);
    }
    widget.onZoomChanged(_zoomed);
  }

  @override
  Widget build(BuildContext context) {
    final mq = MediaQuery.of(context);
    // décodée à deux fois la largeur de l'écran au plus (4096 px) : nette au zoom, mémoire bornée
    final side = math.min(4096, (mq.size.longestSide * mq.devicePixelRatio * 2).round());
    return GestureDetector(
      onTap: widget.onTap,
      onDoubleTapDown: (d) => _doubleTapAt = d.localPosition,
      onDoubleTap: _toggleZoom,
      child: FutureBuilder<Uint8List>(
        future: widget.bytes,
        builder: (context, snap) {
          if (snap.hasError) {
            final err = snap.error;
            return Center(
              child: Padding(
                padding: const EdgeInsets.all(32),
                child: Column(mainAxisSize: MainAxisSize.min, children: [
                  const Icon(Icons.broken_image, color: Colors.white54, size: 48),
                  const SizedBox(height: 12),
                  Text(
                    err is PlatformException ? err.message ?? Fr.genericError : Fr.genericError,
                    textAlign: TextAlign.center,
                    style: const TextStyle(color: Colors.white),
                  ),
                ]),
              ),
            );
          }
          if (!snap.hasData) return const Center(child: CircularProgressIndicator());
          return InteractiveViewer(
            transformationController: _zoom,
            minScale: 1,
            maxScale: 8,
            onInteractionEnd: (_) => widget.onZoomChanged(_zoomed),
            child: SizedBox.expand(
              child: Image(
                image: ResizeImage(MemoryImage(snap.data!), width: side, height: side, policy: ResizeImagePolicy.fit),
                fit: BoxFit.contain,
                gaplessPlayback: true,
                errorBuilder: (_, _, _) => const Center(child: Icon(Icons.broken_image, color: Colors.white54, size: 48)),
              ),
            ),
          );
        },
      ),
    );
  }
}

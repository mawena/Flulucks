import 'package:flutter/material.dart';

import '../ui/tokens.dart';

/// Fil d'Ariane d'un dossier, sous la barre de titre : la racine du stockage (icône, infobulle [rootLabel]), puis
/// chaque dossier jusqu'à [path]. Toucher un dossier parent l'ouvre ; la ligne défile pour montrer la fin.
class Breadcrumbs extends StatefulWidget implements PreferredSizeWidget {
  const Breadcrumbs({super.key, required this.rootLabel, required this.path, required this.onOpen});

  final String rootLabel;
  final String path;
  final ValueChanged<String> onOpen;

  @override
  Size get preferredSize => const Size.fromHeight(Layout.minTouch);

  @override
  State<Breadcrumbs> createState() => _BreadcrumbsState();
}

class _BreadcrumbsState extends State<Breadcrumbs> {
  final _scroll = ScrollController();

  @override
  void initState() {
    super.initState();
    _toEnd();
  }

  @override
  void didUpdateWidget(Breadcrumbs old) {
    super.didUpdateWidget(old);
    if (old.path != widget.path) _toEnd();
  }

  void _toEnd() => WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_scroll.hasClients) _scroll.jumpTo(_scroll.position.maxScrollExtent);
      });

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final parts = widget.path.split('/').where((p) => p.isNotEmpty).toList();
    final sep = ExcludeSemantics(
      child: Icon(Icons.chevron_right_rounded, size: 18, color: theme.colorScheme.onSurfaceVariant),
    );
    return SizedBox(
      height: Layout.minTouch,
      child: ListView(
        controller: _scroll,
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: Space.xs),
        children: [
          IconButton(
            tooltip: widget.rootLabel,
            icon: const Icon(Icons.home_rounded),
            onPressed: parts.isEmpty ? null : () => widget.onOpen('/'),
          ),
          for (var i = 0; i < parts.length; i++) ...[
            sep,
            if (i == parts.length - 1)
              Center(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: Space.m),
                  child: Text(parts[i], style: theme.textTheme.labelLarge),
                ),
              )
            else
              TextButton(
                onPressed: () => widget.onOpen('/${parts.take(i + 1).join('/')}'),
                child: Text(parts[i]),
              ),
          ],
        ],
      ),
    );
  }
}

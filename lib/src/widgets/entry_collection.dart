import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../file_types.dart';
import '../state/app_state.dart';
import '../state/prefs.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';
import 'thumb.dart';

/// Élément d'une collection : [entry] de [sourceId], avec un sous-titre propre à l'écran (sinon taille et date).
class EntryItem {
  const EntryItem({required this.sourceId, required this.entry, this.subtitle});

  final String sourceId;
  final EntryMsg entry;
  final String? subtitle;

  String get key => '$sourceId|${entry.path}';
}

/// « 3,0 Mo · hier » pour un fichier, la date seule pour un dossier.
String entrySubtitle(EntryMsg e) {
  final date = e.mtime > 0 ? Fr.friendlyDate(e.mtime) : '';
  if (e.type != 'file') return date;
  return date.isEmpty ? Fr.humanSize(e.size) : '${Fr.humanSize(e.size)} · $date';
}

/// Libellé TalkBack : « a.jpg, image, 3,0 Mo » (sans taille pour un dossier).
String entrySemantics(EntryMsg e) {
  final parts = [e.name, kindLabel(kindOf(e)), if (e.type == 'file') Fr.humanSize(e.size)];
  return parts.join(', ');
}

/// Éléments en liste ou en grille, vignettes comprises, avec la sélection et le menu par élément (⋮, en liste).
/// Sert à l'explorateur, aux raccourcis, à la recherche et à la recherche globale.
class EntrySliver extends StatelessWidget {
  const EntrySliver({
    super.key,
    required this.state,
    required this.thumbs,
    required this.items,
    required this.grid,
    this.isSelected,
    required this.onTap,
    this.onLongPress,
    this.onMore,
  });

  final AppState state;
  final ThumbCache thumbs;
  final List<EntryItem> items;
  final bool grid;
  final bool Function(EntryItem)? isSelected;
  final void Function(EntryItem) onTap;
  final void Function(EntryItem)? onLongPress;
  final void Function(EntryItem)? onMore;

  bool _sel(EntryItem i) => isSelected?.call(i) ?? false;

  @override
  Widget build(BuildContext context) {
    if (grid) {
      return SliverPadding(
        padding: const EdgeInsets.all(Space.s),
        sliver: SliverGrid.builder(
          gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
            maxCrossAxisExtent: Layout.gridMaxExtent,
            mainAxisSpacing: Layout.gridSpacing,
            crossAxisSpacing: Layout.gridSpacing,
            childAspectRatio: Layout.gridAspect,
          ),
          itemCount: items.length,
          itemBuilder: (_, i) => _GridTile(
            key: ValueKey(items[i].key),
            parent: this,
            item: items[i],
            selected: _sel(items[i]),
          ),
        ),
      );
    }
    // liste bornée à 840 dp et centrée sur un grand écran
    return SliverLayoutBuilder(builder: (context, c) {
      final side = math.max(0.0, (c.crossAxisExtent - Layout.maxList) / 2);
      return SliverPadding(
        padding: EdgeInsets.symmetric(horizontal: side),
        sliver: SliverList.builder(
          itemCount: items.length,
          itemBuilder: (_, i) => _ListTile(
            key: ValueKey(items[i].key),
            parent: this,
            item: items[i],
            selected: _sel(items[i]),
          ),
        ),
      );
    });
  }
}

class _ListTile extends StatelessWidget {
  const _ListTile({super.key, required this.parent, required this.item, required this.selected});

  final EntrySliver parent;
  final EntryItem item;
  final bool selected;

  @override
  Widget build(BuildContext context) {
    final e = item.entry;
    final more = parent.onMore;
    final Widget? trailing = selected
        ? Icon(Icons.check_circle_rounded, key: const ValueKey('check'), color: Theme.of(context).colorScheme.primary)
        : more == null
            ? null
            : IconButton(
                key: const ValueKey('more'),
                tooltip: Fr.actionsOf(e.name),
                icon: const Icon(Icons.more_vert_rounded),
                onPressed: () => more(item),
              );
    // retour haptique de la sélection donné par l'écran (selectionClick), pas celui de l'appui long du système
    return ListTile(
      enableFeedback: false,
      selected: selected,
      leading: EntryThumb(state: parent.state, cache: parent.thumbs, sourceId: item.sourceId, entry: e),
      title: Text(e.name, maxLines: 1, overflow: TextOverflow.ellipsis),
      subtitle: Text(item.subtitle ?? entrySubtitle(e), maxLines: 1, overflow: TextOverflow.ellipsis),
      trailing: AnimatedSwitcher(
        duration: Motion.of(context, Motion.short),
        transitionBuilder: (child, a) => ScaleTransition(scale: a, child: child),
        child: trailing ?? const SizedBox.shrink(),
      ),
      onTap: () => parent.onTap(item),
      onLongPress: parent.onLongPress == null ? null : () => parent.onLongPress!(item),
    );
  }
}

class _GridTile extends StatelessWidget {
  const _GridTile({super.key, required this.parent, required this.item, required this.selected});

  final EntrySliver parent;
  final EntryItem item;
  final bool selected;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = theme.colorScheme;
    final e = item.entry;
    final radius = BorderRadius.circular(Radii.m);
    return Semantics(
      label: entrySemantics(e),
      selected: selected,
      button: true,
      excludeSemantics: true,
      onTap: () => parent.onTap(item),
      onLongPress: parent.onLongPress == null ? null : () => parent.onLongPress!(item),
      child: AnimatedContainer(
        duration: Motion.of(context, Motion.short),
        curve: Motion.standard,
        decoration: BoxDecoration(color: selected ? s.primaryContainer : s.surface.withValues(alpha: 0), borderRadius: radius),
        child: InkWell(
          enableFeedback: false,
          borderRadius: radius,
          onTap: () => parent.onTap(item),
          onLongPress: parent.onLongPress == null ? null : () => parent.onLongPress!(item),
          child: Padding(
            padding: const EdgeInsets.all(Space.xs),
            child: Column(children: [
              Expanded(
                child: Stack(children: [
                  Positioned.fill(
                    child: LayoutBuilder(
                      builder: (_, c) => Center(
                        child: EntryThumb(
                          state: parent.state,
                          cache: parent.thumbs,
                          sourceId: item.sourceId,
                          entry: e,
                          size: c.biggest.shortestSide,
                          radius: Radii.m,
                        ),
                      ),
                    ),
                  ),
                  PositionedDirectional(
                    top: Space.xs,
                    end: Space.xs,
                    child: AnimatedScale(
                      scale: selected ? 1 : 0,
                      duration: Motion.of(context, Motion.short),
                      curve: Motion.standard,
                      child: DecoratedBox(
                        decoration: BoxDecoration(color: s.surface, shape: BoxShape.circle),
                        child: Icon(Icons.check_circle_rounded, color: s.primary),
                      ),
                    ),
                  ),
                ]),
              ),
              const SizedBox(height: Space.xs),
              Text(
                e.name,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                textAlign: TextAlign.center,
                style: theme.textTheme.bodySmall?.copyWith(color: selected ? s.onPrimaryContainer : null),
              ),
            ]),
          ),
        ),
      ),
    );
  }
}

/// Bascule liste / grille, enregistrée dans les préférences d'affichage (tous les écrans de fichiers la suivent).
class ViewModeButton extends StatelessWidget {
  const ViewModeButton({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final v = state.viewPrefs;
    return IconButton(
      tooltip: v.grid ? Fr.showList : Fr.showGrid,
      icon: Icon(v.grid ? Icons.view_list_rounded : Icons.grid_view_rounded),
      onPressed: () => state.setViewPrefs(v.copyWith(grid: !v.grid)),
    );
  }
}

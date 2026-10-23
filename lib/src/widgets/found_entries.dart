import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../screens/explorer_screen.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/feedback.dart';
import '../ui/sheets.dart';
import '../viewers.dart';
import 'entry_collection.dart';
import 'properties_sheet.dart';

/// Éléments trouvés hors de l'explorateur (raccourcis, recherche, recherche globale) : ouverture et actions
/// communes.

/// Ouvre [item] : un dossier dans l'explorateur, un fichier dans le lecteur de Portail qui lui convient (avec
/// les éléments de [shown] **de la même source**, dans l'ordre affiché), sinon dans une autre application.
Future<void> openFoundEntry(BuildContext context, AppState state, EntryItem item, List<EntryItem> shown) async {
  final e = item.entry;
  if (e.type == 'dir') return openExplorer(context, state, item.sourceId, path: e.path);
  try {
    if (opensInPortail(state, e.name)) {
      final folder = [
        for (final i in shown)
          if (i.sourceId == item.sourceId) i.entry,
      ];
      await openViewer(context, state, item.sourceId, e, folder);
    } else {
      await state.api.openWith(item.sourceId, e.path);
    }
  } on PlatformException catch (err) {
    if (context.mounted) showMessage(context, err.message ?? Fr.genericError, kind: MessageKind.error);
  }
}

/// Feuille d'un élément trouvé : Ouvrir le dossier (celui qui le contient), Ouvrir avec…, Partager, Propriétés.
Future<void> showFoundEntrySheet(BuildContext context, AppState state, EntryItem item) async {
  final e = item.entry;
  final file = e.type == 'file';
  final a = await showActionSheet<String>(context, title: e.name, actions: [
    const SheetAction('folder', Icons.folder_open_rounded, Fr.openFolder),
    SheetAction('openWith', Icons.open_in_new_rounded, Fr.openWith, enabled: file),
    SheetAction('share', Icons.share_rounded, Fr.share, enabled: file),
    const SheetAction('properties', Icons.info_outline_rounded, Fr.properties),
  ]);
  if (a == null || !context.mounted) return;
  try {
    switch (a) {
      case 'folder':
        await openExplorer(context, state, item.sourceId, path: parentOf(e.path));
      case 'openWith':
        await state.api.openWith(item.sourceId, e.path);
      case 'share':
        await state.api.share(item.sourceId, [e.path]);
      case 'properties':
        await showProperties(context, state, item.sourceId, e.path);
    }
  } on PlatformException catch (err) {
    if (context.mounted) showMessage(context, err.message ?? Fr.genericError, kind: MessageKind.error);
  }
}

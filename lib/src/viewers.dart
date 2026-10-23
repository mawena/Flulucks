import 'package:flutter/material.dart';

import 'bridge/fluluks_api.g.dart';
import 'file_types.dart';
import 'screens/image_viewer_screen.dart';
import 'screens/text_editor_screen.dart';
import 'state/app_state.dart';

/// Lecteurs déjà présents dans Portail ; les autres types s'ouvrent encore dans une autre application.
const _available = {'image', 'text', 'video', 'audio'};

/// Vrai si [name] s'ouvre dans un lecteur de Portail (réglage « Ouvrir dans Portail » actif et lecteur présent).
bool opensInPortail(AppState state, String name) =>
    state.viewPrefs.builtInViewers && _available.contains(viewerFor(name));

/// Ouvre [e] de [sourceId] dans le lecteur de Portail qui lui convient ; [folder] : éléments du dossier dans
/// l'ordre affiché (visionneuse, file d'attente de la musique).
Future<void> openViewer(BuildContext context, AppState state, String sourceId, EntryMsg e, List<EntryMsg> folder) async {
  switch (viewerFor(e.name)) {
    case 'image':
      final images = folder.where((x) => x.type == 'file' && viewerFor(x.name) == 'image').toList();
      final i = images.indexWhere((x) => x.path == e.path);
      await Navigator.of(context).push(MaterialPageRoute<void>(
          builder: (_) => ImageViewerScreen(state: state, sourceId: sourceId, images: images, initialIndex: i < 0 ? 0 : i)));
    case 'text':
      await openTextEditor(context, state, sourceId, e);
    case 'video':
      await state.api.playVideo(sourceId, e.path);
    case 'audio':
      // le dossier entier dans l'ordre affiché : le côté Kotlin en garde les sons pour la file d'attente
      final files = folder.where((x) => x.type == 'file').toList();
      await state.playAudio(sourceId, [for (final x in files) x.path], files.indexWhere((x) => x.path == e.path));
    default:
      await state.api.openWith(sourceId, e.path);
  }
}

/// Ouvre [e] dans l'éditeur de texte, quel que soit son nom (« Modifier comme texte »).
Future<void> openTextEditor(BuildContext context, AppState state, String sourceId, EntryMsg e) =>
    Navigator.of(context).push(MaterialPageRoute<void>(
        builder: (_) => TextEditorScreen(state: state, sourceId: sourceId, path: e.path, name: e.name)));

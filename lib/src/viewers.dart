import 'package:flutter/material.dart';

import 'bridge/fluluks_api.g.dart';
import 'screens/image_viewer_screen.dart';
import 'screens/text_editor_screen.dart';
import 'state/app_state.dart';

/// Extensions de chaque lecteur de Portail (spec § 18), en minuscules.
const _images = {'jpg', 'jpeg', 'png', 'gif', 'webp', 'bmp', 'heic', 'heif'};
const _audio = {'mp3', 'm4a', 'aac', 'ogg', 'oga', 'opus', 'flac', 'wav'};
const _video = {'mp4', 'm4v', 'mkv', 'webm', 'mov', '3gp', 'avi'};
const _text = {
  'txt', 'md', 'markdown', 'json', 'xml', 'csv', 'tsv', 'log', 'ini', 'conf', 'cfg', 'yaml', 'yml', 'toml', 'srt', //
  'vtt', 'sh', 'bash', 'py', 'kt', 'kts', 'java', 'dart', 'c', 'h', 'cpp', 'hpp', 'js', 'ts', 'css', 'html', 'htm',
  'properties', 'gradle', 'sql', 'env', 'gitignore',
};

/// Lecteurs déjà présents dans Portail ; les autres types s'ouvrent encore dans une autre application.
const _available = {'image', 'text'};

/// Lecteur de Portail pour [name] : `image`, `audio`, `video`, `text`, ou null (autre application). « .gitignore »
/// compte comme l'extension « gitignore ».
String? viewerFor(String name) {
  final dot = name.lastIndexOf('.');
  if (dot < 0 || dot == name.length - 1) return null;
  final ext = name.substring(dot + 1).toLowerCase();
  if (_images.contains(ext)) return 'image';
  if (_audio.contains(ext)) return 'audio';
  if (_video.contains(ext)) return 'video';
  if (_text.contains(ext)) return 'text';
  return null;
}

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
    default:
      await state.api.openWith(sourceId, e.path);
  }
}

/// Ouvre [e] dans l'éditeur de texte, quel que soit son nom (« Modifier comme texte »).
Future<void> openTextEditor(BuildContext context, AppState state, String sourceId, EntryMsg e) =>
    Navigator.of(context).push(MaterialPageRoute<void>(
        builder: (_) => TextEditorScreen(state: state, sourceId: sourceId, path: e.path, name: e.name)));

import 'package:flutter/material.dart';

import 'bridge/fluluks_api.g.dart';
import 'strings_fr.dart';

/// Table unique des types de fichiers (phase 11) : extensions, icône, couleur et lecteur de Portail. Les
/// ensembles d'extensions sont ceux des lecteurs de la phase 10 (spec § 18), en minuscules.
const _images = {'jpg', 'jpeg', 'png', 'gif', 'webp', 'bmp', 'heic', 'heif'};
const _audio = {'mp3', 'm4a', 'aac', 'ogg', 'oga', 'opus', 'flac', 'wav'};
const _video = {'mp4', 'm4v', 'mkv', 'webm', 'mov', '3gp', 'avi'};
const _text = {
  'txt', 'md', 'markdown', 'json', 'xml', 'csv', 'tsv', 'log', 'ini', 'conf', 'cfg', 'yaml', 'yml', 'toml', 'srt', //
  'vtt', 'sh', 'bash', 'py', 'kt', 'kts', 'java', 'dart', 'c', 'h', 'cpp', 'hpp', 'js', 'ts', 'css', 'html', 'htm',
  'properties', 'gradle', 'sql', 'env', 'gitignore',
};
const _documents = {'pdf', 'doc', 'docx', 'odt', 'ods', 'odp', 'xls', 'xlsx', 'ppt', 'pptx', 'rtf', 'epub'};

/// Extensions des archives que le moteur sait extraire (les doubles d'abord).
const _archiveExtensions = ['.tar.gz', '.tar.xz', '.tgz', '.txz', '.zip', '.7z', '.rar', '.tar', '.gz', '.xz'];

enum FileKind { folder, image, video, audio, text, document, archive, package, link, other }

/// Extension de [name] en minuscules ; « .gitignore » compte comme l'extension « gitignore ».
String? _ext(String name) {
  final dot = name.lastIndexOf('.');
  if (dot < 0 || dot == name.length - 1) return null;
  return name.substring(dot + 1).toLowerCase();
}

/// Archive reconnue à l'extension : un tap propose de l'extraire.
bool isArchiveName(String name) {
  final n = name.toLowerCase();
  return _archiveExtensions.any((e) => n.length > e.length && n.endsWith(e));
}

FileKind kindOf(EntryMsg e) {
  if (e.type == 'dir') return FileKind.folder;
  if (e.type == 'link') return FileKind.link;
  if (isArchiveName(e.name)) return FileKind.archive;
  final x = _ext(e.name);
  if (x == 'apk') return FileKind.package;
  if (_images.contains(x)) return FileKind.image;
  if (_video.contains(x)) return FileKind.video;
  if (_audio.contains(x)) return FileKind.audio;
  if (_text.contains(x)) return FileKind.text;
  if (_documents.contains(x)) return FileKind.document;
  return FileKind.other;
}

IconData kindIcon(FileKind k) => switch (k) {
      FileKind.folder => Icons.folder_rounded,
      FileKind.image => Icons.image_rounded,
      FileKind.video => Icons.movie_rounded,
      FileKind.audio => Icons.audiotrack_rounded,
      FileKind.text => Icons.article_rounded,
      FileKind.document => Icons.description_rounded,
      FileKind.archive => Icons.folder_zip_rounded,
      FileKind.package => Icons.android_rounded,
      FileKind.link => Icons.link_rounded,
      FileKind.other => Icons.insert_drive_file_rounded,
    };

/// Couleur par rôle (direction visuelle) : dossiers et archives `primary`, images et vidéos `tertiary`, musique,
/// textes et documents `secondary`, le reste `onSurfaceVariant`.
Color kindColor(FileKind k, ColorScheme s) => switch (k) {
      FileKind.folder || FileKind.archive => s.primary,
      FileKind.image || FileKind.video => s.tertiary,
      FileKind.audio || FileKind.text || FileKind.document => s.secondary,
      _ => s.onSurfaceVariant,
    };

/// Nom du type annoncé par TalkBack (« a.jpg, image, 3,0 Mo »).
String kindLabel(FileKind k) => switch (k) {
      FileKind.folder => Fr.kindFolder,
      FileKind.image => Fr.kindImage,
      FileKind.video => Fr.kindVideo,
      FileKind.audio => Fr.kindAudio,
      FileKind.text => Fr.kindText,
      FileKind.document => Fr.kindDocument,
      FileKind.archive => Fr.kindArchive,
      FileKind.package => Fr.kindPackage,
      FileKind.link => Fr.kindLink,
      FileKind.other => Fr.kindOther,
    };

/// Lecteur de Portail pour [name] : `image`, `audio`, `video`, `text`, ou null (autre application).
String? viewerFor(String name) {
  final x = _ext(name);
  if (_images.contains(x)) return 'image';
  if (_audio.contains(x)) return 'audio';
  if (_video.contains(x)) return 'video';
  if (_text.contains(x)) return 'text';
  return null;
}

/// Vrai pour une photo ou une vidéo (vignette demandée à Kotlin) ; « .jpg » seul n'en est pas une.
bool hasThumbnail(EntryMsg e) {
  if (e.type != 'file' || e.name.startsWith('.') && e.name.lastIndexOf('.') == 0) return false;
  final x = _ext(e.name);
  return _images.contains(x) || _video.contains(x);
}

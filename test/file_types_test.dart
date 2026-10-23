import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fluluks/src/bridge/fluluks_api.g.dart';
import 'package:fluluks/src/file_types.dart';
import 'package:fluluks/src/ui/theme.dart';

EntryMsg _e(String name, {String type = 'file'}) => EntryMsg(name: name, path: '/$name', type: type, size: 1, mtime: 0);

void main() {
  test('kindOfExtensions', () {
    expect(kindOf(_e('IMG.JPG')), FileKind.image);
    expect(kindOf(_e('photo.heic')), FileKind.image);
    expect(kindOf(_e('film.MKV')), FileKind.video);
    expect(kindOf(_e('a.Mp3')), FileKind.audio);
    expect(kindOf(_e('notes.txt')), FileKind.text);
    expect(kindOf(_e('.gitignore')), FileKind.text);
    expect(kindOf(_e('rapport.PDF')), FileKind.document);
    expect(kindOf(_e('lettre.docx')), FileKind.document);
    expect(kindOf(_e('a.tar.gz')), FileKind.archive);
    expect(kindOf(_e('b.ZIP')), FileKind.archive);
    expect(kindOf(_e('Portail.apk')), FileKind.package);
    expect(kindOf(_e('README')), FileKind.other);
    expect(kindOf(_e('DCIM', type: 'dir')), FileKind.folder);
    expect(kindOf(_e('lien.jpg', type: 'link')), FileKind.link);
  });

  test('viewerForUnchanged', () {
    expect(viewerFor('IMG.JPG'), 'image');
    expect(viewerFor('photo.heic'), 'image');
    expect(viewerFor('a.Mp3'), 'audio');
    expect(viewerFor('film.mkv'), 'video');
    expect(viewerFor('notes.TXT'), 'text');
    expect(viewerFor('.gitignore'), 'text');
    expect(viewerFor('a.tar.gz'), isNull);
    expect(viewerFor('README'), isNull);
    expect(viewerFor('doc.pdf'), isNull);
    expect(viewerFor('fin.'), isNull);
  });

  test('thumbnailsForPhotosAndVideosOnly', () {
    expect(hasThumbnail(_e('a.jpg')), isTrue);
    expect(hasThumbnail(_e('b.mp4')), isTrue);
    expect(hasThumbnail(_e('c.mp3')), isFalse);
    expect(hasThumbnail(_e('d.jpg', type: 'dir')), isFalse);
  });

  test('everyKindHasRoundedIconAndRoleColor', () {
    final s = portailScheme(Brightness.light);
    for (final k in FileKind.values) {
      expect(kindIcon(k).fontFamily, 'MaterialIcons', reason: '$k');
      expect(kindLabel(k), isNotEmpty, reason: '$k');
    }
    expect(kindColor(FileKind.folder, s), s.primary);
    expect(kindColor(FileKind.archive, s), s.primary);
    expect(kindColor(FileKind.image, s), s.tertiary);
    expect(kindColor(FileKind.video, s), s.tertiary);
    expect(kindColor(FileKind.audio, s), s.secondary);
    expect(kindColor(FileKind.text, s), s.secondary);
    expect(kindColor(FileKind.document, s), s.secondary);
    expect(kindColor(FileKind.other, s), s.onSurfaceVariant);
  });
}

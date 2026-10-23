import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'strings_fr.dart';

/// Bibliothèques embarquées dans Portail et fichiers de leur licence (déclarés dans `pubspec.yaml`). Les noms sont
/// ceux des projets, tels que les montre « Licences des bibliothèques ».
const portailLibraries = <(String, List<String>)>[
  ('lwext4', ['native/third_party/lwext4/LICENSE']),
  ('ntfs-3g', ['native/third_party/ntfs-3g/COPYING']),
  ('FatFs', ['native/third_party/fatfs/LICENSE.txt']),
  ('libarchive', ['native/third_party/libarchive/COPYING']),
  ('XZ Utils', ['native/third_party/xz/COPYING', 'native/third_party/xz/COPYING.0BSD']),
  ('Argon2', ['native/third_party/argon2/LICENSE']),
  ('cJSON', ['native/third_party/cjson/LICENSE']),
  ('Mbed TLS', ['native/third_party/mbedtls/LICENSE']),
  ('Media3', ['assets/licenses/apache-2.0.txt']),
  ('Apache Commons Net', ['assets/licenses/commons-net-notice.txt', 'assets/licenses/apache-2.0.txt']),
  ('JSch', ['assets/licenses/jsch.txt']),
  ('Bouncy Castle', ['assets/licenses/bouncycastle.txt']),
];

/// Ajoute les licences des bibliothèques de Portail à celles de Flutter ; elles ne sont lues qu'à l'ouverture de la
/// page des licences. Appelé une fois, par `main`.
void registerLicenses() => LicenseRegistry.addLicense(_collect);

Stream<LicenseEntry> _collect() async* {
  for (final (name, files) in portailLibraries) {
    final texts = [
      // sans cache : ces textes ne servent qu'à la page des licences
      for (final f in files) (await rootBundle.loadString(f, cache: false)).replaceAll('\r\n', '\n').trim(),
    ];
    yield LicenseEntryWithLineBreaks([name], texts.join('\n\n'));
  }
}

/// Textes de la page des licences de Material en français, sans la dépendance `flutter_localizations` ; le reste
/// garde les textes par défaut (les infobulles de Portail passent par [Fr]).
class FrMaterialLocalizations extends DefaultMaterialLocalizations {
  const FrMaterialLocalizations();

  static const LocalizationsDelegate<MaterialLocalizations> delegate = _FrMaterialDelegate();

  @override
  String get licensesPageTitle => Fr.licensesTitle;

  @override
  String licensesPackageDetailText(int licenseCount) => Fr.licensesCount(licenseCount);

  @override
  String get viewLicensesButtonLabel => Fr.viewLicenses;
}

class _FrMaterialDelegate extends LocalizationsDelegate<MaterialLocalizations> {
  const _FrMaterialDelegate();

  @override
  bool isSupported(Locale locale) => true;

  @override
  Future<MaterialLocalizations> load(Locale locale) => SynchronousFuture(const FrMaterialLocalizations());

  @override
  bool shouldReload(_FrMaterialDelegate old) => false;
}

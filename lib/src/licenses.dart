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

/// Textes de Material en français, sans la dépendance `flutter_localizations` : page des licences, infobulles
/// (Retour, Fermer…), barre d'édition du texte et annonces de TalkBack (dialogues, volets dépliables). Les sélecteurs
/// de date et d'heure, absents de Portail, gardent les textes par défaut.
class FrMaterialLocalizations extends DefaultMaterialLocalizations {
  const FrMaterialLocalizations();

  static const LocalizationsDelegate<MaterialLocalizations> delegate = _FrMaterialDelegate();

  @override
  String get licensesPageTitle => Fr.licensesTitle;

  @override
  String licensesPackageDetailText(int licenseCount) => Fr.licensesCount(licenseCount);

  @override
  String get viewLicensesButtonLabel => Fr.viewLicenses;

  @override
  String aboutListTileTitle(String applicationName) => 'À propos de $applicationName';

  // infobulles et boutons
  @override
  String get backButtonTooltip => 'Retour';
  @override
  String get closeButtonTooltip => 'Fermer';
  @override
  String get closeButtonLabel => 'Fermer';
  @override
  String get clearButtonTooltip => 'Effacer le texte';
  @override
  String get deleteButtonTooltip => 'Supprimer';
  @override
  String get moreButtonTooltip => 'Plus';
  @override
  String get showMenuTooltip => 'Afficher le menu';
  @override
  String get openAppDrawerTooltip => 'Ouvrir le menu de navigation';
  @override
  String get cancelButtonLabel => 'Annuler';
  @override
  String get continueButtonLabel => 'Continuer';
  @override
  String get saveButtonLabel => 'Enregistrer';
  @override
  String get okButtonLabel => 'OK';
  @override
  String get searchFieldLabel => 'Rechercher';
  @override
  String get nextPageTooltip => 'Page suivante';
  @override
  String get previousPageTooltip => 'Page précédente';

  // barre d'édition du texte
  @override
  String get cutButtonLabel => 'Couper';
  @override
  String get copyButtonLabel => 'Copier';
  @override
  String get pasteButtonLabel => 'Coller';
  @override
  String get selectAllButtonLabel => 'Tout sélectionner';
  @override
  String get lookUpButtonLabel => 'Rechercher';
  @override
  String get searchWebButtonLabel => 'Rechercher sur le Web';
  @override
  String get shareButtonLabel => 'Partager';
  @override
  String get scanTextButtonLabel => 'Scanner du texte';

  // annonces de TalkBack
  @override
  String get modalBarrierDismissLabel => 'Fermer';
  @override
  String get menuDismissLabel => 'Fermer le menu';
  @override
  String get dialogLabel => 'Boîte de dialogue';
  @override
  String get alertDialogLabel => 'Alerte';
  @override
  String get popupMenuLabel => 'Menu contextuel';
  @override
  String get menuBarMenuLabel => 'Menu de la barre de menu';
  @override
  String get drawerLabel => 'Menu de navigation';
  @override
  String get bottomSheetLabel => 'Feuille du bas';
  @override
  String get scrimLabel => 'Fond';
  @override
  String scrimOnTapHint(String modalRouteContentName) => 'Fermer $modalRouteContentName';
  @override
  String get refreshIndicatorSemanticLabel => 'Actualiser';
  @override
  String get selectedDateLabel => 'Sélectionné';
  @override
  String get expandedIconTapHint => 'Réduire';
  @override
  String get collapsedIconTapHint => 'Développer';
  @override
  String get expansionTileExpandedHint => 'appuyez deux fois pour réduire';
  @override
  String get expansionTileCollapsedHint => 'appuyez deux fois pour développer';
  @override
  String get expansionTileExpandedTapHint => 'Réduire';
  @override
  String get expansionTileCollapsedTapHint => 'Développer pour en savoir plus';
  @override
  String get expandedHint => 'Réduit';
  @override
  String get collapsedHint => 'Développé';
  @override
  String get reorderItemUp => 'Déplacer vers le haut';
  @override
  String get reorderItemDown => 'Déplacer vers le bas';
  @override
  String get reorderItemLeft => 'Déplacer vers la gauche';
  @override
  String get reorderItemRight => 'Déplacer vers la droite';
  @override
  String get reorderItemToEnd => 'Déplacer à la fin';
  @override
  String get reorderItemToStart => 'Déplacer au début';
}

/// Textes des widgets de base en français (barre d'édition du texte, cases radio pour TalkBack).
class FrWidgetsLocalizations extends DefaultWidgetsLocalizations {
  const FrWidgetsLocalizations();

  static const LocalizationsDelegate<WidgetsLocalizations> delegate = _FrWidgetsDelegate();

  @override
  String get cutButtonLabel => 'Couper';
  @override
  String get copyButtonLabel => 'Copier';
  @override
  String get pasteButtonLabel => 'Coller';
  @override
  String get selectAllButtonLabel => 'Tout sélectionner';
  @override
  String get lookUpButtonLabel => 'Rechercher';
  @override
  String get searchWebButtonLabel => 'Rechercher sur le Web';
  @override
  String get shareButtonLabel => 'Partager';
  @override
  String get radioButtonUnselectedLabel => 'Non sélectionné';
  @override
  String get searchResultsFound => 'Résultats trouvés';
  @override
  String get noResultsFound => 'Aucun résultat';
  @override
  String get reorderItemUp => 'Déplacer vers le haut';
  @override
  String get reorderItemDown => 'Déplacer vers le bas';
  @override
  String get reorderItemLeft => 'Déplacer vers la gauche';
  @override
  String get reorderItemRight => 'Déplacer vers la droite';
  @override
  String get reorderItemToEnd => 'Déplacer à la fin';
  @override
  String get reorderItemToStart => 'Déplacer au début';
}

class _FrWidgetsDelegate extends LocalizationsDelegate<WidgetsLocalizations> {
  const _FrWidgetsDelegate();

  @override
  bool isSupported(Locale locale) => true;

  @override
  Future<WidgetsLocalizations> load(Locale locale) => SynchronousFuture(const FrWidgetsLocalizations());

  @override
  bool shouldReload(_FrWidgetsDelegate old) => false;
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

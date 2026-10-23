# Portail — Phase 11 : interface complète et professionnelle — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** rendre Portail complet et professionnel. Il s'agit d'une charte visuelle tirée du logo terracotta, en clair et en
sombre, de composants communs (états vides, chargements, erreurs, confirmations, feuilles, messages), d'un accueil
revu avec une recherche dans tous les stockages, d'un explorateur plus riche, de la liste ou de la grille partout où
cela a du sens, de réglages regroupés, des écrans « À propos », « Aide » et « Bienvenue », de l'accessibilité
(AA, 48 dp, TalkBack, texte à 200 %) et des grands écrans. Fin de phase : version **v1.4.0**.

**Architecture :**
- **Flutter, `lib/src/ui/`** (nouveau) : jetons (`tokens.dart`), thème Material 3 (`theme.dart`), composants communs
  (`states.dart`, `dialogs.dart`, `sheets.dart`, `feedback.dart`, `widgets.dart`). Tous les écrans s'en servent.
  Aucune couleur, durée ni marge n'est codée en dur ailleurs.
- **Flutter, `lib/src/file_types.dart`** : une seule table des types de fichiers (extensions, icône, couleur, lecteur),
  qui remplace les listes en double de `viewers.dart` et `widgets/thumb.dart`.
- **Flutter, `lib/src/widgets/entry_collection.dart`** : `EntrySliver` affiche des éléments en liste ou en grille,
  avec vignettes, sélection et menu par élément. Il sert à l'explorateur, aux raccourcis, à la recherche et à la
  recherche globale.
- **Kotlin, `files/UiPrefs.kt`** : préférences d'interface (thème, retours haptiques, bienvenue vue), stockées dans le
  même `PrefStore` que `ViewPrefs`. Pigeon `UiPrefsMsg` et `AppInfoMsg`.
- **Android** : couleurs de fond et écran de démarrage terracotta (`values*/colors.xml`, `values*/styles.xml`), pour
  qu'aucun éclair blanc n'apparaisse en mode sombre.

**Tech Stack :** Flutter 3.41.4 / Dart 3.11.1, Material 3 (`ColorScheme.fromSeed`, `DynamicSchemeVariant.fidelity`,
`SearchBar`, `Easing`, `FadeForwardsPageTransitionsBuilder`), `flutter_test` (règles `meetsGuideline`, tests
dorés), `integration_test` du SDK Flutter (BSD-3-Clause, seule dépendance ajoutée, en `dev_dependencies`), Pigeon
27.3.0, Kotlin (JUnit). **Aucune nouvelle dépendance d'exécution.**

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md) (§ 11 à 13
pour le nom, le logo et la confidentialité, § 18 pour les écrans de la phase 10, **§ 19 « interface complète et
professionnelle »**). Le § 19 laisse le contenu à arrêter avec l'utilisateur. Celui-ci est absent : les choix
ci-dessous sont des *(décisions du planificateur)* et les plus structurants sont résumés en fin de plan
(« À valider avec l'utilisateur »).

## Global Constraints

- **Branche et commits.**
  - La branche `feature/interface` est créée depuis `develop` **après la fusion de la v1.3.0**, dans l'arbre
    principal.
  - Les commits portent l'identité `mawena <gamligocharles@gmail.com>`. Ils suivent les Conventional Commits en
    français, **sans aucune attribution à Claude**, et nomment leurs fichiers (`git commit -- <fichiers>`, nouveaux
    fichiers ajoutés d'abord).
  - Chaque tâche fait au moins un commit. Ce commit coche la case de la tâche dans la feuille de route (section
    Phase 11) et ses étapes dans ce plan.
  - Les lignes T1 à T14 sont ajoutées à la section Phase 11 de la feuille de route avec le commit de ce plan, ou à
    défaut au premier commit de T1.
- **Version :** `v1.4.0`, pubspec `1.4.0+11`.
- **Langue :** toute l'interface est en français, y compris les libellés TalkBack, les infobulles et les messages.
  Toute chaîne visible passe par `Fr` (`lib/src/strings_fr.dart`).
- **Dépendances.**
  - Aucune nouvelle dépendance d'exécution, ni `google_fonts`, ni `flutter_svg`, ni `dynamic_color`, ni
    `shared_preferences`.
  - En développement seulement : `integration_test: sdk: flutter` (licence BSD-3-Clause de Flutter).
  - `cupertino_icons` est retiré s'il n'est pas utilisé (`grep -rn Cupertino lib` vide).
- **Valeurs fixées** *(décisions du planificateur)* :
  - **graine de couleur** `#C2410C` (fin du dégradé du logo), `DynamicSchemeVariant.fidelity` en clair et en sombre.
    Pas de couleurs dynamiques du fond d'écran ;
  - **thème** : `system` (par défaut), `light` ou `dark`, choisi dans les Réglages ;
  - **espacements** (grille de 4 dp) : 4, 8, 12, 16, 24, 32. **Rayons** : 8 (puces, vignettes), 12 (champs,
    tuiles), 16 (cartes), 28 (dialogues, feuilles) ;
  - **mouvement** : 150 ms (petits changements d'état), 250 ms (fondus entre états, apparitions), 400 ms (pages).
    Courbes `Easing.standard`, `Easing.emphasizedDecelerate` en entrée et `Easing.emphasizedAccelerate` en sortie.
    Durée nulle si `MediaQuery.disableAnimationsOf(context)` est vrai ;
  - **points de rupture** : compact < 600 dp, moyen de 600 à 839 dp, étendu ≥ 840 dp. Les listes et formulaires
    tiennent dans une largeur de 840 dp au plus (640 pour les formulaires) ;
  - **grille d'éléments** : `maxCrossAxisExtent` de 132 dp, espacement 8, rapport 0,78 (3 colonnes sur le Pixel 4 XL
    à 412 dp) ;
  - **cibles tactiles** de 48 × 48 dp au moins, **contraste AA** (4,5:1 pour le texte, 3:1 pour les grands textes et
    les icônes) ;
  - **taille du texte** suivie jusqu'à 200 % (`MediaQuery.withClampedTextScaling(maxScaleFactor: 2.0)`), sans
    débordement à 412 × 869 dp ;
  - **icônes** : variantes arrondies de Material Icons (`Icons.*_rounded`), assorties aux formes du logo ;
  - **police** : Roboto du système, avec chiffres tabulaires (`FontFeature.tabularFigures()`) pour les tailles, les
    pourcentages, les durées et les compteurs ;
  - **chargement** : l'indicateur n'apparaît qu'après 200 ms, pour éviter un éclair sur un dossier rapide ;
  - **retours haptiques** activés par défaut et désactivables dans les Réglages.
- **Sûreté et confidentialité :** la refonte ne retire aucune garde des phases précédentes.
  - La phrase de passe est mise à zéro, y compris avec le bouton afficher/masquer.
  - Le formatage reste confirmé par le nom tapé.
  - Les actions d'écriture restent désactivées en lecture seule.
  - Un stockage qui disparaît ramène à l'accueil.
  - Vignettes et résultats d'un disque chiffré restent en mémoire seulement (§ 11).
- **Tests :** `flutter test`, `flutter analyze` et la suite JVM (`cd android && ./gradlew -q :app:testDebugUnitTest`)
  sont verts à la fin de chaque tâche. Les tests Flutter existants dont le texte change sont mis à jour dans la tâche
  qui change ce texte, en citant `Fr.` plutôt que la chaîne quand c'est possible.

## Review Focus

1. **Texte à 200 % et petites largeurs (portrait 412 × 869, paysage 869 × 412).** Attendu : aucun débordement, et
   les actions vitales (Déverrouiller, Éjecter, Annuler une copie, Enregistrer) restent visibles et touchables.
   *Tests : T11 `noOverflowAt200Percent`, `vitalActionsReachable`.*
2. **Thème sombre et contraste.** Attendu : aucun texte sous 4,5:1, aucune couleur codée en dur hors du thème et des
   lecteurs plein écran, aucun éclair blanc au démarrage en mode sombre. *Tests : T1 `schemeContrastAA`,
   `androidColorsMatchTheme`, T11 `noHardcodedColors`, `textContrastGuideline`.*
3. **Refonte qui affaiblirait une garde de sûreté.** Cas : phrase de passe affichée puis masquée, puis envoyée ;
   formatage ; suppression ; lecture seule ; stockage débranché sous un écran revu. Attendu : mêmes gardes qu'en
   v1.3.0. *Tests : T5 `passphraseZeroedAfterToggle`, `formatStillNeedsTypedName`, T6 `readOnlyHidesWriteActions`,
   `closedSourcePopsToHome`.*
4. **Grilles et vignettes hors de l'explorateur.** Cas : raccourcis, recherche, recherche globale, centaines de
   résultats sur plusieurs stockages, dont un disque chiffré. Attendu : défilement fluide, au plus 3 vignettes
   demandées à la fois, rien d'écrit sur le téléphone, la bonne source pour chaque vignette. *Tests : T7
   `filterGridThumbsPerSource`, T12 `scroll2000EntriesSmooth`.*
5. **Premier lancement et mise à jour depuis la v1.3.0.** Attendu : bienvenue montrée une seule fois. Si l'accès aux
   fichiers est refusé, les disques USB restent utilisables. Une préférence absente ou abîmée revient au défaut.
   *Tests : T1 `uiPrefsDefaultsWhenAbsent`, T9 `welcomeShownOnce`, `laterKeepsUsbUsable`.*

---

## Inventaire de l'interface (v1.3.0) et tâche qui corrige

| Écran | Défauts visibles | Tâche |
|---|---|---|
| Thème (`app.dart`) | Graine indigo, étrangère au logo terracotta. Thème toujours celui du système. Écran de démarrage blanc, y compris en mode sombre. Barres système opaques. | T1 |
| Accueil | Longue liste sans hiérarchie. Raccourcis en simples puces. « Branchez un disque USB-C » en texte nu. Erreurs en `MaterialBanner` sans style commun. Pas de logo, pas de recherche globale, pas de mise en page tablette. | T4, T8 |
| Disques et partitions | Quatre boutons de styles différents par partition ouverte. L'état (chiffré, ouvert, lecture seule) se lit seulement dans du texte. Pas d'icône de cadenas. | T4 |
| Déverrouillage | Pas de bouton pour afficher la phrase. Erreur en simple texte rouge. Pas de choix de la lecture seule au moment d'ouvrir. | T5 |
| Explorateur | Grille fixe de 3 colonnes, même sur tablette. Pas de fil d'Ariane, pas de « Tout sélectionner », pas de menu par élément. États vide et erreur sans action « Réessayer ». Bandeau de lecture seule avec une action vide. Suppression sans « Annuler ». | T6 |
| Raccourcis, recherche | **Toujours en liste, même si l'utilisateur a choisi la grille** (demande du 2026-10-23). Pas de vignettes. Les fichiers s'ouvrent dans une autre application au lieu des lecteurs de Portail. | T7 |
| Corbeille, analyse de l'espace | États non harmonisés. Confirmation « Vider » non signalée comme destructive. | T2, T5 |
| Formatage, renommage | Saisie de nom sans validation. Rien n'est présélectionné au renommage. Bouton « Formater » sans couleur de danger. | T5 |
| Réglages | Liste à plat, sans sections. Pas de thème, pas de retours haptiques, pas d'« À propos », de licences ni d'aide. | T1, T9 |
| Textes | « élément(s) », « fichier(s) ». « 0,0 Mo » pour un petit fichier. Espaces ordinaires devant « ? », « : », « % », et à l'intérieur des guillemets « ». Dates toujours au format jj/mm/aaaa. | T3 |
| Barres du bas | Presse-papiers, opération en cours (et lecteur réduit en phase 10) empilés sans style commun. | T10 |
| Écrans de la phase 10 | Éditeur sans ligne d'état (encodage, fins de ligne). Formulaire de serveur et lecteur réduit à aligner sur les composants. | T10 |
| Accessibilité | Tuiles de la grille sans libellé TalkBack, jauges sans valeur annoncée, texte à 200 % jamais essayé. | T11 |

## Direction visuelle *(décisions du planificateur)*

- **Caractère :** un outil sûr et chaleureux. Les surfaces sont calmes, issues du schéma terracotta : le fond est un
  blanc cassé teinté, et le terracotta est réservé aux actions principales et aux états actifs. Les cartes sont
  plates (élévation 0, `surfaceContainerLow`, rayon 16). Les états se lisent d'un coup d'œil grâce à des puces
  colorées par rôle (`ChipTone`) plutôt qu'à des phrases.
- **Couleurs par rôle**, jamais par valeur :
  - action principale `primary` ;
  - disque ouvert `secondaryContainer` ;
  - avertissement (lecture seule, FTP en clair) `tertiaryContainer` ;
  - danger (formater, supprimer définitivement, place ≥ 90 %) `error` et `errorContainer`.
- **Types de fichiers** (`FileKind`) : dossiers et archives en `primary` ; images et vidéos en `tertiary` ; musique,
  textes et documents en `secondary` ; le reste en `onSurfaceVariant`.
- **Typographie :** échelle Material 2021. Les titres de carte (`titleMedium`) et les boutons (`labelLarge`) sont en
  graisse 600, comme le titre de l'accueil (`headlineSmall`). Les en-têtes de section sont en `titleSmall` couleur
  `primary`.
- **Composants :**
  - les feuilles du bas ont une poignée (`showDragHandle`) ;
  - les messages flottent (`SnackBarBehavior.floating`), avec une icône selon leur nature ;
  - les dialogues destructifs ont un bouton `error` ;
  - les boutons font 48 dp de haut au moins ;
  - les champs sont encadrés (`OutlineInputBorder`, rayon 12) ;
  - les barres de progression linéaires font 6 dp, aux bouts arrondis.
- **Mouvement :**
  - les pages passent en fondu avant (`FadeForwardsPageTransitionsBuilder`) ;
  - les états (chargement, vide, erreur, contenu) passent en fondu croisé de 250 ms (`StateSwitcher`) ;
  - la sélection d'une tuile dure 150 ms ;
  - les animations sont coupées si Android les coupe.
- **Haptique :** sélection (appui long, « Tout sélectionner ») → `selectionClick` ; déverrouillage réussi ou copie
  terminée → `lightImpact` ; échec du déverrouillage ou confirmation destructive → `mediumImpact`.
- **Logo :** `assets/logo/portail.png` est rendu depuis le SVG en 1×, 2×, 3× et 4× (96 dp de base). Il apparaît dans
  l'en-tête de l'accueil (32 dp), la bienvenue (96 dp) et « À propos » (72 dp). L'écran de démarrage est terracotta
  plein, avec l'avant-plan blanc de l'icône adaptative.

---

### Task 1 : système de design, thèmes clair et sombre, choix du thème

**Files :**
- Create : `lib/src/ui/tokens.dart`, `lib/src/ui/theme.dart`, `lib/src/state/prefs.dart`,
  `android/app/src/main/kotlin/com/mawena/fluluks/files/UiPrefs.kt`, `android/app/src/test/kotlin/com/mawena/fluluks/UiPrefsTest.kt`,
  `android/app/src/main/res/values/colors.xml`, `values-night/colors.xml`, `values-v31/styles.xml`,
  `values-night-v31/styles.xml`, `test/theme_test.dart`.
- Modify : `lib/main.dart`, `lib/src/app.dart`, `pigeons/fluluks_api.dart` (sorties générées par
  `dart run pigeon --input pigeons/fluluks_api.dart`), `bridge/HostApiImpl.kt`, `lib/src/state/host_api.dart`,
  `lib/src/state/app_state.dart`, `test/fake_host_api.dart`, `lib/src/screens/settings_screen.dart` (choix du thème),
  `lib/src/screens/explorer_screen.dart` (`_setPrefs` passe par `copyWith`), `res/values*/styles.xml`,
  `res/drawable*/launch_background.xml`, `pubspec.yaml` (`cupertino_icons`).

**Interfaces :** Produces :
```dart
// tokens.dart
abstract final class Space { static const xs = 4.0, s = 8.0, m = 12.0, l = 16.0, xl = 24.0, xxl = 32.0; }
abstract final class Radii { static const s = 8.0, m = 12.0, l = 16.0, xl = 28.0; }
abstract final class Motion {
  static const short = Duration(milliseconds: 150), medium = Duration(milliseconds: 250), long = Duration(milliseconds: 400);
  static const standard = Easing.standard, enter = Easing.emphasizedDecelerate, exit = Easing.emphasizedAccelerate;
  static Duration of(BuildContext context, Duration d); // Duration.zero si les animations sont coupées
}
enum WindowClass { compact, medium, expanded }
WindowClass windowClassOf(BuildContext context);       // < 600, < 840, sinon expanded
TextStyle numeric(TextStyle s);                         // ajoute FontFeature.tabularFigures()
// theme.dart
const portailSeed = Color(0xFFC2410C);
ColorScheme portailScheme(Brightness b);                // fromSeed, DynamicSchemeVariant.fidelity
ThemeData portailTheme(Brightness b, {bool haptics = true});
ThemeMode themeModeOf(String pref);                     // 'light', 'dark', sinon system
// prefs.dart
extension ViewPrefsCopy on ViewPrefsMsg { ViewPrefsMsg copyWith({String? sort, bool? ascending, bool? grid, bool? showHidden, bool? builtInViewers}); }
extension UiPrefsCopy on UiPrefsMsg { UiPrefsMsg copyWith({String? theme, bool? haptics, bool? welcomeSeen}); }
// AppState
UiPrefsMsg get uiPrefs; Future<void> setUiPrefs(UiPrefsMsg prefs);   // chargé par refresh()
```
- Pigeon : `UiPrefsMsg(theme: String, haptics: bool, welcomeSeen: bool)`, `UiPrefsMsg getUiPrefs()`,
  `void setUiPrefs(UiPrefsMsg prefs)`.
- Kotlin : `data class UiSettings(val theme: String = "system", val haptics: Boolean = true, val welcomeSeen: Boolean = false)`,
  `class UiPrefs(store: PrefStore) { fun get(): UiSettings; fun set(v: UiSettings) }`, clés `ui_theme`,
  `ui_haptics`, `ui_welcome_seen`.
- Le thème règle :
  - `cardTheme` (élévation 0, `surfaceContainerLow`, rayon 16, marge nulle) ;
  - `listTileTheme` (`enableFeedback: haptics`) ;
  - `appBarTheme` (`surface`, `scrolledUnderElevation` 3, barres système transparentes, icônes selon la luminosité) ;
  - boutons à 48 dp de haut au moins ;
  - `inputDecorationTheme` encadré, rayon 12 ;
  - `bottomSheetTheme` (`showDragHandle: true`) ;
  - `snackBarTheme` flottant ;
  - `progressIndicatorTheme` (6 dp, bouts arrondis, `year2023: false`) ;
  - `pageTransitionsTheme` (Android : `FadeForwardsPageTransitionsBuilder`) ;
  - textes en graisse 600 (direction visuelle).

- [x] **Step 1 : tests.**
  - `theme_test` :
    - `schemeContrastAA` : pour les deux luminosités, ratio ≥ 4,5 pour les paires `onPrimary`/`primary`,
      `onSurface`/`surface`, `onSurfaceVariant`/`surface`, `onSurfaceVariant`/`surfaceContainerLow`,
      `onSecondaryContainer`/`secondaryContainer`, `onTertiaryContainer`/`tertiaryContainer`,
      `onErrorContainer`/`errorContainer`, `error`/`surface`, `primary`/`surface`. Le ratio est calculé par une fonction
      `contrastRatio(Color, Color)` du test, d'après les luminances WCAG.
    - `primaryStaysTerracotta` : la teinte HSL de `primary` en clair est à 10° au plus de celle de `#C2410C`.
    - `themeFollowsSystemByDefault` : `MaterialApp.themeMode == ThemeMode.system`.
    - `themeChoiceApplied` : `uiPrefs.theme = 'dark'` donne `ThemeMode.dark`. Dans les Réglages, toucher « Sombre »
      appelle `setUiPrefs` avec `theme == 'dark'` et garde `haptics` et `welcomeSeen`.
    - `androidColorsMatchTheme` : `portail_fond` de `res/values/colors.xml` (lu comme fichier) vaut
      `portailScheme(light).surface` en `#RRGGBB`, et celui de `values-night` vaut la surface sombre.
    - `copyWithKeepsOtherFields` pour les deux extensions.
  - `UiPrefsTest` (JVM) : `uiPrefsDefaultsWhenAbsent` ; `badThemeFallsBackToSystem` (`ui_theme = "violet"`) ;
    `roundTrip`.
- [x] **Step 2 :** Run `flutter test test/theme_test.dart` et
  `cd android && ./gradlew -q :app:testDebugUnitTest --tests '*UiPrefsTest'` → FAIL (symboles absents).
- [x] **Step 3 :** implémenter.
  - `FluLuksApp` écoute `state` pour `themeMode` et enveloppe l'application dans
    `MediaQuery.withClampedTextScaling(maxScaleFactor: 2.0)`. `main` passe en `SystemUiMode.edgeToEdge`.
  - Les Réglages reçoivent un `SegmentedButton<String>` « Thème » : Système, Clair, Sombre.
  - Couleurs Android :
    - `colors.xml` : `portail_terracotta` `#C2410C` et `portail_fond` (valeur imprimée par le test) ;
    - `NormalTheme` : `windowBackground` `@color/portail_fond` ;
    - `launch_background` : terracotta, avec `ic_launcher_avant` centré ;
    - `values-v31` : `android:windowSplashScreenBackground` `@color/portail_terracotta` et
      `android:windowSplashScreenAnimatedIcon` `@drawable/ic_launcher_avant`.
  - `ViewPrefsMsg(…)` n'est plus construit à la main dans les écrans : ils passent par `copyWith`.
- [x] **Step 4 :** tests verts ; `flutter test`, `flutter analyze` et la suite JVM verts.
- [x] **Step 5 :** commit `feat(interface): charte terracotta, thèmes clair et sombre, choix du thème`.

### Task 2 : composants communs

**Files :**
- Create : `lib/src/ui/states.dart`, `lib/src/ui/dialogs.dart`, `lib/src/ui/sheets.dart`, `lib/src/ui/feedback.dart`,
  `lib/src/ui/widgets.dart`, `test/ui_components_test.dart`.
- Modify : `lib/src/strings_fr.dart`, `lib/src/screens/trash_screen.dart`, `space_screen.dart`, `filter_screen.dart`,
  `search_screen.dart`, `widgets/properties_sheet.dart`, `widgets/transfer_bar.dart` (ces écrans passent aux
  nouveaux états et messages).

**Interfaces :** Consumes : jetons de T1, `AppState.uiPrefs`. Produces :
```dart
class EmptyState extends StatelessWidget { const EmptyState({required IconData icon, required String title, String? message, Widget? action}); }
class ErrorState extends StatelessWidget { const ErrorState({required String message, VoidCallback? onRetry}); } // bouton « Réessayer »
class DelayedProgress extends StatefulWidget { const DelayedProgress({String? label, Duration delay = const Duration(milliseconds: 200)}); }
class StateSwitcher extends StatelessWidget { const StateSwitcher({required Widget child}); } // fondu de Motion.medium
Future<bool> confirmAction(BuildContext context, {required String title, String? body, required String confirmLabel, bool destructive = false});
Future<String?> promptText(BuildContext context, {required String title, required String label, String initial = '',
    bool selectBaseName = false, String? Function(String)? validator});
String? nameError(String name);   // vide, « . », « .. », « / », NUL, plus de 255 octets UTF-8 → message ; sinon null
class SheetAction<T> { const SheetAction(this.value, this.icon, this.label, {this.enabled = true, this.subtitle, this.destructive = false}); }
Future<T?> showActionSheet<T>(BuildContext context, {String? title, required List<SheetAction<T>> actions});
enum MessageKind { info, success, error }
void showMessage(BuildContext context, String text, {MessageKind kind = MessageKind.info, String? actionLabel, VoidCallback? onAction});
abstract final class AppHaptics { static Future<void> selection(AppState s); static Future<void> success(AppState s); static Future<void> warning(AppState s); }
enum ChipTone { neutral, good, warning, danger }
class SectionHeader extends StatelessWidget { const SectionHeader(String title, {Widget? trailing}); }
class StatusChip extends StatelessWidget { const StatusChip({required String label, IconData? icon, ChipTone tone = ChipTone.neutral}); }
class StatusBanner extends StatelessWidget { const StatusBanner({required IconData icon, required String text, ChipTone tone = ChipTone.warning, List<Widget> actions = const []}); }
class UsageBar extends StatelessWidget { const UsageBar({required double used}); } // ≥ 0,9 : couleur error ; annonce « 75 % utilisés »
class AdaptiveBody extends StatelessWidget { const AdaptiveBody({required Widget child, double maxWidth = 840}); }
```
- Tons : `neutral` → `surfaceContainerHigh`/`onSurfaceVariant` ; `good` → `secondaryContainer` ; `warning` →
  `tertiaryContainer` ; `danger` → `errorContainer`.
- Nouvelles chaînes `Fr` : `retry` « Réessayer », `nameEmpty` « Saisissez un nom », `nameSlash` « Le nom ne peut
  pas contenir « / » », `nameReserved` « Nom réservé », `nameTooLong` « Nom trop long », `undo` « Annuler »,
  `usedPercent(int p)` « $p % utilisés ».

- [x] **Step 1 : tests** (`ui_components_test`) :
  - `emptyStateShowsTitleMessageAction` ;
  - `errorStateRetryCalls` ;
  - `delayedProgressHiddenFirst200ms` (rien à 199 ms, indicateur à 200 ms) ;
  - `confirmDestructiveUsesErrorColor` (fond du bouton = `colorScheme.error`, rend `true`, `false` sur Annuler) ;
  - `promptSelectsBaseName` (`rapport.pdf` → sélection 0..7) ;
  - `promptValidatorDisablesOk` (cas de `nameError` : `''`, `'a/b'`, `'..'`, 256 octets) ;
  - `actionSheetReturnsValueAndRespectsEnabled` ;
  - `messageKindIcons` (icônes `check_circle_rounded` et `error_rounded`) ;
  - `hapticsHonourPreference` : appels `HapticFeedback.vibrate` capturés sur `SystemChannels.platform`, aucun si
    `haptics` est faux ;
  - `usageBarSemanticsAndDanger` (valeur « 92 % utilisés », couleur `error`) ;
  - `stateSwitcherNoAnimationWhenDisabled` (`disableAnimations` → durée nulle).
- [x] **Step 2 :** Run `flutter test test/ui_components_test.dart` → FAIL.
- [x] **Step 3 :** implémenter.
  - Corbeille, analyse, raccourcis, recherche et propriétés utilisent `DelayedProgress`, `ErrorState` (avec
    Réessayer), `EmptyState` et `showMessage`.
  - « Vider la corbeille » et « Supprimer définitivement » utilisent `confirmAction(destructive: true)`.
- [x] **Step 4 :** tests verts ; les suites existantes sont vertes (attentes ajustées seulement si un texte a
  changé).
- [x] **Step 5 :** commit `feat(interface): états vides, chargements, erreurs, confirmations et feuilles communs`.

### Task 3 : textes français

**Files :**
- Create : `test/strings_test.dart`.
- Modify : `lib/src/strings_fr.dart`, et les tests qui attendent « élément(s) », « fichier(s) » ou les anciens
  séparateurs (`trash_test`, `space_test`, `explorer_screen_test`, `archives_test`, `home_screen_test`…).

**Interfaces :** Produces :
```dart
static String plural(int n, String one, String many);          // 0 et 1 au singulier (usage français)
static String count(int n, String one, String many);           // « 1 élément », « 3 éléments »
static String humanSize(int bytes);                            // octets, ko, Mo, Go, To ; une décimale sauf octets
static String friendlyDate(int seconds, {DateTime? now});      // relative aux jours proches, sinon jj/mm/aaaa
```
Valeurs fixées :
- `humanSize(0)` « 0 octet », `humanSize(1)` « 1 octet », `humanSize(512)` « 512 octets », `humanSize(1500)`
  « 1,5 ko », `humanSize(250000000)` « 250,0 Mo », `humanSize(2e12)` « 2,0 To », négatif « taille inconnue ».
- `friendlyDate` :
  - même jour → « Aujourd'hui, 14:05 » ;
  - veille → « Hier, 09:12 » ;
  - même année → « 3 mars, 14:05 », avec les mois en minuscules ;
  - sinon → « 03/03/2025 ».
- Typographie :
  - espace fine insécable (U+202F) avant « ? », « ! », « ; » et « % » ;
  - espace insécable (U+00A0) avant « : » et à l'intérieur de « » ;
  - « — » encadré d'espaces ordinaires.
- Textes revus :
  - `clipboard(n, move)` → « 3 éléments à copier » ;
  - `trashSummary` → « 2 éléments, 3,0 Mo » ;
  - `spaceTotal` → « 100,0 Mo — 3 fichiers » ;
  - `transferLine` → « Copie de photo.jpg — 2 sur 5 · 34 % » ;
  - `selected(n)` → « 3 sélectionnés » ;
  - `emptyList` est remplacé par `noDiskTitle` « Aucun disque branché » et `noDiskBody` « Branchez un disque
    USB-C, chiffré LUKS ou non : ext4, NTFS, exFAT et FAT sont reconnus. ».

- [x] **Step 1 : tests** (`strings_test`) :
  - `pluralFrench` (0, 1, 2) ;
  - `humanSizeUnits` (valeurs ci-dessus) ;
  - `friendlyDateRelative` (`now` fixé au 2026-10-23 15:00 locale : même jour, veille, 3 mars, 2025) ;
  - `frenchTypography` : lit `lib/src/strings_fr.dart` comme texte et n'y trouve aucune des suites `' ?'`, `' !'`,
    `' :'`, `' ;'`, `' %'`, `'« '`, `' »'` avec une espace ordinaire, ni `'(s)'`.
- [x] **Step 2 :** Run `flutter test test/strings_test.dart` → FAIL.
- [x] **Step 3 :** réécrire `Fr`. Les listes de l'explorateur, de la corbeille et des raccourcis affichent
  `friendlyDate`. `shortDate` reste pour les propriétés.
- [x] **Step 4 :** `flutter test` vert après la mise à jour des attentes ; `flutter analyze` propre.
- [x] **Step 5 :** commit `fix(textes): pluriels, unités, dates relatives et typographie française`.

### Task 4 : accueil revu et logo

**Files :**
- Create : `lib/src/widgets/brand_mark.dart`, `lib/src/widgets/shortcut_grid.dart`, `assets/logo/portail.png`,
  `assets/logo/2.0x/portail.png`, `3.0x/…`, `4.0x/…`.
- Modify : `tools/logo/render_icons.py` (option `--flutter <dossier>` : 96, 192, 288 et 384 px), `pubspec.yaml`
  (`assets/logo/`), `lib/src/screens/home_screen.dart`, `widgets/source_tile.dart`, `widgets/disk_card.dart`,
  `widgets/partition_tile.dart`, `widgets/server_tile.dart` (phase 10), `strings_fr.dart`, `test/home_screen_test.dart`,
  `test/filters_test.dart`.

**Interfaces :** Consumes : T1, T2. Produces :
- `BrandMark({double size = 32})` : `Image.asset`, décoratif, exclu de TalkBack.
- `ShortcutGrid({required void Function(String id, String label) onOpen})` : les 6 `quickFilters`, une icône de 56 dp
  dans un cercle `secondaryContainer`, le libellé dessous. 3 colonnes en compact, 6 au-delà.
- Chaînes : `searchEverywhere` « Rechercher dans Portail », `sourceMenu(label)` « Options de $label », `chipEncrypted`
  « Chiffré », `chipOpen` « Ouvert », `chipReadOnly` « Lecture seule », `chipLocked` « Verrouillé », `help` « Aide »,
  `about` « À propos ».
- Mise en page :
  - en-tête : `BrandMark` et « Portail » en `headlineSmall` ; action « Réglages » (infobulle inchangée). Le menu
    « Plus » (Aide, À propos) n'est ajouté qu'en T9 ;
  - sous l'en-tête, une `SearchBar` qui ouvre la recherche globale (T8) ;
  - sections dans l'ordre : carte d'accès aux fichiers (`StatusBanner`), Raccourcis, Stockage, Disques USB, Dossiers
    distants, Favoris ;
  - en `expanded`, deux colonnes : à gauche Raccourcis, Stockage et Favoris ; à droite Disques USB et Dossiers
    distants.
- Stockage : tuile avec icône dans un cercle tonal, place libre en chiffres tabulaires et `UsageBar`. Le menu
  `sourceMenu` contient Corbeille et Analyse de l'espace.
- Disque :
  - en-tête avec icône USB, nom, taille et menu (Formater…) ;
  - chaque partition porte une icône de cadenas fermé ou ouvert et des puces : système de fichiers ; Chiffré ;
    Ouvert (`good`) ; Lecture seule (`warning`) ; « non démonté proprement » en `danger` ;
  - partition ouverte : « Parcourir » (rempli) et « Éjecter » (contour, icône `eject_rounded`). Le menu contient
    « Ouvrir dans Fichiers », Corbeille et Analyse de l'espace ;
  - partition verrouillée : « Déverrouiller » (rempli, icône `lock_open_rounded`).
- Aucun disque : `EmptyState(icon: Icons.usb_rounded, title: Fr.noDiskTitle, message: Fr.noDiskBody)`.

- [x] **Step 1 : tests** (`home_screen_test`, `filters_test`) :
  - `homeHeaderShowsBrandAndSearch` ;
  - `shortcutsOpenFilter` (la tuile « Documents » ouvre `FilterScreen`, `quickFilter documents` appelé). Il remplace
    `homeShowsFilterChips`, qui cherchait des `ActionChip` ;
  - `noDiskShowsEmptyState` ;
  - `usageBarDangerAbove90` ;
  - `lockedPartitionShowsUnlockAndChips` (« Chiffré », « LUKS2 », « Verrouillé ») ;
  - `openPartitionPrimaryActions` (« Parcourir » et « Éjecter » visibles ; « Ouvrir dans Fichiers » et « Corbeille »
    dans le menu, et leurs appels faits) ;
  - `sourceMenuOpensTrashAndSpace` ;
  - `twoColumnsWhenExpanded` (1280 × 800 : l'en-tête « Disques USB » est dans la moitié droite) ;
  - les tests existants de l'accueil restent verts, sélecteurs ajustés.
- [x] **Step 2 :** Run `flutter test test/home_screen_test.dart test/filters_test.dart` → FAIL.
- [x] **Step 3 :** rendre les PNG par
  `python3 tools/logo/render_icons.py assets/logo/portail.svg android/app/src/main/res --flutter assets/logo`, puis
  implémenter l'accueil.
- [x] **Step 4 :** tests verts ; suites vertes.
- [x] **Step 5 :** commit `feat(interface): accueil revu, logo, raccourcis en tuiles et état des disques`.

### Task 5 : déverrouillage, formatage et saisies sensibles

**Files :**
- Modify : `widgets/unlock_dialog.dart`, `screens/format_screen.dart`, `screens/explorer_screen.dart` (`_askName` est
  remplacé par `promptText`), `screens/trash_screen.dart`, `strings_fr.dart`, `test/unlock_dialog_test.dart`,
  `test/format_screen_test.dart`, `test/explorer_screen_test.dart`.

**Interfaces :** Consumes : `promptText`, `nameError`, `confirmAction`, `AppHaptics`, `StatusBanner`. Produces :
- `UnlockDialog` gagne une case « Ouvrir en lecture seule », cochée par défaut selon `readOnlyPreference`. Elle fixe
  `writable`. Le constructeur reste inchangé et `writable` donne la valeur initiale.
- Chaînes : `showPassphrase` « Afficher la phrase de passe », `hidePassphrase` « Masquer la phrase de passe »,
  `openReadOnly` « Ouvrir en lecture seule », `formatDanger(name, size)`.

- [x] **Step 1 : tests :**
  - `toggleShowsPassphrase` (`obscureText` bascule, infobulle mise à jour) ;
  - `passphraseZeroedAfterToggle` : afficher, masquer, valider ; la référence reçue par le faux pont est mise à
    zéro, le champ est vidé ;
  - `readOnlyCheckboxSetsWritable` (cochée → `unlockWritable` contient `false`) ;
  - `wrongPassphraseWarnsHaptic` (un `HapticFeedbackType.mediumImpact` capturé, message d'erreur avec icône) ;
  - `formatStillNeedsTypedName` (bouton désactivé tant que le nom n'est pas tapé ; style `error`) ;
  - `formatChoicesAreCards` (4 choix, aide visible, sélection annoncée) ;
  - `renameSelectsBaseNameAndValidates` (explorateur : « a/b » refusé, OK désactivé ; même nom → aucun appel) ;
  - `newFolderRejectsReservedNames`.
- [x] **Step 2 :** Run `flutter test test/unlock_dialog_test.dart test/format_screen_test.dart test/explorer_screen_test.dart` → FAIL.
- [x] **Step 3 :** implémenter.
  - Le dialogue de déverrouillage a un titre avec icône de cadenas, une barre linéaire indéterminée pendant le calcul
    et l'erreur dans un `StatusBanner` `danger`.
  - Le formatage présente des cartes de choix (`RadioGroup` et `RadioListTile` en `Card`) et un résumé du danger en
    `StatusBanner` `danger`. Le bouton « Formater » est rouge.
- [x] **Step 4 :** tests verts ; suites vertes.
- [x] **Step 5 :** commit `feat(interface): déverrouillage, formatage et saisies de noms revus`.

### Task 6 : collection d'éléments et explorateur revu

**Files :**
- Create : `lib/src/file_types.dart`, `lib/src/widgets/entry_collection.dart`, `lib/src/widgets/breadcrumbs.dart`,
  `test/file_types_test.dart`.
- Modify : `lib/src/viewers.dart` et `widgets/thumb.dart` (les tables passent dans `file_types.dart`),
  `screens/explorer_screen.dart`, `screens/search_screen.dart` (import de `entryIcon` remplacé par `kindIcon`),
  `strings_fr.dart`, `test/explorer_screen_test.dart`, `test/thumb_cache_test.dart`, `test/archives_test.dart` et
  `test/text_editor_test.dart` (imports de `isArchiveName` et `viewerFor`).

**Interfaces :** Produces :
```dart
// file_types.dart (ensembles d'extensions de la phase 10, inchangés)
enum FileKind { folder, image, video, audio, text, document, archive, package, link, other }
FileKind kindOf(EntryMsg e);
IconData kindIcon(FileKind k);                       // variantes _rounded
Color kindColor(FileKind k, ColorScheme s);          // rôles de la direction visuelle
String? viewerFor(String name); bool isArchiveName(String name); bool hasThumbnail(EntryMsg e);
// entry_collection.dart
class EntryItem { const EntryItem({required this.sourceId, required this.entry, this.subtitle}); String get key; } // '$sourceId|${entry.path}'
class EntrySliver extends StatelessWidget {
  const EntrySliver({required AppState state, required ThumbCache thumbs, required List<EntryItem> items, required bool grid,
      bool Function(EntryItem)? isSelected, required void Function(EntryItem) onTap,
      void Function(EntryItem)? onLongPress, void Function(EntryItem)? onMore});
}
class ViewModeButton extends StatelessWidget { const ViewModeButton({required AppState state}); } // bascule viewPrefs.grid
// breadcrumbs.dart
class Breadcrumbs extends StatelessWidget implements PreferredSizeWidget {
  const Breadcrumbs({required String rootLabel, required String path, required ValueChanged<String> onOpen}); // 48 dp, défile vers la fin
}
```
- Liste :
  - vignette de 40 dp, ou icône du type dans un carré arrondi teinté par `kindColor` ;
  - nom sur une ligne et sous-titre ;
  - bouton ⋮ `onMore`, avec l'infobulle `Fr.actionsOf(nom)` ;
  - sélection : coche animée.
- Grille : vignette de rayon 12, nom sur 2 lignes centré, coche en surimpression, fond `primaryContainer` si
  l'élément est sélectionné.
- TalkBack : `Semantics(label: '<nom>, <type>, <taille>', selected: …, button: true)`.
- Chaînes : `selectAll` « Tout sélectionner », `showGrid` « Afficher en grille », `showList` « Afficher en liste »,
  `movedToTrash(n)` « 3 éléments mis à la corbeille ».

- [ ] **Step 1 : tests :**
  - `file_types_test` : `kindOfExtensions` (casse ignorée ; `.tar.gz` → archive ; `.apk` → package ; lien →
    link) ; `viewerForUnchanged` (cas de `text_editor_test` repris).
  - `explorer_screen_test` :
    - `gridAdaptsColumns` (3 colonnes à 412 dp, au moins 9 à 1280 dp) ;
    - `breadcrumbJumpsToAncestor` (`/a/b/c` → toucher « a » → `listDir … /a`) ;
    - `selectAllSelectsEverything` (titre « 4 sélectionnés », haptique `selectionClick`) ;
    - `itemMenuOffersActions` (⋮ → Ouvrir avec…, Partager, Copier, Couper, Renommer, Compresser, Propriétés,
      Supprimer ; écriture désactivée en lecture seule) ;
    - `deleteToTrashOffersUndo` : après la suppression, le message propose « Annuler ». Le toucher lit `listTrash`
      et restaure les éléments dont `originalPath` est un des chemins supprimés et dont `deletedAt` est postérieur au
      début de l'action ;
    - `emptyFolderShowsEmptyState` ;
    - `errorShowsRetry` (Réessayer → second `listDir`) ;
    - `readOnlyHidesWriteActions` (bannière `StatusBanner`, pas de bouton flottant, Couper et Supprimer désactivés) ;
    - `gridTileSemantics` ;
    - `viewToggleFlipsPreference` ;
    - `closedSourcePopsToHome` (existant, gardé).
- [ ] **Step 2 :** Run `flutter test test/file_types_test.dart test/explorer_screen_test.dart` → FAIL.
- [ ] **Step 3 :** implémenter.
  - L'explorateur passe à un `CustomScrollView` dont le contenu est `EntrySliver`, sous `RefreshIndicator`.
  - Barre de titre : recherche, `ViewModeButton`, menu (tri, fichiers cachés, favori, corbeille, analyse) et
    `Breadcrumbs` en `bottom`.
  - Un rechargement discret affiche une barre linéaire en haut. Les états passent par `StateSwitcher`.
- [ ] **Step 4 :** tests verts ; suites vertes.
- [ ] **Step 5 :** commit `feat(explorateur): grille adaptative, fil d'Ariane, menu par élément et annulation`.

### Task 7 : liste ou grille dans les raccourcis et la recherche *(demande de l'utilisateur du 2026-10-23)*

L'utilisateur a remarqué que le choix « Affichage des dossiers » (`ViewPrefsMsg.grid`, dans les Réglages et dans le
menu de l'explorateur) n'est pas appliqué aux écrans des **Raccourcis** de l'accueil (`filter_screen.dart` : Photos,
Vidéos, Musique, Documents, Archives, Récents). Ces écrans sont toujours en liste. Ils suivent désormais le même
choix que l'explorateur, vignettes comprises en grille.

Revue des autres écrans de résultats *(décisions du planificateur)* :
- la recherche (`search_screen.dart`) l'ignore aussi : elle suit désormais le choix ;
- la recherche globale (T8) le suit dès sa création ;
- la corbeille reste en liste : chaque ligne montre l'emplacement d'origine et la date de suppression, et
  `TrashItemMsg` n'a pas de chemin lisible pour une vignette ;
- l'analyse de l'espace reste en liste : ses barres proportionnelles sont l'information ;
- l'aperçu du renommage par lot et la file du lecteur de musique (phase 10) restent en liste ;
- les dossiers distants passent par l'explorateur et suivent déjà le choix.

**Files :** Modify `screens/filter_screen.dart`, `screens/search_screen.dart`, `test/filters_test.dart`,
`test/search_screen_test.dart`, `test/fake_host_api.dart` (résultats de recherche par source ; `thumbSources`
enregistre `sourceId|path` à côté de `thumbRequests`).

**Interfaces :** Consumes : `EntrySliver`, `EntryItem`, `ViewModeButton`, `openViewer`, `showActionSheet`.
- Un `FilterItemMsg` devient `EntryMsg(name, path, type: 'file', size, mtime)` dans un `EntryItem` de sa propre
  source, avec le sous-titre `Fr.filterLine`.
- Toucher un fichier passe par `opensInPortail` et `openViewer`, comme dans l'explorateur. La visionneuse reçoit les
  résultats **de la même source**, dans l'ordre affiché.
- ⋮ (ou appui long en grille) ouvre une feuille : Ouvrir le dossier, Ouvrir avec…, Partager, Propriétés.

- [ ] **Step 1 : tests** (ils échouent aujourd'hui) :
  - `filterFollowsGridPreference` : `viewPrefs.grid = true`. Dans `FilterScreen` (`photos`, deux images du
    téléphone), `find.byType(SliverGrid)` trouve un widget, aucun `ListTile` n'est trouvé pour les éléments, et
    `api.thumbRequests` contient les deux chemins ;
  - `filterListWhenListPreference` (liste, sous-titre `Fr.filterLine` inchangé) ;
  - `filterGridThumbsPerSource` : une image du téléphone et une de la carte SD au même chemin. Deux vignettes
    distinctes sont demandées, chacune à sa source (le faux pont enregistre `sourceId|path`) ;
  - `viewToggleInFilterChangesPreference` (`api.viewPrefs.grid` bascule ; l'explorateur ouvert ensuite est en grille) ;
  - `filterOpensInPortailViewer` (photo → `ImageViewerScreen` avec les seules images de sa source ; PDF →
    `openWith`) ;
  - `filterItemSheetOpenFolder` ;
  - `searchFollowsGridPreference` ;
  - `searchOpensInPortailViewer` ;
  - `trashAndSpaceStayLists` (grille choisie → corbeille et analyse toujours en liste ; ce test documente la
    décision).
- [ ] **Step 2 :** Run `flutter test test/filters_test.dart test/search_screen_test.dart` → FAIL
  (`filterFollowsGridPreference` : aucun `SliverGrid`).
- [ ] **Step 3 :** implémenter (chaque écran garde un `ThumbCache` libéré par `dispose`).
- [ ] **Step 4 :** tests verts ; suites vertes.
- [ ] **Step 5 :** commit `fix(raccourcis): liste ou grille selon le réglage, vignettes et lecteurs de Portail`.

### Task 8 : recherche dans tous les stockages

**Files :**
- Create : `lib/src/screens/global_search_screen.dart`, `test/global_search_test.dart`.
- Modify : `screens/home_screen.dart` (la `SearchBar` ouvre l'écran), `screens/search_screen.dart` (puces « Ici » et
  « Partout »), `strings_fr.dart`, `test/fake_host_api.dart`.

**Interfaces :** Produces :
- `GlobalSearchScreen({required AppState state, String initialQuery = ''})`.
- `List<SourceMsg> searchableSources(AppState s)` : les sources de `kind` `phone`, `removable` et `disk`, dans
  l'ordre de l'accueil. Les dossiers distants sont exclus *(décision du planificateur : trop lents, et chaque appel
  ouvrirait des connexions)*.

Comportement :
- la recherche part 400 ms après la dernière frappe ;
- les sources sont interrogées une à une par `api.search(id, '/', q, showHidden)` ;
- une génération plus récente fait abandonner les résultats d'une ancienne et appelle `cancelSearch` ;
- les résultats sont groupés par source, avec un `SectionHeader` « <libellé> · n » et un `EntrySliver` qui suit
  `viewPrefs.grid` ;
- une source tronquée (plus de 500 résultats) affiche la mention `Fr.tooManyResults` sous son en-tête ;
- quitter l'écran annule la recherche.

Chaînes : `searchHere` « Ici », `searchAll` « Partout », `searchingIn(label)` « Recherche dans $label… ».

- [ ] **Step 1 : tests :**
  - `searchesEveryOpenStorage` (appels pour `phone`, `sd:1` et `v1` ; aucun pour `remote:x`) ;
  - `groupsBySource` ;
  - `globalFollowsGridPreference` ;
  - `staleResultsDropped` (une frappe pendant une recherche → `searchCancels` incrémenté, anciens résultats jamais
    affichés) ;
  - `cancelOnLeave` ;
  - `homeSearchBarOpensGlobal` ;
  - `searchChipEverywhere` (depuis l'explorateur : « Partout » ouvre la recherche globale avec la même saisie) ;
  - `emptyQueryShowsHint` (`EmptyState`) et `noResultEmptyState`.
- [ ] **Step 2 :** Run `flutter test test/global_search_test.dart` → FAIL.
- [ ] **Step 3 :** implémenter.
- [ ] **Step 4 :** tests verts ; suites vertes.
- [ ] **Step 5 :** commit `feat(recherche): rechercher dans tous les stockages depuis l'accueil`.

### Task 9 : réglages regroupés, « À propos », licences, aide et bienvenue

**Files :**
- Create : `lib/src/screens/about_screen.dart`, `lib/src/screens/help_screen.dart`, `lib/src/screens/welcome_screen.dart`,
  `lib/src/licenses.dart`, `assets/licenses/apache-2.0.txt`, `assets/licenses/jsch.txt`,
  `assets/licenses/bouncycastle.txt`, `test/about_help_welcome_test.dart`.
- Modify : `screens/settings_screen.dart`, `screens/home_screen.dart` (menu « Plus » de l'en-tête : Aide, À propos),
  `lib/src/app.dart`,
  `lib/main.dart`, `pubspec.yaml` (assets : `assets/licenses/` et les fichiers de licence de `native/third_party/`),
  Pigeon, `HostApiImpl.kt`, `host_api.dart`, `fake_host_api.dart`, `strings_fr.dart`, `test/settings_screen_test.dart`.

**Interfaces :** Produces :
- Pigeon : `AppInfoMsg(versionName: String, versionCode: int, androidRelease: String, device: String)`,
  `AppInfoMsg getAppInfo()`, rempli par `PackageManager`, `Build.VERSION.RELEASE` et `Build.MODEL`.
- `void registerLicenses()` (appelé par `main`) : `LicenseRegistry.addLicense` pour lwext4, ntfs-3g, FatFs,
  libarchive, XZ Utils, Argon2, cJSON, Mbed TLS (fichiers de `native/third_party/`), et pour Media3, Apache Commons
  Net, JSch, Bouncy Castle (fichiers de `assets/licenses/`).
- Écrans : `AboutScreen({required AppState state})`, `HelpScreen()`, `WelcomeScreen({required AppState state})`.
  `FluLuksApp` montre `WelcomeScreen` tant que `uiPrefs.welcomeSeen` est faux.
- Réglages, en sections `SectionHeader` :
  - **Apparence** : Thème, Affichage des dossiers, Fichiers cachés ;
  - **Fichiers** : Ouvrir dans Portail, Accès aux fichiers du téléphone ;
  - **Disques** : Lecture seule, Verrouillage automatique ;
  - **Confort** : Retours haptiques ;
  - **Portail** : Aide, À propos.
- « À propos » :
  - `BrandMark(72)`, « Portail », « Version 1.4.0 (11) », « Android 13 · Pixel 4 XL » ;
  - la phrase « Disques chiffrés LUKS et gestionnaire de fichiers, sans root » ;
  - le bouton « Licences des bibliothèques » (`showLicensePage` avec nom, version et icône) ;
  - la note « Portail utilise des bibliothèques sous GPL (ntfs-3g, lwext4) : diffusé, il l'est sous GPL, avec ses
    sources. ».
- Aide : 8 `ExpansionTile`, aux titres fixés. Chaque réponse fait 3 phrases au plus, tirées du README.
  1. Ouvrir un disque chiffré
  2. Pourquoi mon disque s'ouvre en lecture seule ?
  3. Débrancher sans risque
  4. Voir le disque dans d'autres applications
  5. La corbeille
  6. Le verrouillage automatique
  7. Les dossiers distants (FTP, SFTP)
  8. Formater un disque
- Bienvenue (une seule page défilante) :
  - `BrandMark(96)` et « Bienvenue dans Portail » ;
  - trois points avec icônes :
    - « Vos disques, même chiffrés » ;
    - « Votre phrase de passe ne quitte jamais la mémoire » ;
    - « Accès aux fichiers du téléphone : nécessaire pour parcourir le téléphone et la carte SD, pas pour les disques
      USB » ;
  - boutons « Autoriser l'accès » (`requestAllFilesAccess`) et « Commencer » (`welcomeSeen = true`).

- [ ] **Step 1 : tests :**
  - `settingsGroupedSections` (les 5 en-têtes, dans l'ordre) ;
  - `hapticsSwitchCallsApi` ;
  - `aboutShowsVersion` (« Version 1.4.0 (11) » depuis le faux pont) ;
  - `licensesRegistered` (`LicenseRegistry.licenses` contient les 12 paquets nommés) ;
  - `licensePageOpens` ;
  - `helpEntriesExpand` (8 titres ; le premier s'ouvre) ;
  - `welcomeShownOnce` (premier rendu → bienvenue ; « Commencer » → accueil et `welcomeSeen` vrai ; rendu suivant →
    accueil directement) ;
  - `welcomeAllowAccessCallsApi` ;
  - `laterKeepsUsbUsable` (« Commencer » sans accès → accueil avec « Disques USB » et la carte d'accès).
- [ ] **Step 2 :** Run `flutter test test/about_help_welcome_test.dart test/settings_screen_test.dart` → FAIL.
- [ ] **Step 3 :** implémenter.
- [ ] **Step 4 :** tests verts ; suites JVM et Flutter vertes.
- [ ] **Step 5 :** commit `feat(interface): réglages regroupés, à propos, licences, aide et bienvenue`.

### Task 10 : barres du bas et écrans de la phase 10

Les écrans visés sont ceux livrés en v1.3.0 : `TextEditorScreen`, `ImageViewerScreen`, `NowPlayingScreen`,
`MiniPlayer`, `ServerFormScreen`, `ServerTile`, `VideoActivity` et `MusicService`. Si un nom diffère à la fusion, les
tests suivent le nom réel, sans changer de comportement.

**Files :**
- Create : `lib/src/widgets/bottom_dock.dart`, `test/phase10_screens_test.dart`.
- Modify : `widgets/clipboard_bar.dart`, `widgets/transfer_bar.dart`, `widgets/mini_player.dart`,
  `screens/text_editor_screen.dart`, `screens/image_viewer_screen.dart`, `screens/now_playing_screen.dart`,
  `screens/server_form_screen.dart`, `widgets/server_tile.dart`, `screens/home_screen.dart`,
  `screens/explorer_screen.dart`, `media/MusicService.kt` (petite icône `ic_launcher_mono`, couleur
  `portail_terracotta`), `AndroidManifest.xml` (thème noir plein écran de `VideoActivity`), `strings_fr.dart`.

**Interfaces :** Produces : `BottomDock({required AppState state, Widget? pending})`.
- L'ordre est fixé de haut en bas : action en attente (presse-papiers ou extraction), lecteur réduit, opération en
  cours.
- Les éléments partagent une surface `surfaceContainerHigh`, un séparateur et `SafeArea`.
- L'accueil et l'explorateur n'empilent plus leurs barres à la main.

Écrans :
- **Éditeur :**
  - ligne d'état en bas, en `labelSmall` : « UTF-8 · CRLF · 120 lignes », ou « UTF-8 avec BOM · LF · … » ;
  - « Lecture seule » en `StatusChip` `warning` ;
  - erreur de lecture : `ErrorState` avec Réessayer ;
  - police à chasse fixe.
- **Visionneuse :** compteur « 3 / 5 » en chiffres tabulaires ; actions avec infobulles ; fond noir gardé.
- **Lecture en cours :** pochette de remplacement (icône dans un carré `primaryContainer`, rayon 28) ; temps en
  chiffres tabulaires ; curseur annoncé « Position 1:23 sur 4:05 ».
- **Lecteur réduit :** libellé TalkBack « Lecture en cours : <titre> ».
- **Formulaire de serveur :**
  - `AdaptiveBody(maxWidth: 640)` ;
  - port validé de 1 à 65535, adresse obligatoire ;
  - avertissement FTP en `StatusBanner` `warning` ;
  - empreinte SSH en police à chasse fixe, avec un bouton Copier.

- [ ] **Step 1 : tests** (`phase10_screens_test`) :
  - `dockOrder` (presse-papiers au-dessus du lecteur, lecteur au-dessus de la copie) ;
  - `dockHiddenWhenIdle` ;
  - `editorStatusLine` (BOM et CRLF ; LF) ;
  - `editorErrorStateRetry` ;
  - `imageViewerCounterTabular` ;
  - `miniPlayerSemantics` ;
  - `nowPlayingSliderSemantics` ;
  - `serverFormValidation` (port 0, port 70000 et adresse vide refusés) ;
  - `ftpWarningBanner`.
- [ ] **Step 2 :** Run `flutter test test/phase10_screens_test.dart` → FAIL.
- [ ] **Step 3 :** implémenter (Kotlin : icône et couleur de la notification, thème de `VideoActivity`).
- [ ] **Step 4 :** tests verts ; suites JVM et Flutter vertes.
- [ ] **Step 5 :** commit `feat(interface): barres du bas unifiées et écrans des lecteurs alignés`.

### Task 11 : accessibilité, grands écrans et tests dorés

**Files :**
- Create : `test/demo_data.dart` (faux pont peuplé : téléphone, carte SD, disque LUKS2 ouvert, partition verrouillée,
  favoris, dossier de 40 éléments variés, serveur SFTP), `test/screen_harness.dart`, `test/accessibility_test.dart`,
  `test/adaptive_test.dart`, `test/source_rules_test.dart`, `test/goldens_test.dart`, `test/goldens/*.png`.
- Modify : les écrans où un test échoue.

**Interfaces :** Produces :
- `FakeHostApi demoApi()`.
- `Future<void> pumpScreen(WidgetTester t, String screen, {Brightness brightness = Brightness.light, Size size = const Size(412, 869), double textScale = 1.0})`.
- Valeurs de `screen` : `home`, `explorerList`, `explorerGrid`, `filterGrid`, `search`, `globalSearch`, `trash`,
  `space`, `settings`, `about`, `help`, `welcome`, `unlock`, `format`, `editor`, `serverForm`, `nowPlaying`.

- [ ] **Step 1 : tests :**
  - `accessibility_test` : pour chaque écran et chaque luminosité, `meetsGuideline(androidTapTargetGuideline)`,
    `labeledTapTargetGuideline` et `textContrastGuideline` ;
  - `adaptive_test` :
    - `noOverflowAt200Percent` : écrans × tailles (412 × 869, 869 × 412, 1280 × 800) × échelles (1,0 et 2,0) ; aucune
      exception de rendu (`tester.takeException()` nul) ;
    - `vitalActionsReachable` : à 200 % et 412 × 869, « Déverrouiller », « Éjecter », « Annuler » (copie en cours) et
      « Enregistrer » (éditeur) sont trouvés et touchables après `ensureVisible` ;
  - `source_rules_test` :
    - `noHardcodedColors` : aucun `Colors.` dans `lib/` hors de `ui/theme.dart`, `image_viewer_screen.dart` et
      `now_playing_screen.dart`, et aucun `Color(0x` hors de `ui/theme.dart` ;
    - `noRawSnackBars` : aucun `SnackBar(` hors de `ui/feedback.dart` ;
    - `noMagicDurations` : aucun `Duration(milliseconds:` dans `lib/src/screens` ni `lib/src/widgets`, hors de la
      temporisation de recherche (400 ms) et de la barre d'opération (4 s) ;
  - `goldens_test` (Linux, police de test) : `home_light`, `home_dark`, `explorer_grid_dark`, `unlock_light`,
    `settings_light`, `welcome_light`, à 412 × 869.
- [ ] **Step 2 :** Run `flutter test test/accessibility_test.dart test/adaptive_test.dart test/source_rules_test.dart` →
  noter les échecs.
- [ ] **Step 3 :** corriger les écrans : `Semantics` manquants, infobulles, `Flexible`/`Wrap` là où cela déborde,
  libellés de jauge.
- [ ] **Step 4 :** Run `flutter test --update-goldens test/goldens_test.dart`. Regarder chaque PNG avant de le
  garder. Puis `flutter test` complet vert et `flutter analyze` propre.
- [ ] **Step 5 :** commit `test(interface): accessibilité, grands écrans, règles de style et tests dorés`.

### Task 12 : captures et essais instrumentés sur le Pixel 4 XL

**Files :**
- Create : `integration_test/ui_tour_test.dart`, `test_driver/integration_test.dart`,
  `docs/superpowers/research/<AAAA-MM-JJ>-essais-phase11/captures/` (date du jour des essais).
- Modify : `pubspec.yaml` (`integration_test: sdk: flutter` en `dev_dependencies`).

**Interfaces :** Consumes : `demoApi()` et `pumpScreen` (T11, importés depuis `../test/`). Aucun disque n'est
touché : l'application tourne sur le faux pont.

- [ ] **Step 1 :** écrire `ui_tour_test` :
  - `captureTour` : captures (`binding.takeScreenshot`) de chaque écran de `pumpScreen`, en clair et en sombre, puis
    de l'accueil, de l'explorateur et du déverrouillage à 200 % et en paysage ;
  - `scroll2000EntriesSmooth` : `binding.watchPerformance` pendant le défilement d'un dossier de 2 000 éléments, en
    liste puis en grille. Le 90ᵉ centile de construction doit rester ≤ 8 ms, et celui du rendu ≤ 16 ms.
- [ ] **Step 2 :** vérifier que le téléphone est au repos et sauvegarder l'APK de l'utilisateur
  (`adb shell pm path com.mawena.fluluks`, puis `adb pull`). Run
  `flutter drive --profile --driver=test_driver/integration_test.dart --target=integration_test/ui_tour_test.dart -d <Pixel>`.
  Attendu : `All tests passed`, captures écrites dans le dossier ci-dessus.
- [ ] **Step 3 :** relire les captures (contraste, coupures, alignements). Corriger ce qui doit l'être, avec ses tests
  Flutter, puis relancer.
- [ ] **Step 4 :** réinstaller l'APK de l'utilisateur.
- [ ] **Step 5 :** commit `test(interface): captures et fluidité mesurées sur le Pixel`.

### Task 13 *(utilisateur)* : essais

**Files :** Create `docs/superpowers/research/<AAAA-MM-JJ>-essais-phase11/essais.md`.

- [ ] Construire et installer l'APK release sur le Pixel **avant** de demander l'essai.
- [ ] Fiche d'essais, une ligne par point, avec une case « Réussi / Remarque » :
  - premier lancement (bienvenue, accès refusé puis accordé) ;
  - thème Système, Clair et Sombre, et démarrage en mode sombre (aucun éclair blanc) ;
  - accueil avec le vrai disque : déverrouiller avec « afficher la phrase » ; puces d'état ; éjecter ;
  - explorateur : grille, fil d'Ariane, ⋮, tout sélectionner, suppression puis « Annuler » ;
  - Raccourcis et recherche en grille (demande de l'utilisateur) ; recherche globale sur le téléphone et le disque ;
  - texte à 200 % (Réglages Android › Taille de police au maximum) ; TalkBack sur l'accueil et l'explorateur ;
  - paysage ;
  - retours haptiques activés, puis désactivés ;
  - « À propos » et licences ; Aide ;
  - musique et dossiers distants (barres du bas, formulaire).

### Task 14 : documentation et v1.4.0

- [ ] README : section « Interface » (thèmes, accessibilité, recherche globale, aide), trois captures choisies dans
  T12, mention de la phase 11 dans le tableau ; feuille de route cochée. Commit `docs: interface complète et
  professionnelle`.
- [ ] Relecture finale de toute la branche.
- [ ] Fusion `feature/interface` → `develop`, puis `release/1.4.0` (pubspec `1.4.0+11`), `main`, et étiquette signée
  `v1.4.0`. Pas de push sans l'accord de l'utilisateur.

---

## À valider avec l'utilisateur

Ces choix ont été faits par le planificateur en l'absence de l'utilisateur. Chacun est accompagné d'une
recommandation. Ils sont à confirmer avant T1, ou aux essais de T13 pour ceux qui se jugent à l'usage.

1. **Couleurs : terracotta du logo partout,** plutôt que les couleurs dynamiques tirées du fond d'écran (Material
   You). *Recommandation : terracotta.* L'identité de Portail est reconnaissable, et le contraste est vérifié par les
   tests. Material You demanderait la dépendance `dynamic_color` et rendrait l'apparence imprévisible.
2. **Police : Roboto du système,** sans police embarquée (Inter, par exemple, sous licence OFL, environ 300 Ko).
   *Recommandation : Roboto,* avec des chiffres tabulaires. Elle est lisible, déjà présente et cohérente avec
   Android.
3. **Navigation : un accueil unique** avec une recherche globale, sans barre de navigation en bas ni deux volets sur
   tablette (les grands écrans ont seulement deux colonnes sur l'accueil et une grille adaptative). *Recommandation :
   oui pour la v1.4.0.* Les deux volets (stockages à gauche, explorateur à droite) iraient dans une phase ultérieure
   si l'utilisateur se sert d'une tablette.
4. **Bienvenue montrée une fois, y compris après la mise à jour** depuis la v1.3.0. *Recommandation : oui.* L'écran
   est court, et il explique l'accès aux fichiers et la confidentialité.
5. **Menu ⋮ sur chaque élément et « Annuler » après une mise à la corbeille.** *Recommandation : oui.* C'est l'usage
   des gestionnaires de fichiers courants, et cela évite le détour par la sélection.
6. **Case « Ouvrir en lecture seule » dans le dialogue de déverrouillage,** préremplie par le réglage.
   *Recommandation : oui.* Le choix se fait au moment où il compte, sans passer par les Réglages.
7. **Dossiers distants exclus de la recherche globale.** *Recommandation : oui.* La recherche reste rapide et ne
   multiplie pas les connexions. On peut toujours chercher dans un dossier distant depuis son explorateur.
8. **Corbeille et analyse de l'espace gardées en liste,** même quand la grille est choisie (T7). *Recommandation :
   oui.* Leurs lignes portent des informations qu'une grille cacherait : emplacement d'origine, date, barres
   proportionnelles.
9. **Écran de démarrage terracotta plein,** avec l'avant-plan blanc du logo, en clair comme en sombre.
   *Recommandation : oui.* Il est cohérent avec l'icône et évite l'éclair blanc en mode sombre.
10. **Retours haptiques activés par défaut.** *Recommandation : oui,* avec un interrupteur dans les Réglages
    (section Confort).

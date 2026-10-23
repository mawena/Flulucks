# Portail — Phase 12 : lecteur vidéo (orientation, pistes, gestes) — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** le lecteur vidéo de la phase 10 s'ouvre en portrait, suit la rotation seulement quand Android l'autorise, se
tourne et se verrouille à la main, choisit ses pistes audio et sous-titres, et avance ou recule en glissant ou en
touchant deux fois, comme VLC. Fin de phase : version **v1.5.0**.

**Architecture :**
- **Logique pure, testée en JVM** (`android/app/src/main/kotlin/com/mawena/fluluks/media/`) : `VideoOrientation.kt`
  (mode d'orientation et valeur demandée à Android), `TrackMenu.kt` (pistes décrites en français et choix proposés),
  `TimeText.kt` (affichage des durées) et `SeekGestures.kt` (automate des gestes : glissement et touchers). Aucune
  dépendance à Media3 ni aux vues ; seules des constantes entières d'Android (`ActivityInfo`) y apparaissent.
- **Adaptateurs Media3** : `PlayerTracks.kt` traduit `Player.currentTracks` en `TrackInfo` et applique un choix
  (`TrackSelectionOverride`, `setTrackTypeDisabled`). Il est essayé par les essais instrumentés.
- **Vues** : `VideoOverlay.kt` construit en code, par-dessus le `PlayerView`, une barre du haut (titre, boutons
  Pistes, Tourner, cadenas), une bulle centrale (aperçu, messages) et deux bulles latérales (cumul des touchers).
  `VideoActivity` relie le tout : `dispatchTouchEvent` passe chaque événement à `SeekGestures` avant le lecteur.
- **Manifeste** : `android:screenOrientation="user"` au lieu de `sensorLandscape`.

**Tech Stack :** Kotlin, Media3 1.11.1 (`media3-exoplayer`, `media3-ui`, déjà présents), JUnit 4 (JVM),
AndroidX Test (instrumenté). **Aucune nouvelle dépendance.**

**Spec :** [docs/superpowers/specs/2026-10-17-fluluks-design.md](../specs/2026-10-17-fluluks-design.md), **§ 20** (et
§ 18 pour le lecteur de la phase 10).

## Global Constraints

- **Branche :** `feature/lecteur-video`, créée depuis `develop` dans l'arbre `../Portail-wt-video`. La phase 11
  (`feature/interface`) change seulement le thème de `VideoActivity` dans le manifeste ; la fusion s'en charge.
- **Commits :** identité `mawena <gamligocharles@gmail.com>`, Conventional Commits en français, **sans aucune
  attribution à Claude**, fichiers nommés (`git commit -- <fichiers>`, nouveaux fichiers ajoutés d'abord). Chaque
  tâche coche ses étapes ici et sa ligne dans la feuille de route, sur la branche **et** aussitôt sur `develop`
  (commit `docs(feuille-de-route): phase 12, Tn faite` qui ne nomme que la feuille de route).
- **Version :** `v1.5.0`, pubspec `1.5.0+12`.
- **Langue :** toute chaîne visible (boutons, descriptions TalkBack, bulles, menus) est en français, rangée en
  constantes dans les objets purs (`VideoOrientation`, `TrackMenu`, `TimeText`) pour être testée.
- **Valeurs fixées** *(décisions du planificateur, voir la fin du plan)* :
  - saut d'un toucher : `VideoActivity.SEEK_MS` = 10 000 ms (déjà utilisé par la musique) ;
  - glissement : **150 ms par dp** parcouru, linéaire, position bornée à [0, durée] (durée inconnue : seulement ≥ 0) ;
  - double toucher : délai `ViewConfiguration.getDoubleTapTimeout()` (300 ms) et distance
    `getScaledDoubleTapSlop()` ; série : un toucher arrivé au plus **700 ms** après le précédent continue le cumul ;
    un toucher dure au plus **500 ms** ;
  - zones : moitié gauche = recul, moitié droite = avance ;
  - bulles : disparaissent 700 ms après le dernier toucher ou le lever du doigt ; message d'orientation 1 500 ms.
- **Inchangé (§ 18, § 20) :** lecture par l'URI de contenu, sous-titres en mémoire, écran allumé pendant la lecture,
  fermeture avec message si le stockage est fermé. `VideoActivity.newPlayer(context, uri, subs)` garde sa signature
  (essai instrumenté de la phase 10).
- **Tests :** à la fin de chaque tâche, `flutter analyze`, `flutter test` et
  `cd android && ./gradlew -q --offline :app:testDebugUnitTest` sont verts.

## Review Focus

1. **Glisser sur la barre de progression ou toucher un bouton visible** : la barre et les boutons doivent garder leur
   comportement, sans saut parasite. → T3 : `dragNeverStartsOnControl` ; T4 : `doubleTapOnPlayButtonIsNotAJump`.
2. **Premier toucher qui montre (ou cache) les commandes, puis second toucher** : le double toucher saute de 10 s et
   les commandes reviennent à leur état d'avant. → T4 : `doubleTapRestoresControls`.
3. **Vidéo distante (FTP, SFTP) ou durée inconnue** : un glissement ne saute qu'une fois, au lever du doigt, et ne
   dépasse ni 0 ni la fin. → T3 : `dragSeeksOnlyOnRelease`, `dragClampsToDuration`, `dragUnknownDurationOnlyClampsAtZero`.
4. **Vidéo sans piste audio, ou sans aucun sous-titre** : le menu Pistes reste utilisable et le dit. → T2 :
   `noAudioTrackSaysSo`, `textChoicesWithoutTracksOfferOnlyOff`.
5. **Rotation automatique d'Android coupée, téléphone tenu en paysage** : le lecteur s'ouvre en portrait (§ 20). → T1 :
   `autoModeUsesUserOrientation` (le mode `user` d'Android applique lui-même le réglage) ; vérifié sur le Pixel en T7.

---

### Task 1 : orientation (portrait par défaut, Tourner, cadenas)

**Files :**
- Create : `android/app/src/main/kotlin/com/mawena/fluluks/media/VideoOrientation.kt`,
  `android/app/src/main/kotlin/com/mawena/fluluks/media/VideoOverlay.kt`,
  `android/app/src/main/res/drawable/video_ic_rotate.xml`, `video_ic_lock.xml`, `video_ic_lock_open.xml`
- Modify : `android/app/src/main/kotlin/com/mawena/fluluks/media/VideoActivity.kt`,
  `android/app/src/main/AndroidManifest.xml` (activité `.media.VideoActivity`)
- Test : `android/app/src/test/kotlin/com/mawena/fluluks/VideoOrientationTest.kt`

**Interfaces :**
- Produces :
  - `sealed interface OrientationMode { object Auto; object Locked; data class Forced(val landscape: Boolean) }`
    (`Auto` = rotation selon Android ; `Locked` = orientation affichée figée ; `Forced` = choisie à la main, figée) ;
  - `object VideoOrientation` : `fun requested(mode: OrientationMode): Int`,
    `fun rotate(mode: OrientationMode, landscapeNow: Boolean): OrientationMode`,
    `fun toggleLock(mode: OrientationMode): OrientationMode`, `fun isLocked(mode: OrientationMode): Boolean`,
    `fun lockDescription(mode: OrientationMode): String`, `fun message(mode: OrientationMode): String`,
    constante `ROTATE = "Tourner l'écran"` ;
  - `class VideoOverlay(context: Context)` : `val root: FrameLayout` (contient le `PlayerView` donné à
    `attach(player: View)`), `val rotateButton`, `val lockButton: ImageButton`, `fun setTitle(text: String)`,
    `fun setLocked(locked: Boolean, description: String)`, `fun setBarVisible(visible: Boolean)`,
    `fun showInfo(text: String, hideAfterMs: Long?)`, `fun hideInfo()`, `fun controlRects(): List<Rect>` (zones de la
    barre du haut, si visible, en coordonnées de la fenêtre).

- [ ] **Step 1 : tests en échec** (`VideoOrientationTest`)

```kotlin
@Test fun autoModeUsesUserOrientation() = assertEquals(SCREEN_ORIENTATION_USER, requested(Auto))
@Test fun lockedModeFreezesCurrentRotation() = assertEquals(SCREEN_ORIENTATION_LOCKED, requested(Locked))
@Test fun forcedModes() {
    assertEquals(SCREEN_ORIENTATION_USER_LANDSCAPE, requested(Forced(true)))
    assertEquals(SCREEN_ORIENTATION_USER_PORTRAIT, requested(Forced(false)))
}
@Test fun rotateSwitchesAndLocks() {
    assertEquals(Forced(true), rotate(Auto, landscapeNow = false))
    assertEquals(Forced(false), rotate(Forced(true), landscapeNow = true))
    assertEquals(Forced(true), rotate(Locked, landscapeNow = false))
}
@Test fun lockToggle() {
    assertEquals(Locked, toggleLock(Auto)); assertEquals(Auto, toggleLock(Locked)); assertEquals(Auto, toggleLock(Forced(true)))
    assertFalse(isLocked(Auto)); assertTrue(isLocked(Locked)); assertTrue(isLocked(Forced(false)))
}
@Test fun frenchTexts() {
    assertEquals("Verrouiller l'orientation", lockDescription(Auto))
    assertEquals("Déverrouiller l'orientation", lockDescription(Forced(true)))
    assertEquals("Rotation automatique", message(Auto))
    assertEquals("Orientation verrouillée", message(Locked))
    assertEquals("Paysage verrouillé", message(Forced(true)))
    assertEquals("Portrait verrouillé", message(Forced(false)))
}
```

- [ ] **Step 2 :** `./gradlew -q --offline :app:testDebugUnitTest --tests '*VideoOrientationTest'` → échec de
  compilation (`VideoOrientation` absent).
- [ ] **Step 3 :** écrire `VideoOrientation.kt` (constantes `ActivityInfo.SCREEN_ORIENTATION_*`, inlinées à la
  compilation, donc utilisables en JVM).
- [ ] **Step 4 :** relancer → vert.
- [ ] **Step 5 : vues.** Icônes vectorielles blanches 24 dp reprises d'AOSP (Apache-2.0) :
  `ic_qs_auto_rotate` → `video_ic_rotate`, `ic_lock` → `video_ic_lock`, `ic_lock_open` → `video_ic_lock_open`
  (`platforms/android-36/data/res/drawable/`). `VideoOverlay` : barre du haut en dégradé noir → transparent,
  titre (nom du fichier, une ligne, ellipse au milieu), boutons de 48 dp avec `contentDescription` ; marge haute selon
  les encarts système (barres et encoche) ; bulle centrale (texte blanc 20 sp sur fond noir 60 %, coins de 16 dp).
- [ ] **Step 6 : activité.** `mode` (`OrientationMode.Auto` au départ) ; `requestedOrientation` posé avant
  `setContentView` ; Tourner → `rotate(mode, resources.configuration.orientation == ORIENTATION_LANDSCAPE)` ;
  cadenas → `toggleLock(mode)` ; après chaque changement : `requestedOrientation`, `setLocked`, `showInfo(message,
  1 500)`, et `playerView.showController()` (relance la temporisation des commandes). La barre du haut suit les
  commandes (`setControllerVisibilityListener`). Manifeste : `android:screenOrientation="user"`.
- [ ] **Step 7 :** suites vertes ; commit `feat(video): portrait par défaut, rotation selon Android, Tourner et cadenas`.

### Task 2 : pistes audio et sous-titres

**Files :**
- Create : `media/TrackMenu.kt`, `media/PlayerTracks.kt`, `res/drawable/video_ic_tracks.xml`
- Modify : `media/VideoActivity.kt` (bouton Pistes, `newPlayer` : libellé des `.srt`), `media/VideoOverlay.kt`
- Test : `android/app/src/test/kotlin/com/mawena/fluluks/TrackMenuTest.kt`

**Interfaces :**
- Produces :
  - `enum class TrackKind { AUDIO, TEXT }` ;
    `data class TrackInfo(val kind: TrackKind, val group: Int, val index: Int, val language: String?, val label: String?, val channels: Int, val selected: Boolean)`
    (`group` et `index` : place dans `Tracks.groups` et dans le groupe ; `channels` ≤ 0 si inconnu) ;
  - `data class TrackChoice(val title: String, val track: TrackInfo?, val checked: Boolean)` (`track` nul = sous-titres
    désactivés) ;
  - `object TrackMenu` : constantes `TITLE = "Pistes"`, `AUDIO = "Audio"`, `TEXT = "Sous-titres"`,
    `OFF = "Désactivés"`, `NO_AUDIO = "Aucune piste audio"` ; `fun languageName(code: String?): String?`,
    `fun title(track: TrackInfo, number: Int): String`, `fun audio(tracks: List<TrackInfo>): List<TrackChoice>`,
    `fun text(tracks: List<TrackInfo>): List<TrackChoice>` ;
  - `object PlayerTracks` : `fun infos(tracks: Tracks): List<TrackInfo>` (pistes audio et texte **prises en charge**
    seulement), `fun apply(player: Player, tracks: Tracks, choice: TrackChoice, kind: TrackKind)`.

- [ ] **Step 1 : tests en échec** (`TrackMenuTest`)

```kotlin
private fun audio(g: Int, lang: String?, label: String? = null, ch: Int = 2, sel: Boolean = false) =
    TrackInfo(TrackKind.AUDIO, g, 0, lang, label, ch, sel)
private fun text(g: Int, lang: String?, label: String?, sel: Boolean = false) = TrackInfo(TrackKind.TEXT, g, 0, lang, label, -1, sel)

@Test fun languageNames() {
    assertEquals("Français", languageName("fr")); assertEquals("Anglais", languageName("en"))
    assertNull(languageName(null)); assertNull(languageName("und")); assertNull(languageName(""))
    assertEquals("Portugais (Brésil)", languageName("pt-BR"))
}
@Test fun audioTitles() {
    assertEquals("Français · Stéréo", title(audio(0, "fr"), 1))
    assertEquals("Anglais · Commentaires · 5.1", title(audio(1, "en", "Commentaires", 6), 2))
    assertEquals("Mono", title(audio(2, null, ch = 1), 3))
    assertEquals("Piste 4", title(audio(3, null, ch = -1), 4))
    assertEquals("Français · 7.1", title(audio(4, "fr", "Français", 8), 5)) // libellé égal à la langue : une fois
    assertEquals("3 canaux", title(audio(5, null, ch = 3), 6).substringAfterLast(" · "))
}
@Test fun audioChoicesKeepOrderAndSelection() {
    val c = audio(listOf(audio(0, "fr", sel = true), audio(1, "en")))
    assertEquals(listOf("Français · Stéréo", "Anglais · Stéréo"), c.map { it.title })
    assertEquals(listOf(true, false), c.map { it.checked })
}
@Test fun duplicateTitlesAreNumbered() =
    assertEquals(listOf("Français · Stéréo (1)", "Français · Stéréo (2)"), audio(listOf(audio(0, "fr"), audio(1, "fr"))).map { it.title })
@Test fun noAudioTrackSaysSo() = assertTrue(audio(emptyList()).isEmpty()) // l'activité affiche NO_AUDIO, désactivé
@Test fun textChoicesStartWithOff() {
    val c = text(listOf(text(0, "fr", "film.fr.srt", sel = true), text(1, null, "film.srt")))
    assertEquals(listOf("Désactivés", "Français · film.fr.srt", "film.srt"), c.map { it.title })
    assertEquals(listOf(false, true, false), c.map { it.checked }); assertNull(c[0].track)
}
@Test fun textChoicesWithoutTracksOfferOnlyOff() =
    assertEquals(listOf(TrackChoice("Désactivés", null, true)), text(emptyList()))
@Test fun offIsCheckedWhenNoTextSelected() = assertTrue(text(listOf(text(0, "fr", null))).first().checked)
```

- [ ] **Step 2 :** lancer `*TrackMenuTest` → échec de compilation.
- [ ] **Step 3 :** écrire `TrackMenu.kt`. Règles du titre : parties = nom de la langue
  (`Locale.forLanguageTag(code).getDisplayName(Locale.FRENCH)`, première lettre en capitale ; `und` ou vide → rien ;
  nom inconnu → le code tel quel), libellé (s'il n'est ni vide, ni égal au nom de la langue ou au code, casse ignorée),
  canaux pour l'audio (1 « Mono », 2 « Stéréo », 6 « 5.1 », 8 « 7.1 », autre > 0 « N canaux ») ; jointes par
  « · » ; aucune partie → « Piste N ». Titres en double suffixés de « (N) », N = rang dans la liste.
- [ ] **Step 4 :** relancer → vert.
- [ ] **Step 5 : `PlayerTracks`.** `infos` : groupes `C.TRACK_TYPE_AUDIO` et `C.TRACK_TYPE_TEXT`, pistes
  `isTrackSupported(i)`, `Format.language`, `Format.label`, `Format.channelCount` (`Format.NO_VALUE` → -1),
  `isTrackSelected(i)`. `apply` : piste → `setTrackTypeDisabled(kind, false)` +
  `setOverrideForType(TrackSelectionOverride(groups[group].mediaTrackGroup, index))` ; `track` nul (texte) →
  `clearOverridesOfType(C.TRACK_TYPE_TEXT)` + `setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true)`.
- [ ] **Step 6 : vues.** Icône `video_ic_tracks` (copie d'`exo_ic_audiotrack` de Media3, Apache-2.0). Bouton Pistes
  dans la barre du haut (avant Tourner). Toucher → `PopupMenu` ancré au bouton : sous-menus « Audio » et
  « Sous-titres », éléments à cocher exclusifs (`setGroupCheckable(…, true, true)`), « Aucune piste audio » désactivé
  si la liste est vide ; le choix appelle `PlayerTracks.apply` sur l'instantané `currentTracks` qui a servi au menu.
  `PlayerView.setShowSubtitleButton(false)` : le bouton Pistes le remplace. `newPlayer` : libellé d'un `.srt` = nom
  du fichier (`film.fr.srt`), langue inchangée.
- [ ] **Step 7 :** suites vertes ; commit `feat(video): choix des pistes audio et des sous-titres`.

### Task 3 : glisser pour avancer ou reculer

**Files :**
- Create : `media/TimeText.kt`, `media/SeekGestures.kt`
- Modify : `media/VideoActivity.kt` (`dispatchTouchEvent`), `media/VideoOverlay.kt`
- Test : `android/app/src/test/kotlin/com/mawena/fluluks/TimeTextTest.kt`,
  `android/app/src/test/kotlin/com/mawena/fluluks/SeekGesturesTest.kt`

**Interfaces :**
- Produces :
  - `object TimeText` : `fun clock(ms: Long): String` (« 0:05 », « 12:34 », « 1:02:03 », négatif → « 0:00 »),
    `fun delta(ms: Long): String` (« +0:25 », « −1:05 », signe moins U+2212), `fun seconds(ms: Long): String`
    (« +20 s », « −30 s »), `fun preview(targetMs: Long, deltaMs: Long, durationMs: Long): String`
    (« 12:34 / 1:30:00\n+0:25 » ; durée ≤ 0 → « 12:34\n+0:25 »), `fun clamp(ms: Long, durationMs: Long): Long` ;
  - `class SeekGestures(density: Float, touchSlop: Float, doubleTapSlop: Float, doubleTapTimeoutMs: Long = 300, streakMs: Long = 700, tapMaxMs: Long = 500, stepMs: Long = 10_000, msPerDp: Long = 150)`
    avec `data class Playback(val positionMs: Long, val durationMs: Long, val seekable: Boolean)`,
    `data class Outcome(val dispatch: Boolean, val cancelChildren: Boolean = false, val effect: Effect? = null)`,
    `sealed interface Effect { data class Preview(val targetMs: Long, val deltaMs: Long); data class DragEnd(val targetMs: Long, val deltaMs: Long); data class Jump(val stepMs: Long, val totalMs: Long, val restoreControls: Boolean) }`,
    et `fun down(x: Float, y: Float, timeMs: Long, width: Int, onControl: Boolean, playback: Playback): Outcome`,
    `fun move(x: Float, y: Float): Outcome`, `fun up(x: Float, y: Float, timeMs: Long): Outcome`,
    `fun cancel(): Outcome`. `dispatch` : l'activité passe l'événement au lecteur ; `cancelChildren` : elle lui envoie
    d'abord un `ACTION_CANCEL` (le geste lui est retiré).

- [ ] **Step 1 : tests en échec** (`TimeTextTest`, puis la partie glissement de `SeekGesturesTest` ; densité 2,
  seuil 16 px, largeur 1 000 px, lecture à 60 000 ms sur 600 000 ms)

```kotlin
@Test fun clocks() { assertEquals("0:05", clock(5_400)); assertEquals("12:34", clock(754_000)); assertEquals("1:02:03", clock(3_723_000)); assertEquals("0:00", clock(-1)) }
@Test fun deltas() { assertEquals("+0:25", delta(25_000)); assertEquals("−1:05", delta(-65_000)); assertEquals("+20 s", seconds(20_000)); assertEquals("−30 s", seconds(-30_000)) }
@Test fun previews() { assertEquals("12:34 / 1:30:00\n+0:25", preview(754_000, 25_000, 5_400_000)); assertEquals("12:34\n−0:05", preview(754_000, -5_000, -1)) }

@Test fun dragRightPreviewsThenSeeksOnRelease() {          // 200 px = 100 dp = 15 s
    assertTrue(g.down(100f, 500f, 0, 1000, false, pb).dispatch)
    val start = g.move(140f, 502f)                           // au-delà du seuil : le geste est pris
    assertFalse(start.dispatch); assertTrue(start.cancelChildren)
    assertEquals(Preview(66_000, 6_000), start.effect)       // 40 px
    assertEquals(Preview(75_000, 15_000), g.move(300f, 510f).effect)
    val end = g.up(300f, 510f, 400)
    assertFalse(end.dispatch); assertEquals(DragEnd(75_000, 15_000), end.effect)
}
@Test fun dragSeeksOnlyOnRelease() // aucun effet DragEnd avant up, un seul après
@Test fun dragLeftRewinds() // 100 → 0 px : Preview(45_000, -15_000)
@Test fun dragClampsToDuration() // lecture à 595 000 : Preview(600_000, 5_000) ; à 2 000 vers la gauche : Preview(0, -2_000)
@Test fun dragUnknownDurationOnlyClampsAtZero() // durationMs = -1 : +15 s depuis 60 000 → 75 000
@Test fun verticalMoveIsNotADrag() // move(102, 600) : dispatch vrai, aucun effet ; up : dispatch vrai
@Test fun dragNeverStartsOnControl() // down(onControl = true) puis move(400, 500) : dispatch vrai, aucun effet
@Test fun notSeekableNeverDrags() // seekable = false : tout passe au lecteur, aucun effet
@Test fun cancelEndsDragWithoutSeek() // pendant un glissement : cancel() → DragEnd(60_000, 0), dispatch vrai
```

- [ ] **Step 2 :** lancer `*TimeTextTest` et `*SeekGesturesTest` → échec de compilation.
- [ ] **Step 3 :** écrire `TimeText.kt` et la partie glissement de `SeekGestures.kt`. Automate : `IDLE`, `PASS`
  (geste laissé au lecteur : départ sur une commande, ou vidéo non navigable), `PRESSED` (toucher passé au lecteur,
  peut devenir un toucher ou un glissement), `HELD` (toucher retenu, T4), `DRAGGING`. Le glissement part de `PRESSED`
  ou `HELD` quand |dx| > seuil et |dx| > |dy| ; cible = `clamp(position + dx / density × msPerDp, durée)`, figée sur la
  position au `down`. `cancelChildren` seulement depuis `PRESSED`.
- [ ] **Step 4 :** relancer → vert.
- [ ] **Step 5 : activité.** `dispatchTouchEvent` : `ACTION_DOWN`, `MOVE`, `UP`, `CANCEL` vont à `SeekGestures`
  (coordonnées de la fenêtre, largeur du `PlayerView`, `onControl` = point dans `controlRects()` de l'overlay ou, si
  les commandes sont visibles, dans `exo_bottom_bar`, `exo_progress` ou `exo_center_controls`) ; les autres actions
  passent telles quelles. `Preview` → `showInfo(TimeText.preview(…), null)` ; `DragEnd` → `hideInfo()` et
  `seekTo(target)` si l'écart n'est pas nul. `Playback` lu sur le lecteur (`currentPosition`, `duration` ou -1 si
  `C.TIME_UNSET`, `isCurrentMediaItemSeekable`) ; lecteur absent → non navigable.
- [ ] **Step 6 :** suites vertes ; commit `feat(video): glisser pour avancer ou reculer, avec l'aperçu de la position`.

### Task 4 : toucher deux fois, cumulable

**Files :** Modify `media/SeekGestures.kt`, `media/VideoActivity.kt`, `media/VideoOverlay.kt` ; Test
`SeekGesturesTest.kt`.

**Interfaces :** Consumes `SeekGestures`, `TimeText.seconds`, `TimeText.clamp` (T3). Produces
`VideoOverlay.showJump(forward: Boolean, text: String)`.

- [ ] **Step 1 : tests en échec** (suite de `SeekGesturesTest`)

```kotlin
@Test fun doubleTapRightJumpsForward() {
    assertTrue(g.down(800f, 500f, 0, 1000, false, pb).dispatch); assertTrue(g.up(800f, 500f, 80).dispatch)
    assertFalse(g.down(805f, 505f, 250, 1000, false, pb).dispatch)
    assertEquals(Outcome(false, effect = Jump(10_000, 10_000, true)), g.up(805f, 505f, 320))
}
@Test fun doubleTapLeftJumpsBack() // Jump(-10_000, -10_000, true)
@Test fun streakAddsTenSecondsPerTap() // 3e et 4e touchers, chacun < 700 ms après le précédent : Jump(10_000, 20_000, false), Jump(10_000, 30_000, false) ; down et up retenus
@Test fun streakChangesSide() // série à droite, puis toucher à gauche : Jump(-10_000, -10_000, false)
@Test fun streakEndsAfterDelay() // toucher 800 ms après : dispatch vrai, aucun effet (premier toucher d'un nouveau double)
@Test fun slowSecondTapIsNotADoubleTap() // second down 350 ms après le premier up : dispatch vrai
@Test fun farSecondTapIsNotADoubleTap() // second down à plus de doubleTapSlop : dispatch vrai
@Test fun longPressIsNotATap() // premier toucher de 600 ms : le second n'est pas un double
@Test fun doubleTapOnPlayButtonIsNotAJump() // premier down onControl = true : dispatch vrai partout, aucun Jump
@Test fun doubleTapRestoresControls() // le second toucher du double porte restoreControls = true (les suivants non)
@Test fun secondTapTurnedIntoDragIsADrag() // second down retenu, puis move de 200 px : Preview, puis DragEnd ; aucun Jump
@Test fun notSeekableDoubleTapPasses() // seekable = false : les deux touchers passent au lecteur
```

- [ ] **Step 2 :** lancer → échecs.
- [ ] **Step 3 :** compléter `SeekGestures` : un toucher de `PRESSED` (sans glissement, ≤ `tapMaxMs`) devient candidat
  (position, heure du `up`) s'il n'est pas parti d'une commande ; un `down` à ≤ `doubleTapTimeoutMs` du `up` candidat
  et à ≤ `doubleTapSlop` de lui passe en `HELD` (retenu, même sur une commande devenue visible) ; un `down` hors
  commande à ≤ `streakMs` du dernier saut passe en `HELD` (série) ; au `up` d'un `HELD` resté toucher : `Jump` (côté
  = moitié de la largeur ; même côté : cumul, sinon cumul repart de ±10 s) ; tout autre `down` termine la série.
- [ ] **Step 4 :** relancer → vert.
- [ ] **Step 5 : activité et vues.** Au `down` passé en `PRESSED`, retenir `controllerWasVisible`
  (`playerView.isControllerFullyVisible`). `Jump` → `seekTo(TimeText.clamp(currentPosition + step, durée))`,
  `showJump(step > 0, TimeText.seconds(total))` (bulle au tiers gauche ou droit, cachée 700 ms après), et si
  `restoreControls` : `showController()` ou `hideController()` selon `controllerWasVisible`.
- [ ] **Step 6 :** suites vertes ; commit `feat(video): toucher deux fois pour reculer ou avancer de 10 s, cumulable`.

### Task 5 : documentation

- [ ] README : la ligne **Vidéo** des lecteurs intégrés décrit le portrait, la rotation selon Android, Tourner, le
  cadenas, les pistes et les gestes ; tableau de la feuille de route déjà à jour (phase 12 en v1.5.0). Fiche d'essais
  `docs/superpowers/research/2026-10-23-essais-phase12/essais.md` (T7). Commit `docs: lecteur vidéo de la phase 12`.

### Task 6 : essais instrumentés sur le Pixel

**Files :** Create `tools/fixtures/make_video_phase12.sh`,
`android/app/src/androidTest/kotlin/com/mawena/fluluks/VideoPlayerInstrumentedTest.kt`.

- [ ] **Step 1 :** script `make_video_phase12.sh <dossier>` (ffmpeg) : `video-phase12.mkv`, 6 s, mire 320 × 240,
  deux pistes audio AAC (`fra` stéréo titrée « Original », `eng` mono titrée « Commentaires ») et une piste de
  sous-titres SubRip intégrée (`eng`). Contrôle par `ffprobe` (3 pistes audio+texte).
- [ ] **Step 2 :** essais écrits et compilés (`./gradlew -q --offline :app:compileDebugAndroidTestKotlin`) :
  - `tracksListedInFrench` : lecteur de `newPlayer` sur `video-phase12.mkv` + `film.fr.srt` à côté ;
    `TrackMenu.audio(PlayerTracks.infos(…))` donne « Français · Original · Stéréo » et « Anglais · Commentaires · Mono » ;
    `TrackMenu.text` commence par « Désactivés », contient « Français · film.fr.srt » et une piste « Anglais » ;
  - `audioTrackSwitches` : choisir la seconde piste audio → elle seule est choisie ;
  - `subtitlesCanBeDisabled` : `OFF` → `isTypeSelected(C.TRACK_TYPE_TEXT)` faux ; retour au `.srt` → vrai ;
  - `activityStartsInUserOrientationAndLocks` : `VideoActivity` lancée sur la vidéo → `requestedOrientation` =
    `SCREEN_ORIENTATION_USER` ; clic sur le cadenas → `LOCKED` ; clic sur Tourner → `USER_LANDSCAPE` ou
    `USER_PORTRAIT` ; re-clic sur le cadenas → `USER`.
- [ ] **Step 3 *(téléphone)* :** pousser la fixture (`adb push … /data/local/tmp/fluluks/fixtures/`), lancer
  `connectedDebugAndroidTest` filtré sur `VideoPlayerInstrumentedTest` et `ReadersInstrumentedTest` → verts ;
  commit `test(video): essais instrumentés de la phase 12 sur le Pixel`.

### Task 7 *(utilisateur)* : essais sur le Pixel

- [ ] Construire et installer l'APK sur le Pixel **avant** de demander l'essai.
- [ ] Fiche `docs/superpowers/research/2026-10-23-essais-phase12/essais.md` remplie (orientation avec la rotation
  automatique activée puis coupée, Tourner, cadenas, pistes, glissement, double toucher et série, vidéo distante).

### Fin de phase

- [ ] Relecture finale de toute la branche.
- [ ] Fusion `feature/lecteur-video` → `develop`, puis `release/1.5.0` (pubspec `1.5.0+12`), `main`, étiquette
  `v1.5.0`. Pas de push sans l'accord de l'utilisateur.

---

## Décisions du planificateur

Prises en l'absence de l'utilisateur ; chacune se change à faible coût si l'essai la contredit.

1. **Rotation automatique déléguée à Android** : le mode automatique demande `SCREEN_ORIENTATION_USER`, qui suit le
   capteur quand la rotation automatique est activée et garde sinon l'orientation du téléphone (portrait en général),
   y compris quand l'utilisateur change le réglage pendant la lecture. Pas de lecture de
   `Settings.System.ACCELEROMETER_ROTATION` ni d'observateur. *Coût si faux : lire ce réglage et l'observer dans
   `VideoActivity`, `requested` prenant un booléen de plus.*
2. **Tourner verrouille** l'orientation choisie (sinon le capteur l'annulerait aussitôt quand la rotation automatique
   est activée) ; le cadenas fermé se rouvre d'un toucher et rend la rotation automatique. Le paysage choisi suit les
   deux côtés selon le réglage d'Android (`USER_LANDSCAPE`). Le mode n'est pas mémorisé d'une vidéo à l'autre.
3. **Bouton Pistes à la place du bouton sous-titres de Media3**, menu `PopupMenu` à deux sous-menus. Le choix de piste
   audio du menu ⚙ de Media3 reste là (traduit par Media3).
4. **Glissement linéaire de 150 ms par dp** (environ 1 min sur la largeur du Pixel en portrait, 2 min en paysage),
   saut au lever du doigt seulement (une vidéo distante ne recharge son flux qu'une fois). Pas de geste vertical
   (volume, luminosité) : non demandé.
5. **Double toucher par moitié d'écran** (gauche / droite), série de 700 ms entre deux touchers, chaque toucher saute
   aussitôt (pas d'attente de la fin de la série) ; le premier toucher montre ou cache les commandes comme avant, et le
   second les remet dans leur état d'avant.
6. **Pas de nouvelle dépendance** ; icônes reprises d'AOSP et de Media3 (Apache-2.0, déjà dans les licences).

package com.mawena.fluluks.media

import android.content.pm.ActivityInfo

/** Orientation voulue pour le lecteur vidéo (phase 12, spec § 20). */
sealed interface OrientationMode {
    /** Rotation selon Android : suit le capteur seulement si la rotation automatique est activée. */
    data object Auto : OrientationMode

    /** Orientation affichée figée (cadenas fermé). */
    data object Locked : OrientationMode

    /** Portrait ou paysage choisi à la main avec « Tourner », figé lui aussi. */
    data class Forced(val landscape: Boolean) : OrientationMode
}

/**
 * Choix d'orientation du lecteur vidéo, sans vue : la valeur demandée à Android (`requestedOrientation`) et les
 * textes des boutons. Le mode automatique s'appuie sur les modes « user » d'Android, qui appliquent eux-mêmes le
 * réglage de rotation automatique, y compris s'il change pendant la lecture.
 */
object VideoOrientation {
    const val ROTATE = "Tourner l'écran"

    fun requested(mode: OrientationMode): Int = when (mode) {
        OrientationMode.Auto -> ActivityInfo.SCREEN_ORIENTATION_USER
        OrientationMode.Locked -> ActivityInfo.SCREEN_ORIENTATION_LOCKED
        is OrientationMode.Forced ->
            if (mode.landscape) ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT
    }

    /** « Tourner » : l'autre orientation que celle affichée ([landscapeNow]), figée. */
    fun rotate(mode: OrientationMode, landscapeNow: Boolean): OrientationMode = OrientationMode.Forced(!landscapeNow)

    /** Cadenas : fige l'orientation affichée, ou rend la rotation automatique. */
    fun toggleLock(mode: OrientationMode): OrientationMode =
        if (mode == OrientationMode.Auto) OrientationMode.Locked else OrientationMode.Auto

    fun isLocked(mode: OrientationMode): Boolean = mode != OrientationMode.Auto

    /** Description du cadenas pour TalkBack : ce que fait un toucher. */
    fun lockDescription(mode: OrientationMode): String =
        if (isLocked(mode)) "Déverrouiller l'orientation" else "Verrouiller l'orientation"

    /** Message bref affiché après un changement de mode. */
    fun message(mode: OrientationMode): String = when (mode) {
        OrientationMode.Auto -> "Rotation automatique"
        OrientationMode.Locked -> "Orientation verrouillée"
        is OrientationMode.Forced -> if (mode.landscape) "Paysage verrouillé" else "Portrait verrouillé"
    }
}

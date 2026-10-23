package com.mawena.fluluks

import android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
import android.content.pm.ActivityInfo.SCREEN_ORIENTATION_USER
import android.content.pm.ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
import android.content.pm.ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT
import com.mawena.fluluks.media.OrientationMode.Auto
import com.mawena.fluluks.media.OrientationMode.Forced
import com.mawena.fluluks.media.OrientationMode.Locked
import com.mawena.fluluks.media.VideoOrientation.isLocked
import com.mawena.fluluks.media.VideoOrientation.lockDescription
import com.mawena.fluluks.media.VideoOrientation.message
import com.mawena.fluluks.media.VideoOrientation.requested
import com.mawena.fluluks.media.VideoOrientation.rotate
import com.mawena.fluluks.media.VideoOrientation.toggleLock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Orientation du lecteur vidéo (phase 12, spec § 20). */
class VideoOrientationTest {
    /** Le mode « user » d'Android suit le capteur seulement si la rotation automatique est activée. */
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
        assertEquals(Forced(false), rotate(Auto, landscapeNow = true))
    }

    @Test fun lockToggle() {
        assertEquals(Locked, toggleLock(Auto))
        assertEquals(Auto, toggleLock(Locked))
        assertEquals(Auto, toggleLock(Forced(true)))
        assertFalse(isLocked(Auto))
        assertTrue(isLocked(Locked))
        assertTrue(isLocked(Forced(false)))
    }

    @Test fun frenchTexts() {
        assertEquals("Verrouiller l'orientation", lockDescription(Auto))
        assertEquals("Déverrouiller l'orientation", lockDescription(Forced(true)))
        assertEquals("Déverrouiller l'orientation", lockDescription(Locked))
        assertEquals("Rotation automatique", message(Auto))
        assertEquals("Orientation verrouillée", message(Locked))
        assertEquals("Paysage verrouillé", message(Forced(true)))
        assertEquals("Portrait verrouillé", message(Forced(false)))
    }
}

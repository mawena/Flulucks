package com.mawena.fluluks

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.mawena.fluluks.remote.KeystoreSecretBox
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith

/** Secrets des serveurs scellés par la clé du Keystore d'Android (AES-GCM), sur le Pixel. */
@RunWith(AndroidJUnit4::class)
class SecretBoxInstrumentedTest {
    @Test fun sealOpenRoundTripAndTamperRefused() {
        val box = KeystoreSecretBox()
        val plain = "mot de passe é 123".toByteArray()
        val a = box.seal(plain)
        val b = box.seal(plain)
        assertFalse(a.contentEquals(b)) // vecteur d'initialisation neuf à chaque fois
        assertFalse(String(a, Charsets.ISO_8859_1).contains("mot de passe"))
        assertArrayEquals(plain, box.open(a))
        assertArrayEquals(plain, KeystoreSecretBox().open(b)) // même clé d'une instance à l'autre
        a[a.size - 1] = (a[a.size - 1].toInt() xor 1).toByte()
        try {
            box.open(a)
            fail("scellé modifié accepté")
        } catch (e: Exception) {
        }
    }
}

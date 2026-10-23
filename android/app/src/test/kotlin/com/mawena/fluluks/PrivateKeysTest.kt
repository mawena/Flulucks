package com.mawena.fluluks

import com.jcraft.jsch.JSch
import com.jcraft.jsch.KeyPair
import com.mawena.fluluks.remote.PrivateKeys
import java.io.ByteArrayOutputStream
import java.io.IOException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** Clés privées importées : type, empreinte, chiffrement ; jamais de secret dans les messages. */
class PrivateKeysTest {
    private fun gen(type: Int) = KeyPair.genKeyPair(JSch(), type, if (type == KeyPair.RSA) 2048 else 256)

    private fun openssh(kp: KeyPair, phrase: String?) =
        ByteArrayOutputStream().also { kp.writeOpenSSHv1PrivateKey(it, phrase?.toByteArray()) }.toByteArray()

    @Test fun opensshKeysInspected() {
        for ((type, name) in listOf(KeyPair.ED25519 to "ssh-ed25519", KeyPair.ECDSA to "ecdsa-sha2-nistp256", KeyPair.RSA to "ssh-rsa")) {
            val kp = gen(type)
            val plain = PrivateKeys.inspect(openssh(kp, null))
            assertEquals(name, plain.type)
            assertEquals(kp.fingerPrint, plain.fingerprint)
            assertTrue(plain.fingerprint.startsWith("SHA256:"))
            assertFalse(plain.encrypted)
            // chiffrée : même empreinte, lue sans la phrase
            val enc = PrivateKeys.inspect(openssh(kp, "phrase"))
            assertEquals(PrivateKeys.Info(name, kp.fingerPrint, true), enc)
            assertEquals("$name ${kp.fingerPrint}", PrivateKeys.describe(enc))
        }
    }

    @Test fun pemKeyAccepted() {
        val kp = gen(KeyPair.RSA)
        val pem = ByteArrayOutputStream().also { kp.writePrivateKey(it) }.toByteArray()
        val info = PrivateKeys.inspect(pem)
        assertEquals(PrivateKeys.Info("ssh-rsa", kp.fingerPrint, false), info)
    }

    @Test fun keyBytesLeftIntact() {
        val key = openssh(gen(KeyPair.ED25519), null)
        val copy = key.copyOf()
        PrivateKeys.inspect(key)
        assertTrue(key.contentEquals(copy)) // l'appelant garde sa clé (à enregistrer), il la remet à zéro lui-même
    }

    @Test fun unreadableKeyRefused() {
        val e = assertThrows(IOException::class.java) { PrivateKeys.inspect("secret-123 pas une clé".toByteArray()) }
        assertEquals("Clé privée illisible", e.message)
        assertThrows(IOException::class.java) { PrivateKeys.inspect(ByteArray(0)) }
    }
}

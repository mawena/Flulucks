package com.mawena.fluluks

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.jcraft.jsch.JSch
import com.jcraft.jsch.KeyPair
import com.jcraft.jsch.XDH
import com.mawena.fluluks.remote.PrivateKeys
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Ed25519 et Curve25519 de JSch avec Bouncy Castle sur ART (absents du fournisseur de sécurité d'Android). */
@RunWith(AndroidJUnit4::class)
class SshCryptoInstrumentedTest {
    @Test fun ed25519KeySignsAndIsInspected() {
        val jsch = PrivateKeys.jsch()
        val kp = KeyPair.genKeyPair(jsch, KeyPair.ED25519)
        val data = "Portail".toByteArray()
        val sig = kp.getSignature(data, "ssh-ed25519")
        val v = kp.getVerifier("ssh-ed25519")
        v.update(data)
        assertTrue(v.verify(sig))
        val enc = ByteArrayOutputStream().also { kp.writeOpenSSHv1PrivateKey(it, "phrase".toByteArray()) }.toByteArray()
        val info = PrivateKeys.inspect(enc)
        assertEquals(PrivateKeys.Info("ssh-ed25519", kp.fingerPrint, true), info)
        val back = KeyPair.load(jsch, enc, null)
        assertTrue(back.decrypt("phrase".toByteArray()))
    }

    @Test fun curve25519Exchange() {
        PrivateKeys.jsch()
        val cls = Class.forName(JSch.getConfig("xdh"))
        val a = cls.getDeclaredConstructor().newInstance() as XDH
        val b = cls.getDeclaredConstructor().newInstance() as XDH
        a.init("X25519", 32)
        b.init("X25519", 32)
        val qa = a.getQ()
        val qb = b.getQ()
        assertTrue(a.validate(qb) && b.validate(qa))
        assertArrayEquals(a.getSecret(qb), b.getSecret(qa))
    }
}

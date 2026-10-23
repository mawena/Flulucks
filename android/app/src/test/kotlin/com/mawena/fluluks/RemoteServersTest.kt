package com.mawena.fluluks

import com.mawena.fluluks.remote.RemoteServers
import com.mawena.fluluks.remote.SecretBox
import com.mawena.fluluks.remote.ServerConfig
import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Serveurs enregistrés et secrets scellés (boîte simulée : octets inversés et décalés). */
class RemoteServersTest {
    @get:Rule val tmp = TemporaryFolder()

    /** Boîte de test : rien de lisible en clair sur le disque, réversible. */
    private object Box : SecretBox {
        override fun seal(plain: ByteArray) = plain.reversedArray().map { (it + 7).toByte() }.toByteArray()
        override fun open(sealed: ByteArray) = sealed.map { (it - 7).toByte() }.toByteArray().reversedArray()
    }

    private fun store() = RemoteServers(tmp.root, Box)
    private fun cfg(name: String = "Maison", remember: Boolean = false) =
        ServerConfig("", "sftp", name, "192.168.0.10", 22, "charles", "/home/charles", remember, null, null)

    /** Tout ce qui est écrit sous le dossier de l'application, d'un bloc. */
    private fun allBytes() = tmp.root.walk().filter { it.isFile }.map { it.readBytes() }.fold(ByteArray(0)) { a, b -> a + b }

    private fun contains(hay: ByteArray, needle: ByteArray) = (0..hay.size - needle.size).any { i ->
        needle.indices.all { hay[i + it] == needle[it] }
    }

    @Test fun serversRoundTrip() {
        val a = store().save(cfg("Maison"), null, null)
        val b = store().save(cfg("Travail").copy(protocol = "ftp", port = 21, user = ""), null, null)
        assertTrue(a.id.isNotEmpty()); assertTrue(a.id != b.id)
        assertEquals(listOf(a, b), store().list())
        val renamed = store().save(a.copy(name = "Maison 2"), null, null)
        assertEquals(a.id, renamed.id)
        assertEquals(listOf("Maison 2", "Travail"), store().list().map { it.name })
    }

    @Test fun invalidConfigRefused() {
        for (bad in listOf(cfg().copy(host = " "), cfg().copy(port = 0), cfg().copy(port = 70000), cfg().copy(protocol = "http"))) {
            try {
                store().save(bad, null, null)
                throw AssertionError("accepté : $bad")
            } catch (e: IllegalArgumentException) {
            }
        }
        // dossier de départ invalide : racine ; nom vide : l'adresse
        val s = store().save(cfg().copy(startPath = "../x", name = ""), null, null)
        assertEquals("/", s.startPath); assertEquals("192.168.0.10", s.name)
    }

    @Test fun passwordNotStoredByDefault() {
        val s = store().save(cfg(remember = false), "motdepasse".toByteArray(), null)
        assertNull(store().secrets(s.id)?.password)
        assertFalse(contains(allBytes(), "motdepasse".toByteArray()))
    }

    @Test fun secretsSealedOnDisk() {
        val s = store().save(cfg(remember = true), "motdepasse".toByteArray(), "phrase".toByteArray())
        assertFalse(contains(allBytes(), "motdepasse".toByteArray()))
        assertFalse(contains(allBytes(), "phrase".toByteArray()))
        val sec = store().secrets(s.id)!!
        assertArrayEquals("motdepasse".toByteArray(), sec.password)
        assertArrayEquals("phrase".toByteArray(), sec.keyPassphrase)
        // décoché ensuite : le mot de passe mémorisé est effacé
        store().save(s.copy(remember = false), null, null)
        assertNull(store().secrets(s.id)?.password)
    }

    @Test fun keepsPasswordWhenNotRetyped() {
        val s = store().save(cfg(remember = true), "motdepasse".toByteArray(), null)
        store().save(s.copy(name = "Autre"), null, null) // fiche modifiée sans retaper le mot de passe
        assertArrayEquals("motdepasse".toByteArray(), store().secrets(s.id)!!.password)
    }

    @Test fun deleteServerRemovesSecrets() {
        val s = store().save(cfg(remember = true), "motdepasse".toByteArray(), null)
        store().delete(s.id)
        assertEquals(emptyList<ServerConfig>(), store().list())
        assertNull(store().secrets(s.id))
        assertEquals(emptyList<File>(), File(tmp.root, "remote-secrets").listFiles()?.toList() ?: emptyList<File>())
    }

    @Test fun hostKeyStoredAndForgotten() {
        val s = store().save(cfg(), null, null)
        store().setHostKey(s.id, "ssh-ed25519 SHA256:abc")
        assertEquals("ssh-ed25519 SHA256:abc", store().get(s.id)!!.hostKey)
        store().setHostKey(s.id, null)
        assertNull(store().get(s.id)!!.hostKey)
    }

    @Test fun damagedFileGivesEmptyList() {
        File(tmp.root, "remote-servers.json").writeText("{pas du json")
        assertEquals(emptyList<ServerConfig>(), store().list())
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.remote.HostKeyUnknown
import com.mawena.fluluks.remote.RemoteServers
import com.mawena.fluluks.remote.RemoteSource
import com.mawena.fluluks.remote.Remotes
import com.mawena.fluluks.remote.SecretBox
import com.mawena.fluluks.remote.ServerConfig
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Source distante au-dessus d'un faux client, et connexions de [Remotes]. */
class RemoteSourceTest {
    @get:Rule val tmp = TemporaryFolder()

    private object Box : SecretBox {
        override fun seal(plain: ByteArray) = plain.copyOf()
        override fun open(sealed: ByteArray) = sealed.copyOf()
    }

    private lateinit var server: File
    private lateinit var client: FakeRemoteClient
    private lateinit var store: RemoteServers
    private val cfg = ServerConfig("s1", "sftp", "Maison", "h", 22, "u", "/", false, null, null)

    @Before fun setup() {
        server = tmp.newFolder("serveur")
        client = FakeRemoteClient(server)
        store = RemoteServers(tmp.newFolder("app"), Box)
        VolumeManager.resetForTests(MemEngine(), delayer = FakeDelayer()) // aucun volume laissé par un autre test
    }

    @After fun teardown() {
        Remotes.resetForTests(null, null)
    }

    private fun src() = RemoteSource(cfg, client)
    private fun put(path: String, text: String) = File(server, path).apply { parentFile!!.mkdirs(); writeText(text) }

    @Test fun identity() {
        val s = src()
        assertEquals("remote:s1", s.id); assertEquals("remote:s1", s.stableKey); assertEquals("Maison", s.label)
        assertNull(s.readOnlyReason)
        assertEquals(-1L to -1L, s.space()) // place inconnue
        assertEquals(FileType.DIR, s.stat("/")!!.type)
    }

    @Test fun hostileServerNamesSkipped() {
        put("ok.txt", "x")
        client.extra["/"] = listOf("..", ".", "a/b", "nul\u0000", "", "bien.txt")
        val names = src().list("/").map { it.name }.toSet()
        assertEquals(setOf("ok.txt", "bien.txt"), names)
        // chemins reconstruits par la source, jamais pris du serveur
        assertTrue(src().list("/").all { it.path == "/${it.name}" })
    }

    @Test fun fileOperations() {
        put("d/a.txt", "contenu")
        val s = src()
        assertEquals(listOf("a.txt"), s.list("/d").map { it.name })
        assertEquals("contenu", s.openRead("/d/a.txt").readBytes().decodeToString())
        assertEquals("tenu", s.openReadAt("/d/a.txt", 3).readBytes().decodeToString())
        // création : nom unique
        val (p, out) = s.createFile("/d", "a.txt")
        out.use { it.write("neuf".toByteArray()) }
        assertEquals("/d/a (1).txt", p)
        assertEquals("/e", s.mkdir("/", "e"))
        assertEquals("/e (1)", s.mkdir("/", "e"))
        assertEquals("/d/b.txt", s.rename("/d/a.txt", "b.txt"))
        assertEquals("/e/b.txt", s.move("/d/b.txt", "/e"))
        assertEquals("contenu", File(server, "e/b.txt").readText())
        // rename vers un nom pris : refusé
        try {
            s.rename("/d/a (1).txt", "../x")
            throw AssertionError("accepté")
        } catch (e: java.io.FileNotFoundException) {
        }
    }

    @Test fun deleteFullDirectory() {
        put("d/a.txt", "1"); put("d/s/b.txt", "2"); put("garde.txt", "3")
        src().delete("/d")
        assertEquals(listOf("garde.txt"), server.list()!!.toList())
    }

    @Test fun connectAddsSourceAndDisconnectWipes() {
        val saved = store.save(cfg.copy(id = ""), null, null)
        var given: ByteArray? = null
        Remotes.resetForTests(store) { _, sec -> given = sec.password; client }
        val pass = "secret".toByteArray()
        val s = Remotes.connect(saved.id, pass)
        assertEquals("remote:${saved.id}", s.id)
        assertArrayEquals("secret".toByteArray(), given)
        assertEquals(listOf(s.id), Remotes.connected().map { it.id })
        assertTrue(Sources.list().any { it.id == s.id })
        Remotes.disconnect(saved.id)
        assertTrue(client.closed)
        assertTrue(given!!.all { it == 0.toByte() }) // copie du mot de passe effacée
        assertEquals(emptyList<RemoteSource>(), Remotes.connected())
    }

    @Test fun hostKeyErrorNotConnected() {
        val saved = store.save(cfg.copy(id = ""), null, null)
        Remotes.resetForTests(store) { _, _ -> throw HostKeyUnknown("ssh-ed25519 SHA256:xyz") }
        try {
            Remotes.connect(saved.id, null)
            throw AssertionError("accepté")
        } catch (e: HostKeyUnknown) {
            assertEquals("ssh-ed25519 SHA256:xyz", e.fingerprint)
        }
        assertFalse(Remotes.connected().any())
        Remotes.trustHostKey(saved.id, "ssh-ed25519 SHA256:xyz")
        assertEquals("ssh-ed25519 SHA256:xyz", store.get(saved.id)!!.hostKey)
    }

    @Test fun contentUriAndNativeGoThroughStreamProvider() {
        put("v.mkv", "vidéo")
        val n = src().openNative("/v.mkv")
        assertTrue(n is com.mawena.fluluks.files.NativeIn.Proxy)
        assertEquals(File(server, "v.mkv").length(), (n as com.mawena.fluluks.files.NativeIn.Proxy).size)
        val (p, out) = src().createNative("/", "a.zip")
        assertEquals("/a.zip", p)
        assertTrue(out is com.mawena.fluluks.files.NativeOut.Pipe)
        (out as com.mawena.fluluks.files.NativeOut.Pipe).sink.close()
    }

    @Test fun privateKeyImportedSealedThenForgotten() {
        val saved = store.save(cfg.copy(id = ""), null, null)
        Remotes.resetForTests(store, null)
        val kp = com.jcraft.jsch.KeyPair.genKeyPair(com.jcraft.jsch.JSch(), com.jcraft.jsch.KeyPair.ED25519)
        val key = java.io.ByteArrayOutputStream().also { kp.writeOpenSSHv1PrivateKey(it, null) }.toByteArray()
        val keep = key.copyOf()
        val info = Remotes.importPrivateKey(saved.id, key)
        assertEquals("ssh-ed25519 ${kp.fingerPrint}", info)
        assertEquals(info, store.get(saved.id)!!.keyInfo)
        assertArrayEquals(keep, store.secrets(saved.id)!!.privateKey)
        assertTrue(key.all { it == 0.toByte() }) // copie reçue remise à zéro
        // fichier qui n'est pas une clé : refusé, rien de changé
        try {
            Remotes.importPrivateKey(saved.id, "pas une clé".toByteArray())
            throw AssertionError("accepté")
        } catch (e: java.io.IOException) {
            assertEquals("Clé privée illisible", e.message)
        }
        assertEquals(info, store.get(saved.id)!!.keyInfo)
        Remotes.forgetPrivateKey(saved.id)
        assertNull(store.get(saved.id)!!.keyInfo)
        assertNull(store.secrets(saved.id)?.privateKey)
    }

    /** Relecture finale (I1) : les messages des clients distants arrivent tels quels, pas « L'opération a échoué ». */
    @Test fun serverMessagesReachUser() {
        com.mawena.fluluks.files.Files.setResolverForTests { if (it == "remote:s1") src() else null }
        try {
            client.listError = java.io.IOException("Serveur occupé : trop de transferts en cours")
            val e1 = runCatching { com.mawena.fluluks.files.Files.list("remote:s1", "/", false) }.exceptionOrNull()
            assertEquals("Serveur occupé : trop de transferts en cours", e1?.message)
            put("a.txt", "x")
            client.readError = java.io.IOException("Connexion perdue")
            val e2 = runCatching { src().openRead("/a.txt").read() }.exceptionOrNull()!!
            assertEquals("Connexion perdue", com.mawena.fluluks.files.Files.toFilesException(e2, "openDocument").message)
        } finally {
            com.mawena.fluluks.files.Files.setResolverForTests(null)
        }
        val saved = store.save(cfg.copy(id = ""), null, null)
        Remotes.resetForTests(store) { _, _ -> throw java.io.IOException("Serveur injoignable") }
        val e3 = runCatching { Remotes.connect(saved.id, null) }.exceptionOrNull()!!
        assertEquals("Serveur injoignable", com.mawena.fluluks.files.Files.toFilesException(e3, "openDocument").message)
        val e4 = runCatching { Remotes.importPrivateKey(saved.id, "pas une clé".toByteArray()) }.exceptionOrNull()!!
        assertEquals("Clé privée illisible", com.mawena.fluluks.files.Files.toFilesException(e4, "openDocument").message)
    }
}

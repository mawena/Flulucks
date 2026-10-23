package com.mawena.fluluks

import com.jcraft.jsch.JSch
import com.jcraft.jsch.KeyPair as JKeyPair
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.remote.AuthFailed
import com.mawena.fluluks.remote.HostKeyChanged
import com.mawena.fluluks.remote.HostKeyUnknown
import com.mawena.fluluks.remote.Secrets
import com.mawena.fluluks.remote.ServerConfig
import com.mawena.fluluks.remote.SftpClient
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.Paths
import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.PublicKey
import java.util.Base64
import java.util.Random
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.apache.sshd.common.channel.Channel
import org.apache.sshd.common.channel.ChannelListener
import org.apache.sshd.common.config.keys.PublicKeyEntry
import org.apache.sshd.common.file.virtualfs.VirtualFileSystemFactory
import org.apache.sshd.common.keyprovider.KeyPairProvider
import org.apache.sshd.server.SshServer
import org.apache.sshd.server.auth.password.PasswordAuthenticator
import org.apache.sshd.server.auth.pubkey.PublickeyAuthenticator
import org.apache.sshd.sftp.server.SftpSubsystemFactory
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Client SFTP contre un serveur MINA SSHD embarqué, sur 127.0.0.1 seulement, port libre, fichiers enfermés dans un
 * dossier temporaire ; clés du serveur et du client générées par le test.
 */
class SftpClientTest {
    @get:Rule val tmp = TemporaryFolder()

    private lateinit var root: File
    private lateinit var sshd: SshServer
    private lateinit var hostKeys: KeyPair

    /** Tentatives d'authentification vues par le serveur (mot de passe, clé, clavier interactif). */
    private val authAttempts = AtomicInteger()
    private val openChannels = AtomicInteger()
    private val maxChannels = AtomicInteger()

    /** Clé publique (base64 du blob SSH) acceptée par le serveur, null : aucune. */
    @Volatile private var allowedKey: String? = null

    @Before fun setup() {
        root = tmp.newFolder("serveur")
        hostKeys = ecKeys()
        sshd = start(hostKeys, 0)
    }

    @After fun teardown() {
        sshd.stop(true)
    }

    private fun ecKeys(): KeyPair = KeyPairGenerator.getInstance("EC").apply { initialize(256) }.generateKeyPair()

    private fun start(keys: KeyPair, port: Int): SshServer = SshServer.setUpDefaultServer().apply {
        host = "127.0.0.1"
        this.port = port
        keyPairProvider = KeyPairProvider.wrap(keys)
        passwordAuthenticator = PasswordAuthenticator { u, p, _ ->
            authAttempts.incrementAndGet()
            u == USER && p == PASSWORD
        }
        publickeyAuthenticator = PublickeyAuthenticator { u, k, _ ->
            authAttempts.incrementAndGet()
            u == USER && allowedKey != null && blob64(k) == allowedKey
        }
        subsystemFactories = listOf(SftpSubsystemFactory())
        fileSystemFactory = VirtualFileSystemFactory(root.toPath())
        addChannelListener(object : ChannelListener {
            override fun channelOpenSuccess(channel: Channel) {
                val n = openChannels.incrementAndGet()
                maxChannels.accumulateAndGet(n, ::maxOf)
            }

            override fun channelClosed(channel: Channel, reason: Throwable?) {
                openChannels.decrementAndGet()
            }
        })
        start()
    }

    private fun blob64(k: PublicKey) = PublicKeyEntry.toString(k).split(" ")[1]

    /** Empreinte attendue, calculée ici sans le code testé : « type SHA256:base64 sans = ». */
    private fun expected(k: PublicKey): String {
        val (type, b64) = PublicKeyEntry.toString(k).split(" ")
        val sha = MessageDigest.getInstance("SHA-256").digest(Base64.getDecoder().decode(b64))
        return "$type SHA256:" + Base64.getEncoder().withoutPadding().encodeToString(sha)
    }

    private fun cfg(hostKey: String? = expected(hostKeys.public), user: String = USER) =
        ServerConfig("s1", "sftp", "Test", "127.0.0.1", sshd.port, user, "/", false, hostKey, null)

    private fun pw(p: String? = PASSWORD) = Secrets(p?.toByteArray(), null, null)

    private fun put(path: String, bytes: ByteArray) =
        File(root, path).apply { parentFile!!.mkdirs(); writeBytes(bytes) }

    /** Clé privée OpenSSH générée par JSch ; [phrase] : chiffrée. Le serveur accepte sa partie publique. */
    private fun clientKey(type: Int, phrase: String?): ByteArray {
        val kp = JKeyPair.genKeyPair(JSch(), type, if (type == JKeyPair.RSA) 2048 else 256)
        allowedKey = Base64.getEncoder().encodeToString(kp.publicKeyBlob)
        val out = ByteArrayOutputStream()
        kp.writeOpenSSHv1PrivateKey(out, phrase?.toByteArray())
        return out.toByteArray()
    }

    private fun waitFor(what: String, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + 5_000
        while (!cond()) {
            if (System.currentTimeMillis() > end) throw AssertionError(what)
            Thread.sleep(20)
        }
    }

    @Test fun unknownHostKeyAskedOnce() {
        put("a.txt", "x".toByteArray())
        val e = assertThrows(HostKeyUnknown::class.java) { SftpClient.connect(cfg(hostKey = null), pw()) }
        assertEquals(expected(hostKeys.public), e.fingerprint)
        assertTrue(e.fingerprint.startsWith("ecdsa-sha2-nistp256 SHA256:"))
        assertFalse(e.fingerprint.endsWith("="))
        assertEquals(0, authAttempts.get()) // connexion non établie : le mot de passe n'est pas parti
        // empreinte acceptée : connexion sans nouvelle question
        SftpClient.connect(cfg(hostKey = e.fingerprint), pw()).use { c ->
            assertEquals(listOf("a.txt"), c.list("/").map { it.name })
        }
    }

    @Test fun changedHostKeyRefused() {
        val trusted = expected(hostKeys.public)
        SftpClient.connect(cfg(hostKey = trusted), pw()).close()
        // même port, autre clé de serveur
        val port = sshd.port
        sshd.stop(true)
        val other = ecKeys()
        sshd = start(other, port)
        assertEquals(port, sshd.port)
        authAttempts.set(0)
        val e = assertThrows(HostKeyChanged::class.java) { SftpClient.connect(cfg(hostKey = trusted), pw()) }
        assertEquals(trusted, e.old)
        assertEquals(expected(other.public), e.new)
        assertEquals(0, authAttempts.get())
        assertTrue(e.message!!.contains("clé du serveur a changé"))
    }

    @Test fun passwordAuth() {
        put("d/b.txt", "bonjour".toByteArray())
        SftpClient.connect(cfg(), pw()).use { c ->
            val d = c.stat("/d")!!
            assertEquals(FileType.DIR, d.type)
            val f = c.list("/d").single()
            assertEquals("b.txt", f.name)
            assertEquals(FileType.FILE, f.type)
            assertEquals(7L, f.size)
            assertTrue(f.mtime > 0)
            assertNull(c.stat("/absent"))
        }
        // sans mot de passe : à demander
        val e = assertThrows(AuthFailed::class.java) { SftpClient.connect(cfg(), pw(null)) }
        assertTrue(e.missing)
        assertEquals("Mot de passe requis", e.message)
    }

    @Test fun ed25519KeyAuth() {
        put("k.txt", "clé".toByteArray())
        val key = clientKey(JKeyPair.ED25519, null)
        // aucun mot de passe : la clé seule suffit
        SftpClient.connect(cfg(), Secrets(null, null, key)).use { c ->
            assertEquals(listOf("k.txt"), c.list("/").map { it.name })
        }
        // clé que le serveur ne connaît pas : refusée
        allowedKey = "autre"
        val e = assertThrows(AuthFailed::class.java) { SftpClient.connect(cfg(), Secrets(null, null, key)) }
        assertFalse(e.missing)
    }

    @Test fun ecdsaAndRsaKeysAuth() {
        for (type in listOf(JKeyPair.ECDSA, JKeyPair.RSA)) {
            val key = clientKey(type, null)
            SftpClient.connect(cfg(), Secrets(null, null, key)).use { c -> c.list("/") }
        }
    }

    @Test fun encryptedKeyNeedsPassphrase() {
        val key = clientKey(JKeyPair.ED25519, "phrase de clé")
        // ni phrase ni mot de passe : à demander
        val e = assertThrows(AuthFailed::class.java) { SftpClient.connect(cfg(), Secrets(null, null, key)) }
        assertTrue(e.missing)
        // phrase mémorisée
        SftpClient.connect(cfg(), Secrets(null, "phrase de clé".toByteArray(), key)).use { it.list("/") }
        // le mot de passe saisi à la connexion sert de phrase
        SftpClient.connect(cfg(), Secrets("phrase de clé".toByteArray(), null, key)).use { it.list("/") }
        // mauvaise phrase : refusée
        val bad = assertThrows(AuthFailed::class.java) {
            SftpClient.connect(cfg(), Secrets(null, "mauvaise".toByteArray(), key))
        }
        assertFalse(bad.missing)
    }

    @Test fun readAtOffsets() {
        val data = ByteArray(300_000).also { Random(7).nextBytes(it) }
        put("v/film.bin", data)
        SftpClient.connect(cfg(), pw()).use { c ->
            for (off in listOf(0L, 1L, 65_535L, 131_079L, data.size - 1L, data.size.toLong())) {
                val got = c.openReadAt("/v/film.bin", off).use(InputStream::readBytes)
                assertArrayEquals("décalage $off", data.copyOfRange(off.toInt(), data.size), got)
            }
            // lecture partielle puis fermeture : le canal est rendu
            c.openReadAt("/v/film.bin", 10).use { assertEquals(data[10].toInt() and 0xff, it.read()) }
            assertThrows(FileNotFoundException::class.java) { c.openReadAt("/v/absent.bin", 0).close() }
            assertEquals(data.size.toLong(), c.stat("/v/film.bin")!!.size)
        }
        assertTrue(maxChannels.get() <= SftpClient.MAX_CHANNELS)
    }

    @Test fun uploadRenameDelete() {
        SftpClient.connect(cfg(), pw()).use { c ->
            c.create("/", "a.bin").use { it.write(ByteArray(100_000) { i -> i.toByte() }) }
            assertEquals(100_000L, File(root, "a.bin").length())
            // même nom : remplacé, pas allongé
            c.create("/", "a.bin").use { it.write("court".toByteArray()) }
            assertEquals("court", File(root, "a.bin").readText())
            c.mkdir("/", "dossier")
            assertTrue(File(root, "dossier").isDirectory)
            c.rename("/a.bin", "/dossier/b.bin")
            assertFalse(File(root, "a.bin").exists())
            assertEquals("court", File(root, "dossier/b.bin").readText())
            assertEquals(listOf("b.bin"), c.list("/dossier").map { it.name })
            // dossier non vide : refusé, rien de perdu
            assertThrows(IOException::class.java) { c.delete("/dossier", dir = true) }
            assertTrue(File(root, "dossier/b.bin").exists())
            c.delete("/dossier/b.bin", dir = false)
            c.delete("/dossier", dir = true)
            assertNull(c.stat("/dossier"))
            assertFalse(File(root, "dossier").exists())
        }
    }

    @Test fun wildcardNamesTakenLiterally() {
        put("a*", "étoile".toByteArray())
        put("ab", "1".toByteArray())
        put("ac", "2".toByteArray())
        put("x\\y", "barre".toByteArray())
        SftpClient.connect(cfg(), pw()).use { c ->
            assertNull(c.stat("/a?")) // pas de motif : « a? » n'existe pas
            assertEquals("étoile", c.openReadAt("/a*", 0).use { it.readBytes().decodeToString() })
            assertEquals("barre", c.openReadAt("/x\\y", 0).use { it.readBytes().decodeToString() })
            c.delete("/a*", dir = false) // ni « ab » ni « ac »
            assertEquals(setOf("ab", "ac", "x\\y"), c.list("/").map { it.name }.toSet())
            c.rename("/x\\y", "/z*")
            assertEquals("barre", File(root, "z*").readText())
            c.mkdir("/", "d?")
            assertTrue(File(root, "d?").isDirectory)
            c.create("/d?", "f*").use { it.write(1) }
            assertEquals(listOf("f*"), c.list("/d?").map { it.name })
        }
    }

    @Test fun symlinkListedNotFollowed() {
        put("cible/dedans.txt", "garde".toByteArray())
        Files.createSymbolicLink(File(root, "lien").toPath(), Paths.get("cible"))
        SftpClient.connect(cfg(), pw()).use { c ->
            val byName = c.list("/").associateBy { it.name }
            assertEquals(FileType.SYMLINK, byName.getValue("lien").type)
            assertEquals(FileType.DIR, byName.getValue("cible").type)
            assertEquals(FileType.SYMLINK, c.stat("/lien")!!.type)
            // supprimer le lien ne touche pas la cible
            c.delete("/lien", dir = false)
            assertFalse(Files.exists(File(root, "lien").toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS))
            assertEquals("garde", File(root, "cible/dedans.txt").readText())
        }
    }

    @Test fun badCredentialsMessage() {
        val e = assertThrows(AuthFailed::class.java) { SftpClient.connect(cfg(), pw("faux")) }
        assertFalse(e.missing)
        assertEquals("Identifiant ou mot de passe refusé", e.message)
        val u = assertThrows(AuthFailed::class.java) { SftpClient.connect(cfg(user = "inconnu"), pw()) }
        assertFalse(u.missing)
    }

    @Test fun channelsNeverExceedThree() {
        put("f.bin", ByteArray(200_000) { it.toByte() })
        SftpClient.connect(cfg(), pw(), waitMs = 10_000).use { c ->
            val streams = (1..3).map { c.openReadAt("/f.bin", it.toLong()) }
            waitFor("3 canaux ouverts") { openChannels.get() == 3 }
            // un quatrième usage attend qu'un canal soit rendu
            val pool = Executors.newSingleThreadExecutor()
            val later = pool.submit(Callable { c.list("/").map { it.name } })
            Thread.sleep(300)
            assertFalse(later.isDone)
            assertEquals(3, openChannels.get())
            streams[0].close()
            assertEquals(listOf("f.bin"), later.get(5, TimeUnit.SECONDS))
            streams.drop(1).forEach(InputStream::close)
            pool.shutdown()
            // beaucoup d'opérations en parallèle : jamais plus de 3 canaux
            val many = Executors.newFixedThreadPool(8)
            val jobs = (1..40).map { i ->
                many.submit(Callable {
                    if (i % 2 == 0) c.stat("/f.bin")!!.size else c.openReadAt("/f.bin", i * 1000L).use { it.read().toLong() }
                })
            }
            jobs.forEach { assertNotNull(it.get(30, TimeUnit.SECONDS)) }
            many.shutdown()
        }
        assertTrue("au plus 3 canaux : ${maxChannels.get()}", maxChannels.get() <= 3)
        assertEquals(3, maxChannels.get())
        // délai d'attente dépassé : message clair
        SftpClient.connect(cfg(), pw(), waitMs = 200).use { c ->
            val streams = (1..3).map { c.openReadAt("/f.bin", 0) }
            val e = assertThrows(IOException::class.java) { c.list("/") }
            assertTrue(e.message!!.contains("occupé"))
            streams.forEach(InputStream::close)
            c.list("/")
        }
    }

    @Test fun secretsNotInMessages() {
        val secret = "MotDePasseUltraSecret-42"
        val phrase = "PhraseDeCleUltraSecrete-17"
        val key = clientKey(JKeyPair.ED25519, phrase)
        val failures = ArrayList<Throwable>()
        fun collect(block: () -> Unit) {
            try {
                block()
            } catch (t: Throwable) {
                failures += t
            }
        }
        collect { SftpClient.connect(cfg(), pw(secret)) }
        collect { SftpClient.connect(cfg(hostKey = null), pw(secret)) }
        collect { SftpClient.connect(cfg(hostKey = "ssh-ed25519 SHA256:autre"), pw(secret)) }
        collect { SftpClient.connect(cfg(), Secrets(secret.toByteArray(), "x$phrase".toByteArray(), key)) }
        collect { SftpClient.connect(cfg(), Secrets(phrase.reversed().toByteArray(), null, key)) }
        collect { SftpClient.connect(cfg().copy(port = 1), pw(secret)) }
        collect { SftpClient.connect(cfg(), Secrets(null, null, "pas une clé $secret".toByteArray())) }
        collect { SftpClient.connect(cfg(), pw()).use { it.openReadAt("/$secret", 0) } }
        assertEquals(8, failures.size)
        for (t in failures) {
            var c: Throwable? = t
            while (c != null) {
                val text = "$c ${c.message}"
                assertFalse(text, text.contains(secret) && !text.contains("/$secret"))
                assertFalse(text, text.contains(phrase))
                c = c.cause
            }
        }
    }

    private companion object {
        const val USER = "alice"
        const val PASSWORD = "s3cret"
    }
}

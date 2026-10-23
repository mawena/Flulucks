package com.mawena.fluluks

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.SeekableReader
import com.mawena.fluluks.remote.AuthFailed
import com.mawena.fluluks.remote.FtpClient
import com.mawena.fluluks.remote.RemoteSource
import com.mawena.fluluks.remote.Secrets
import com.mawena.fluluks.remote.ServerConfig
import java.io.ByteArrayInputStream
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.util.Collections
import java.util.Random
import kotlin.concurrent.thread
import org.apache.ftpserver.FtpServer
import org.apache.ftpserver.FtpServerFactory
import org.apache.ftpserver.command.Command
import org.apache.ftpserver.command.CommandFactoryFactory
import org.apache.ftpserver.ftplet.DefaultFtpReply
import org.apache.ftpserver.ftplet.FtpRequest
import org.apache.ftpserver.impl.DefaultFtpServer
import org.apache.ftpserver.impl.FtpIoSession
import org.apache.ftpserver.impl.FtpServerContext
import org.apache.ftpserver.listener.ListenerFactory
import org.apache.ftpserver.usermanager.ClearTextPasswordEncryptor
import org.apache.ftpserver.usermanager.PropertiesUserManagerFactory
import org.apache.ftpserver.usermanager.impl.BaseUser
import org.apache.ftpserver.usermanager.impl.WritePermission
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Client FTP contre un Apache FtpServer embarqué sur 127.0.0.1 (port libre), l'utilisateur `u` enfermé dans
 * [root], l'anonyme dans [anon]. Le « vieux serveur » ISO-8859-1 remplace `FEAT` (ni `UTF8` ni `MLST`) et `LIST`
 * (octets Latin-1 bruts) : FtpServer annonce toujours UTF-8 et n'écrit ses listes qu'en UTF-8.
 */
class FtpClientTest {
    @get:Rule val tmp = TemporaryFolder()

    private lateinit var root: File
    private lateinit var anon: File
    private var server: FtpServer? = null
    private var port = 0
    private val clients = ArrayList<FtpClient>()

    @Before fun setup() {
        root = tmp.newFolder("racine")
        anon = tmp.newFolder("anonyme")
    }

    @After fun teardown() {
        clients.forEach { runCatching { it.close() } }
        server?.let { if (!it.isStopped) it.stop() }
    }

    private fun start(latin1: Boolean = false) {
        val users = PropertiesUserManagerFactory().apply { passwordEncryptor = ClearTextPasswordEncryptor() }
            .createUserManager()
        users.save(BaseUser().apply { name = "u"; password = "secret"; homeDirectory = root.path; authorities = listOf(WritePermission()) })
        users.save(BaseUser().apply { name = "anonymous"; homeDirectory = anon.path })
        val f = FtpServerFactory()
        f.userManager = users
        f.addListener("default", ListenerFactory().apply { serverAddress = "127.0.0.1"; port = 0 }.createListener())
        if (latin1) {
            f.commandFactory = CommandFactoryFactory().apply {
                addCommand("FEAT", OldFeat)
                addCommand("LIST", Latin1List)
            }.createCommandFactory()
        }
        val s = f.createServer()
        s.start()
        server = s
        port = (s as DefaultFtpServer).getListener("default").port
        assertTrue(port > 0)
    }

    private fun stats() = (server as DefaultFtpServer).serverContext.ftpStatistics

    private fun cfg(user: String = "u") = ServerConfig("s1", "ftp", "Maison", "127.0.0.1", port, user, "/", false, null, null)

    private fun client(user: String = "u", password: String? = "secret", timeoutMs: Int = 30_000): FtpClient =
        FtpClient(cfg(user), Secrets(password?.toByteArray(), null, null), timeoutMs).also { clients += it }

    private fun bytes(n: Int, seed: Long = 7) = ByteArray(n).also { Random(seed).nextBytes(it) }

    private fun put(path: String, data: ByteArray, under: File = root) =
        File(under, path).apply { parentFile!!.mkdirs(); writeBytes(data) }

    @Test fun listsWithAccents() {
        put("été.txt", "bonjour".toByteArray())
        put("Dossier é/naïve ü.bin", bytes(10))
        start()
        val c = client()
        val top = c.list("/").associateBy { it.name }
        assertEquals(setOf("été.txt", "Dossier é"), top.keys)
        assertEquals(FileType.FILE, top["été.txt"]!!.type)
        assertEquals(7L, top["été.txt"]!!.size)
        assertTrue(top["été.txt"]!!.mtime > 0)
        assertEquals(FileType.DIR, top["Dossier é"]!!.type)
        assertEquals(listOf("naïve ü.bin"), c.list("/Dossier é").map { it.name })
        assertEquals(10L, c.stat("/Dossier é/naïve ü.bin")!!.size)
        assertNull(c.stat("/absent.txt"))
        assertEquals("bonjour", c.openReadAt("/été.txt", 0).use { String(it.readBytes()) })
        assertEquals("jour", c.openReadAt("/été.txt", 3).use { String(it.readBytes()) })
    }

    @Test fun latin1ListingDecoded() {
        start(latin1 = true)
        val c = client()
        val l = c.list("/").associateBy { it.name }
        assertEquals(setOf("café.txt", "résumés"), l.keys)
        assertEquals(FileType.FILE, l["café.txt"]!!.type)
        assertEquals(4L, l["café.txt"]!!.size)
        assertEquals(FileType.DIR, l["résumés"]!!.type)
        assertEquals(4L, c.stat("/café.txt")!!.size) // sans MLST : par la liste du dossier parent
    }

    @Test fun readAtOffsets() {
        val data = bytes(300 * 1024)
        put("a/gros.bin", data)
        start()
        val c = client()
        for (off in listOf(0, 1, 4096, 150_000, data.size - 1, data.size)) {
            val got = c.openReadAt("/a/gros.bin", off.toLong()).use { it.readBytes() }
            assertArrayEquals("décalage $off", data.copyOfRange(off, data.size), got)
        }
        assertEquals(0, c.busy)
        assertEquals(1, c.live) // une seule connexion, rendue et reprise à chaque lecture

        // accès aléatoire (vidéo qu'on avance, ZIP lu par la fin) : octets justes, connexions rendues
        val r = SeekableReader(data.size.toLong()) { c.openReadAt("/a/gros.bin", it) }
        val rnd = Random(3)
        val buf = ByteArray(5000)
        repeat(40) {
            val off = rnd.nextInt(data.size)
            val n = r.read(off.toLong(), buf, buf.size)
            assertArrayEquals(data.copyOfRange(off, minOf(off + buf.size, data.size)), buf.copyOf(n))
        }
        r.close()
        assertEquals(0, c.busy)
        assertTrue(c.peak <= 3)
        try {
            c.openReadAt("/absent.bin", 0).close()
            fail()
        } catch (e: FileNotFoundException) {
            assertEquals("Fichier introuvable", e.message)
        }
        assertEquals(0, c.busy)
    }

    @Test fun uploadThenRename() {
        start()
        val c = client()
        val data = bytes(200_000)
        c.create("/", "envoi é.bin").use { it.write(data) }
        assertArrayEquals(data, File(root, "envoi é.bin").readBytes())
        assertEquals(0, c.busy)
        c.mkdir("/", "sous")
        c.rename("/envoi é.bin", "/sous/renommé.bin")
        assertNull(c.stat("/envoi é.bin"))
        assertEquals(200_000L, c.stat("/sous/renommé.bin")!!.size)
        assertArrayEquals(data, File(root, "sous/renommé.bin").readBytes())
        // remplace un fichier existant
        c.create("/sous", "renommé.bin").use { it.write("court".toByteArray()) }
        assertEquals("court", File(root, "sous/renommé.bin").readText())
        try {
            c.rename("/absent", "/ailleurs")
            fail()
        } catch (e: FileNotFoundException) {
            assertEquals("Renommage impossible", e.message)
        }
        assertEquals(0, c.busy)
    }

    @Test fun mkdirAndRecursiveDelete() {
        start()
        val c = client()
        c.mkdir("/", "d")
        c.mkdir("/d", "e")
        c.create("/d/e", "f.txt").use { it.write("x".toByteArray()) }
        assertTrue(File(root, "d/e/f.txt").isFile)
        try {
            c.mkdir("/", "d") // existe déjà
            fail()
        } catch (e: FileNotFoundException) {
            assertEquals("Création impossible", e.message)
        }
        try {
            c.delete("/d", dir = true) // pas vide
            fail()
        } catch (e: FileNotFoundException) {
            assertEquals("Suppression impossible", e.message)
        }
        // fichier, puis dossier vide, par le client
        c.create("/", "seul.txt").use { it.write("y".toByteArray()) }
        c.delete("/seul.txt", dir = false)
        assertFalse(File(root, "seul.txt").exists())
        c.mkdir("/", "vide")
        c.delete("/vide", dir = true)
        assertFalse(File(root, "vide").exists())
        // suppression récursive : par la source
        RemoteSource(cfg(), c).delete("/d")
        assertFalse(File(root, "d").exists())
        assertEquals(0, c.busy)
    }

    @Test fun anonymousLogin() {
        put("public.txt", "pour tous".toByteArray(), under = anon)
        start()
        val c = client(user = "", password = null)
        assertEquals(listOf("public.txt"), c.list("/").map { it.name })
        assertEquals("pour tous", c.openReadAt("/public.txt", 0).use { String(it.readBytes()) })
        assertEquals(1, stats().totalAnonymousLoginNumber)
    }

    @Test fun badPasswordMessage() {
        start()
        try {
            client(password = "mauvais-mdp-42")
            fail()
        } catch (e: AuthFailed) {
            assertFalse(e.missing)
            assertEquals("Identifiant ou mot de passe refusé", e.message)
            assertFalse(e.toString().contains("mauvais-mdp-42"))
        }
        for (pw in listOf(null, "")) {
            try {
                client(password = pw)
                fail()
            } catch (e: AuthFailed) {
                assertTrue(e.missing) // à demander
            }
        }
        assertEquals(1, stats().totalConnectionNumber) // mot de passe manquant : aucune connexion
    }

    @Test fun unreachableServer() {
        start()
        val p = port
        server!!.stop()
        try {
            FtpClient(cfg().copy(port = p), Secrets("secret".toByteArray(), null, null))
            fail()
        } catch (e: IOException) {
            assertEquals("Serveur injoignable", e.message)
        }
    }

    @Test fun connectionDropMidRead() {
        val data = bytes(32 * 1024 * 1024)
        put("film.bin", data)
        start()
        val c = client(timeoutMs = 5_000)
        val s = c.openReadAt("/film.bin", 0)
        val buf = ByteArray(64 * 1024)
        var total = s.read(buf).toLong()
        assertTrue(total > 0)
        assertEquals(1, c.busy)
        val stopper = thread { server!!.stop() }
        try {
            while (true) {
                val n = s.read(buf)
                if (n < 0) break
                total += n
            }
            fail("lecture finie sans erreur ($total octets)")
        } catch (e: IOException) {
            assertEquals("Connexion perdue", e.message)
        }
        s.close() // sans erreur après la perte
        stopper.join(10_000)
        assertEquals(0, c.busy) // connexion rendue…
        assertEquals(0, c.live) // … et jetée : pas de fuite
    }

    @Test fun poolNeverExceedsThree() {
        for (i in 1..6) put("f$i.bin", bytes(50_000, seed = i.toLong()))
        start()
        val c = client()
        val streams = (1..3).map { c.openReadAt("/f$it.bin", 0) }
        assertEquals(3, c.busy)
        var listed: List<String>? = null
        val t = thread { listed = c.list("/").map { it.name } }
        t.join(400)
        assertTrue("la 4e opération attend une connexion libre", t.isAlive)
        assertEquals(3, stats().currentConnectionNumber)
        // une lecture finie rend sa connexion : la liste la reprend
        assertArrayEquals(bytes(50_000, seed = 1), streams[0].use { it.readBytes() })
        t.join(10_000)
        assertFalse(t.isAlive)
        assertEquals(6, listed!!.size)
        streams.drop(1).forEach { it.close() } // abandonnées en cours : jetées
        assertEquals(0, c.busy)
        assertEquals(3, stats().totalConnectionNumber)

        // à plusieurs fils : jamais plus de trois connexions
        val errors = Collections.synchronizedList(ArrayList<Throwable>())
        val workers = (1..8).map { w ->
            thread {
                try {
                    repeat(5) {
                        val i = (w + it) % 6 + 1
                        assertArrayEquals(bytes(50_000, seed = i.toLong()), c.openReadAt("/f$i.bin", 0).use { s -> s.readBytes() })
                        assertNotNull(c.stat("/f$i.bin"))
                    }
                } catch (e: Throwable) {
                    errors += e
                }
            }
        }
        workers.forEach { it.join(60_000) }
        assertEquals(emptyList<Throwable>(), errors)
        assertTrue("pic de ${c.peak} connexions", c.peak <= 3)
        assertTrue(c.live <= 3)
        assertEquals(0, c.busy)
    }

    /** `FEAT` d'un vieux serveur : ni `UTF8` ni `MLST`. */
    private object OldFeat : Command {
        override fun execute(session: FtpIoSession, context: FtpServerContext, request: FtpRequest) {
            session.resetState()
            session.write(DefaultFtpReply(211, "Extensions supported\n SIZE\n MDTM\n REST STREAM\nEnd"))
        }
    }

    /** `LIST` d'un vieux serveur : noms en ISO-8859-1. */
    private object Latin1List : Command {
        private const val LISTING = "-rw-r--r--   1 user group        4 Jan 01  2024 café.txt\r\n" +
            "drwxr-xr-x   1 user group        0 Jan 01  2024 résumés\r\n"

        override fun execute(session: FtpIoSession, context: FtpServerContext, request: FtpRequest) {
            session.resetState()
            session.write(DefaultFtpReply(150, "Liste"))
            val dc = try {
                session.dataConnection.openConnection()
            } catch (e: Exception) {
                session.write(DefaultFtpReply(425, "Pas de connexion de données"))
                return
            }
            dc.transferToClient(session.ftpletSession, ByteArrayInputStream(LISTING.toByteArray(Charsets.ISO_8859_1)))
            session.dataConnection.closeDataConnection()
            session.write(DefaultFtpReply(226, "Fini"))
        }
    }
}

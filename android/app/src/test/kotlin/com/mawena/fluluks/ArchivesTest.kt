package com.mawena.fluluks

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.files.Archives
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.NativeIn
import com.mawena.fluluks.files.NativeOut
import com.mawena.fluluks.files.TransferState
import com.mawena.fluluks.files.Transfers
import com.mawena.fluluks.volumes.VolumeManager
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.nio.file.Files as NioFiles
import java.util.Base64
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Compresser et extraire entre une source locale (dossier temporaire) et un disque (moteur en mémoire). Le moteur
 * d'archives est simulé : il rend les entrées choisies par le test et note ce que la compression lui confie.
 */
class ArchivesTest {
    @get:Rule val tmp = TemporaryFolder()

    /**
     * Entrée d'archive simulée : nom brut, type (f, d, l, h, o), taille annoncée, données réellement rendues ;
     * [stream] : unique entrée d'un .gz ou d'un .xz seul (« raw » du moteur).
     */
    private data class Arc(
        val raw: ByteArray, val type: String, val size: Long, val data: ByteArray = ByteArray(0), val encrypted: Boolean = false,
        val stream: Boolean = false,
    )

    private fun file(name: String, data: ByteArray) = Arc(name.toByteArray(), "f", data.size.toLong(), data)
    private fun dir(name: String) = Arc(name.toByteArray(), "d", 0)
    private fun other(name: String, type: String) = Arc(name.toByteArray(), type, 0)

    /** Moteur en mémoire + archives simulées ; les descripteurs « ouverts » par Archives sont suivis jusqu'à leur fermeture. */
    private class Engine : MemEngine() {
        var entries: List<Arc> = emptyList()
        /** Code rendu par arcOpenFd/arcOpenFile (0 : ouverture réussie). */
        var openCode = 0
        var onArcRead: (Int) -> Unit = {}
        var onPwrite: (Int) -> Unit = {}
        var onPread: (Int) -> Unit = {}
        var onNext: (Int) -> Unit = {}
        var onArcwWrite: (Int) -> Unit = {}
        var arcwWrites = 0
        /** Tubes donnés à Archives (NativeOut.Pipe) : descripteur → côté écriture. */
        private val pipes = HashMap<Int, PipedOutputStream>()
        var arcReads = 0
        var nexts = 0
        /** Allocation simulée : chaque élément du volume occupe au moins [cluster] octets de la place libre. */
        var cluster = 0L
        var pwrites = 0
        var preads = 0
        val fds = HashMap<Int, File>()
        val closedFds = HashSet<Int>()
        private var nextFd = 100
        private var nextArc = 5000L

        private class R(val fd: Int, val file: Long) { var idx = -1; var pos = 0 }
        private class W(val fd: Int, val file: Long) { val adds = ArrayList<JSONObject>(); val data = ArrayList<ByteArrayOutputStream>() }
        private val readers = HashMap<Long, R>()
        private val writers = HashMap<Long, W>()
        /** Entrées (JSON de arcwAdd) et données de la dernière archive terminée (commit). */
        var lastAdds: List<JSONObject> = emptyList()
        var lastData: List<ByteArray> = emptyList()

        val openArchives get() = readers.size + writers.size
        fun pipesClosed() = pipes.isEmpty()

        fun openFd(f: File, write: Boolean): Int {
            val fd = nextFd++
            fds[fd] = f
            if (write) f.writeBytes(ByteArray(0))
            return fd
        }

        /** Comme ParcelFileDescriptor.createPipe : descripteur d'écriture pour le moteur, flux de lecture pour Archives. */
        fun pipe(): Pair<Int, InputStream> {
            val fd = nextFd++
            fds[fd] = File("tube-$fd")
            val out = PipedOutputStream()
            val inp = PipedInputStream(out, 1 shl 16)
            pipes[fd] = out
            return fd to inp
        }

        private fun release(fd: Int, file: Long) {
            if (fd >= 0) closedFds.add(fd) else closeFile(file)
        }

        override fun pwrite(file: Long, offset: Long, buf: ByteArray, len: Int): Int {
            onPwrite(++pwrites)
            return super.pwrite(file, offset, buf, len)
        }

        override fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int {
            onPread(++preads)
            return super.pread(file, offset, buf, len)
        }

        /** Comme le moteur : fermer le disque ferme les archives ouvertes sur ses fichiers. */
        override fun closeDisk(disk: Long): Int {
            readers.entries.removeAll { it.value.fd < 0 }
            writers.entries.removeAll { it.value.fd < 0 }
            files.clear()
            return 0
        }

        override fun statfs(vol: Long): ByteArray {
            val real = free
            free = real - cluster * (nodes.size - 1)
            try {
                return super.statfs(vol)
            } finally {
                free = real
            }
        }

        override fun arcOpenFd(fd: Int): Long = open(fd, -1)
        override fun arcOpenFile(file: Long, size: Long): Long = open(-1, file)

        private fun open(fd: Int, file: Long): Long {
            if (openCode < 0) {
                release(fd, file)
                return openCode.toLong()
            }
            val h = nextArc++
            readers[h] = R(fd, file)
            return h
        }

        override fun arcNext(arc: Long): ByteArray? {
            val r = readers[arc] ?: return """{"err":${EngineError.FL_EINVAL}}""".toByteArray()
            onNext(++nexts)
            r.idx++
            r.pos = 0
            val a = entries.getOrNull(r.idx) ?: return null
            return JSONObject().put("name", Base64.getEncoder().encodeToString(a.raw)).put("type", a.type).put("size", a.size)
                .put("mtime", 1714564800).put("encrypted", a.encrypted).put("raw", a.stream).toString().toByteArray()
        }

        override fun arcRead(arc: Long, buf: ByteArray, len: Int): Int {
            val r = readers[arc] ?: return EngineError.FL_EINVAL
            onArcRead(++arcReads)
            val a = entries[r.idx]
            if (a.encrypted) return EngineError.FL_ENOTSUP
            val n = minOf(len, a.data.size - r.pos)
            if (n <= 0) return 0
            System.arraycopy(a.data, r.pos, buf, 0, n)
            r.pos += n
            return n
        }

        override fun arcClose(arc: Long): Int {
            val r = readers.remove(arc) ?: return EngineError.FL_EINVAL
            release(r.fd, r.file)
            return 0
        }

        override fun arcwOpenFd(fd: Int): Long = openW(fd, -1)
        override fun arcwOpenFile(file: Long): Long = openW(-1, file)

        private fun openW(fd: Int, file: Long): Long {
            val h = nextArc++
            writers[h] = W(fd, file)
            return h
        }

        override fun arcwAdd(arc: Long, json: ByteArray): Int {
            val w = writers[arc] ?: return EngineError.FL_EINVAL
            w.adds.add(JSONObject(String(json, Charsets.UTF_8)))
            w.data.add(ByteArrayOutputStream())
            return 0
        }

        override fun arcwWrite(arc: Long, buf: ByteArray, len: Int): Int {
            val w = writers[arc] ?: return EngineError.FL_EINVAL
            onArcwWrite(++arcwWrites)
            w.data.last().write(buf, 0, len)
            return 0
        }

        override fun arcwClose(arc: Long, commit: Boolean): Int {
            val w = writers.remove(arc) ?: return EngineError.FL_EINVAL
            if (commit) {
                lastAdds = w.adds
                lastData = w.data.map { it.toByteArray() }
                when {
                    w.fd in pipes -> pipes[w.fd]!!.write(ZIP)
                    w.fd >= 0 -> fds[w.fd]!!.writeBytes(ZIP)
                    else -> pwrite(w.file, 0, ZIP, ZIP.size)
                }
            }
            pipes.remove(w.fd)?.close()
            release(w.fd, w.file)
            return 0
        }
    }

    private lateinit var e: Engine
    private lateinit var volId: String
    private lateinit var remote: RemoteLike
    private val root: File get() = File(tmp.root, "racine")
    private val remoteRoot: File get() = File(tmp.root, "distant")

    /**
     * Source « distante » : un dossier local, mais l'archive est lue par un descripteur mandataire
     * ([NativeIn.Proxy]) et le ZIP écrit par un tube ([NativeOut.Pipe]), comme un serveur FTP ou SFTP.
     */
    private inner class RemoteLike(val inner: LocalSource) : FileSource by inner {
        override val id = "remote"
        var closedWith: Boolean? = null
        var unknownSpace = false
        override fun space() = if (unknownSpace) -1L to -1L else inner.space()
        override fun openNative(path: String): NativeIn =
            NativeIn.Proxy(inner.stat(path)!!.size) { e.openFd(File(remoteRoot, path.substring(1)), false) }
        override fun createNative(parent: String, name: String): Pair<String, NativeOut> {
            val (p, out) = inner.createFile(parent, name)
            return p to NativeOut.Pipe(out) { closedWith = it }
        }
    }
    private val mib = 1 shl 20

    @Before fun setup() {
        root.mkdir()
        remoteRoot.mkdir()
        e = Engine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        val diskId = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(diskId, 1, true) }.id
        Transfers.resetForTests()
        Archives.resetForTests(e, e::openFd, e::pipe)
        val local = LocalSource("phone", "phone", "Téléphone", root) { true }
        remote = RemoteLike(LocalSource("remote", "remote", "Serveur", remoteRoot) { true })
        val disks = DiskSource(volId, e)
        Files.setResolverForTests { id ->
            when (id) {
                "phone" -> local
                "remote" -> remote
                volId -> disks.takeIf { VolumeManager.volume(volId) != null }
                else -> null
            }
        }
    }

    @After fun teardown() {
        Transfers.resetForTests()
        Archives.resetForTests(null, null, null)
        Files.setResolverForTests(null)
    }

    private fun data(n: Int, seed: Int = 7) = ByteArray(n) { (it * seed + it / 4093).toByte() }
    private fun local(path: String, bytes: ByteArray = "archive".toByteArray()) =
        File(root, path).apply { parentFile!!.mkdirs(); writeBytes(bytes) }
    private fun diskSrc(): FileSource = Files.source(volId)

    private fun extract(srcId: String, archive: String, dstId: String, dir: String, newFolder: Boolean): TransferState {
        Archives.startExtract(srcId, archive, dstId, dir, newFolder)
        return await()
    }

    private fun compress(srcId: String, paths: List<String>, dir: String, name: String? = null): TransferState {
        Archives.startCompress(srcId, paths, dir, name)
        return await()
    }

    private fun await(): TransferState {
        val end = System.currentTimeMillis() + 10_000
        while (true) {
            val s = Transfers.state.value
            if (s != null && s.finished && !Transfers.isBusy("phone") && !Transfers.isBusy(volId)) return s
            check(System.currentTimeMillis() < end) { "délai dépassé : $s" }
            Thread.sleep(10)
        }
    }

    /** Aucune archive, aucun fichier du moteur ni descripteur laissé ouvert, aucun fichier partiel nulle part. */
    private fun noLeak() {
        assertEquals(0, e.openArchives)
        assertTrue(e.files.isEmpty())
        assertEquals(e.fds.keys, e.closedFds)
        assertTrue(e.pipesClosed())
        val partial = tmp.root.walk().filter { it.name.endsWith(Transfers.PARTIAL_SUFFIX) }.toList()
        assertEquals(emptyList<File>(), partial)
        assertFalse(e.nodes.keys.any { it.endsWith(Transfers.PARTIAL_SUFFIX) })
    }

    @Test fun archiveExtensions() {
        for (n in listOf("a.zip", "a.ZIP", "a.7z", "a.rar", "a.tar", "a.tar.gz", "a.tgz", "a.tar.xz", "a.txz", "a.gz", "a.xz")) {
            assertTrue(n, Archives.isArchive(n))
        }
        for (n in listOf("a.txt", "zip", ".zip", "a.zip.txt", "a.bz2", "a")) assertFalse(n, Archives.isArchive(n))
        assertEquals("photos", Archives.stripExtension("photos.tar.gz"))
        assertEquals("photos", Archives.stripExtension("photos.TGZ"))
        assertEquals("notes.txt", Archives.stripExtension("notes.txt.gz"))
    }

    @Test fun extractIntoNewFolderUnique() {
        local("photos.tar.gz")
        File(root, "photos").mkdir()
        val b = data(1000); val c = data(3 * mib + 5, 3)
        e.entries = listOf(dir("a/"), file("a/b.txt", b), file("c.bin", c))
        val s = extract("phone", "/photos.tar.gz", "phone", "/", newFolder = true)
        assertNull(s.error); assertFalse(s.cancelled)
        assertEquals("extract", s.kind)
        assertEquals(2, s.fileCount); assertEquals(2, s.fileIndex)
        assertEquals((b.size + c.size).toLong(), s.totalBytes); assertEquals(s.totalBytes, s.doneBytes)
        assertArrayEquals(b, File(root, "photos (1)/a/b.txt").readBytes())
        assertArrayEquals(c, File(root, "photos (1)/c.bin").readBytes())
        assertEquals(0, File(root, "photos").list()!!.size)
        noLeak()
    }

    @Test fun extractFromDiskToPhone() {
        diskSrc().createFile("/", "a.zip").second.use { it.write("archive".toByteArray()) }
        val b = data(2 * mib + 1)
        e.entries = listOf(file("d/b.bin", b))
        File(root, "out").mkdir()
        val s = extract(volId, "/a.zip", "phone", "/out", newFolder = false)
        assertNull(s.error)
        assertArrayEquals(b, File(root, "out/d/b.bin").readBytes())
        noLeak()
    }

    @Test fun hostileEntriesSkipped() {
        local("h.zip")
        File(root, "out").mkdir()
        val d = "hostile\n".toByteArray()
        e.entries = listOf(
            file("../x", d), file("/abs", d), file("a/../../x", d), file("../../racine/h.zip", d),
            other("lien", "l"), other("dur", "h"), other("fifo", "o"),
            file("ok.txt", d), file("a/./b//c.txt", d),
        )
        val s = extract("phone", "/h.zip", "phone", "/out", newFolder = false)
        assertEquals(Archives.msgSkipped(7), s.error)
        assertTrue(s.finished); assertFalse(s.cancelled)
        assertArrayEquals(d, File(root, "out/ok.txt").readBytes())
        assertArrayEquals(d, File(root, "out/a/b/c.txt").readBytes())
        // rien hors de la destination : ni à côté de la racine, ni à la racine, ni à côté de l'archive
        assertEquals(setOf("racine", "distant"), tmp.root.list()!!.toSet())
        assertEquals(0, remoteRoot.list()!!.size)
        assertEquals(setOf("h.zip", "out"), root.list()!!.toSet())
        assertArrayEquals("archive".toByteArray(), File(root, "h.zip").readBytes())
        assertEquals(setOf("ok.txt", "a"), File(root, "out").list()!!.toSet())
        noLeak()
    }

    @Test fun symlinkThenFileThroughIt() {
        local("l.tar")
        File(root, "out").mkdir()
        val d = data(10)
        e.entries = listOf(Arc("l".toByteArray(), "l", 0, "/".toByteArray()), file("l/x", d))
        val s = extract("phone", "/l.tar", "phone", "/out", newFolder = false)
        assertEquals(Archives.msgSkipped(1), s.error)
        val l = File(root, "out/l")
        assertTrue(l.isDirectory); assertFalse(NioFiles.isSymbolicLink(l.toPath()))
        assertArrayEquals(d, File(l, "x").readBytes())
        noLeak()
    }

    @Test fun duplicateEntriesGetUniqueNames() {
        local("d.zip")
        File(root, "out").mkdir()
        e.entries = listOf(file("a.txt", data(1)), file("a.txt", data(2)), dir("a.txt"), file("a.txt/b", data(3)))
        val s = extract("phone", "/d.zip", "phone", "/out", newFolder = false)
        assertNull(s.error)
        assertArrayEquals(data(1), File(root, "out/a.txt").readBytes())
        assertArrayEquals(data(2), File(root, "out/a (1).txt").readBytes())
        assertArrayEquals(data(3), File(root, "out/a (2).txt/b").readBytes())
        noLeak()
    }

    @Test fun cp437NameExtracted() {
        local("w.zip")
        File(root, "out").mkdir()
        e.entries = listOf(Arc(byteArrayOf(0x82.toByte()) + ".txt".toByteArray(), "f", 3, "abc".toByteArray()))
        assertNull(extract("phone", "/w.zip", "phone", "/out", newFolder = false).error)
        assertArrayEquals("abc".toByteArray(), File(root, "out/é.txt").readBytes())
        noLeak()
    }

    @Test fun restrictedNamesSanitizedOnDisk() {
        local("w.zip")
        e.entries = listOf(file("a:b/c?.txt", data(4)))
        assertNull(extract("phone", "/w.zip", volId, "/", newFolder = false).error)
        assertArrayEquals(data(4), e.nodes["/a_b/c_.txt"])
        noLeak()
    }

    @Test fun bombStopsAtBudget() {
        e.free = 3L * mib; e.total = 20L * mib // réserve : 1 Mio (5 %), 2 Mio utilisables
        local("b.gz")
        // flux sans taille annoncée qui se décompresse en 5 Mio
        e.entries = listOf(Arc("b".toByteArray(), "f", -1, data(5 * mib)))
        var s = extract("phone", "/b.gz", volId, "/", newFolder = false)
        assertTrue(s.error, s.error!!.startsWith("Pas assez de place"))
        assertEquals(setOf("/"), e.nodes.keys)
        noLeak()
        // tailles annoncées au-delà de la place : refus avant toute lecture et toute écriture
        e.arcReads = 0
        e.entries = listOf(file("a.bin", data(10)), Arc("gros.bin".toByteArray(), "f", 2L * mib, ByteArray(0)))
        s = extract("phone", "/b.gz", volId, "/", newFolder = true)
        assertTrue(s.error, s.error!!.startsWith("Pas assez de place"))
        assertEquals(0, e.arcReads)
        assertEquals(setOf("/"), e.nodes.keys)
        noLeak()
    }

    @Test fun manySmallFilesStopAtReserve() {
        e.free = 3L * mib; e.total = 20L * mib // réserve : 1 Mio
        e.cluster = 128L shl 10 // clé exFAT : 128 Kio occupés par fichier, même pour 1 Kio de données
        local("petits.zip")
        e.entries = (1..100).map { file("f$it.txt", data(1024, it)) }
        val s = extract("phone", "/petits.zip", volId, "/", newFolder = false)
        assertNotNull(s.error)
        assertTrue(s.error, s.error!!.startsWith("Pas assez de place"))
        val left = e.free - e.cluster * (e.nodes.size - 1)
        assertTrue("libre : $left", left >= 1L * mib - e.cluster) // au plus un fichier au-delà de la réserve
        noLeak()
    }

    @Test fun perEntryCostInPrecheck() {
        e.free = 3L * mib; e.total = 20L * mib // 2 Mio utilisables
        local("p.zip")
        // 600 fichiers d'un octet : 600 o annoncés, mais au moins un bloc de 4 Kio chacun (2,3 Mio)
        e.entries = (1..600).map { file("f$it", data(1, it)) }
        val s = extract("phone", "/p.zip", volId, "/", newFolder = true)
        assertNotNull(s.error)
        assertTrue(s.error, s.error!!.startsWith("Pas assez de place"))
        assertEquals(0, e.arcReads)
        assertEquals(setOf("/"), e.nodes.keys)
        noLeak()
    }

    @Test fun oversizedHeaderStopsFirstPass() {
        local("b.tar.xz")
        var seen: TransferState? = null
        e.onNext = { if (it == 1) seen = Transfers.state.value }
        // en-tête honnête de 1 Tio de zéros : refus dès cet en-tête, sans décompresser la suite
        e.entries = listOf(Arc("zeros.bin".toByteArray(), "f", 1L shl 40), file("a.txt", data(10)), file("b.txt", data(10)))
        val s = extract("phone", "/b.tar.xz", "phone", "/", newFolder = true)
        assertNotNull(s.error)
        assertTrue(s.error, s.error!!.startsWith("Pas assez de place"))
        assertEquals(1, e.nexts)
        assertEquals(0, e.arcReads)
        // premier passage annoncé : « Lecture de l'archive b.tar.xz… »
        assertEquals("b.tar.xz", seen!!.currentName)
        assertEquals(0, seen!!.fileCount)
        assertEquals(listOf("b.tar.xz"), root.list()!!.toList())
        noLeak()
    }

    @Test fun singleStreamNamedAfterArchive() {
        local("notes.txt.xz")
        local("notes.txt", "déjà là".toByteArray())
        val d = data(5000)
        // libarchive nomme « data » l'unique entrée d'un .xz ou d'un .gz sans nom d'origine
        e.entries = listOf(Arc("data".toByteArray(), "f", -1, d, stream = true))
        val s = extract("phone", "/notes.txt.xz", "phone", "/", newFolder = true)
        assertNull(s.error)
        assertArrayEquals(d, File(root, "notes (1).txt").readBytes())
        assertEquals(setOf("notes.txt.xz", "notes.txt", "notes (1).txt"), root.list()!!.toSet())
        noLeak()
    }

    @Test fun extractThroughProxy() {
        File(remoteRoot, "a.zip").writeBytes("archive".toByteArray())
        val b = data(2 * mib + 1)
        e.entries = listOf(file("d/b.bin", b))
        File(root, "out").mkdir()
        val s = extract("remote", "/a.zip", "phone", "/out", newFolder = false)
        assertNull(s.error)
        assertArrayEquals(b, File(root, "out/d/b.bin").readBytes())
        noLeak()
    }

    @Test fun compressThroughPipe() {
        File(remoteRoot, "a.txt").writeBytes(data(1000)); File(remoteRoot, "b.txt").writeBytes(data(5000, 3))
        val s = compress("remote", listOf("/a.txt", "/b.txt"), "/", "lot")
        assertNull(s.error)
        assertArrayEquals(ZIP, File(remoteRoot, "lot.zip").readBytes())
        assertEquals(true, remote.closedWith)
        assertEquals(setOf("a.txt", "b.txt", "lot.zip"), remoteRoot.list()!!.toSet())
        noLeak()
    }

    @Test fun compressThroughPipeCancelled() {
        File(remoteRoot, "gros.bin").writeBytes(data(3 * mib))
        e.onArcwWrite = { if (it == 2) Transfers.cancel() }
        val s = compress("remote", listOf("/gros.bin"), "/")
        assertTrue(s.cancelled)
        assertEquals(setOf("gros.bin"), remoteRoot.list()!!.toSet())
        assertEquals(false, remote.closedWith)
        noLeak()
    }

    @Test fun extractFromRemoteAndCompressToRemote() {
        remote.unknownSpace = true // place libre inconnue d'un serveur : pas de vérification
        File(remoteRoot, "a.zip").writeBytes("archive".toByteArray())
        val b = data(2 * mib + 1)
        e.entries = listOf(file("d/b.bin", b))
        File(remoteRoot, "out").mkdir()
        assertNull(extract("remote", "/a.zip", "remote", "/out", newFolder = false).error)
        assertArrayEquals(b, File(remoteRoot, "out/d/b.bin").readBytes())
        val s = compress("remote", listOf("/out"), "/", "lot")
        assertNull(s.error)
        assertArrayEquals(ZIP, File(remoteRoot, "lot.zip").readBytes())
        assertEquals(true, remote.closedWith)
        noLeak()
    }

    @Test fun lyingSizeStops() {
        local("m.zip")
        File(root, "out").mkdir()
        val d = data(100)
        e.entries = listOf(file("ok.txt", d), Arc("faux.bin".toByteArray(), "f", 10, data(1000)), file("apres.txt", d))
        val s = extract("phone", "/m.zip", "phone", "/out", newFolder = false)
        assertEquals(Archives.MSG_CORRUPT, s.error)
        assertArrayEquals(d, File(root, "out/ok.txt").readBytes())
        assertEquals(listOf("ok.txt"), File(root, "out").list()!!.toList())
        noLeak()
    }

    @Test fun encryptedRefused() {
        local("s.zip")
        e.entries = listOf(file("a.txt", data(5)), Arc("secret.txt".toByteArray(), "f", 5, data(5), encrypted = true))
        val s = extract("phone", "/s.zip", "phone", "/", newFolder = true)
        assertEquals(Archives.MSG_ENCRYPTED, s.error)
        assertEquals(0, e.arcReads)
        assertEquals(listOf("s.zip"), root.list()!!.toList()) // pas de dossier créé
        noLeak()
    }

    @Test fun unknownFormatReported() {
        local("faux.zip")
        e.openCode = EngineError.FL_ENOTSUP
        val s = extract("phone", "/faux.zip", "phone", "/", newFolder = true)
        assertEquals(Archives.MSG_NOT_ARCHIVE, s.error)
        assertEquals(listOf("faux.zip"), root.list()!!.toList())
        noLeak()
    }

    @Test fun cancelRemovesPartial() {
        local("c.zip")
        File(root, "out").mkdir()
        e.entries = listOf(file("a.txt", data(1000)), file("gros.bin", data(3 * mib)))
        e.onArcRead = { if (it == 3) Transfers.cancel() } // première lecture de gros.bin
        val s = extract("phone", "/c.zip", "phone", "/out", newFolder = false)
        assertTrue(s.cancelled)
        assertEquals(listOf("a.txt"), File(root, "out").list()!!.toList())
        noLeak()
    }

    @Test fun sourceGoneDuringExtract() {
        diskSrc().createFile("/", "a.zip").second.use { it.write("archive".toByteArray()) }
        File(root, "out").mkdir()
        e.entries = listOf(file("a.txt", data(1000)), file("b.bin", data(8 * mib)))
        e.onArcRead = {
            if (it == 3) {
                VolumeManager.eject(volId)
                val end = System.currentTimeMillis() + 5_000
                while (VolumeManager.volume(volId) != null && System.currentTimeMillis() < end) Thread.sleep(5)
            }
        }
        val s = extract(volId, "/a.zip", "phone", "/out", newFolder = false)
        assertEquals(Transfers.MSG_SOURCE_GONE, s.error)
        assertEquals(listOf("a.txt"), File(root, "out").list()!!.toList())
        noLeak()
    }

    @Test fun compressStoresMediaDeflatesText() {
        val a = data(1000); val b = data(2 * mib + 3, 3); val c = data(77, 5)
        local("a.txt", a); local("b.JPG", b); local("D/c.mp3", c); File(root, "D/vide").mkdirs()
        val s = compress("phone", listOf("/a.txt", "/b.JPG", "/D"), "/")
        assertNull(s.error); assertEquals("compress", s.kind)
        assertEquals(3, s.fileCount); assertEquals((a.size + b.size + c.size).toLong(), s.totalBytes)
        assertEquals(s.totalBytes, s.doneBytes)
        assertEquals(listOf("a.txt", "b.JPG", "D", "D/c.mp3", "D/vide"), e.lastAdds.map { it.getString("name") })
        assertEquals(listOf("f", "f", "d", "f", "d"), e.lastAdds.map { it.getString("type") })
        assertEquals(listOf(false, true, false, true, false), e.lastAdds.map { it.getBoolean("store") })
        assertEquals(listOf(a.size, b.size, 0, c.size, 0).map(Int::toLong), e.lastAdds.map { it.getLong("size") })
        assertEquals(File(root, "a.txt").lastModified() / 1000, e.lastAdds[0].getLong("mtime"))
        assertArrayEquals(a, e.lastData[0]); assertArrayEquals(b, e.lastData[1]); assertArrayEquals(c, e.lastData[3])
        assertArrayEquals(ZIP, File(root, "Archive.zip").readBytes())
        noLeak()
    }

    @Test fun compressNeverIncludesItself() {
        local("D/a.txt", data(3))
        assertNull(compress("phone", listOf("/D"), "/D").error)
        assertEquals(listOf("D", "D/a.txt"), e.lastAdds.map { it.getString("name") })
        assertArrayEquals(ZIP, File(root, "D/D.zip").readBytes())
        noLeak()
    }

    @Test fun compressNameUnique() {
        diskSrc().createFile("/", "a.txt").second.use { it.write(data(9)) }
        assertNull(compress(volId, listOf("/a.txt"), "/").error)
        assertNull(compress(volId, listOf("/a.txt"), "/").error)
        assertNull(compress(volId, listOf("/a.txt"), "/", "sauvegarde").error)
        assertNull(compress(volId, listOf("/a.txt"), "/", "x.ZIP").error)
        assertNull(compress(volId, listOf("/a.txt"), "/", "a:b").error) // disque : nom assaini
        for (n in listOf("/a.zip", "/a (1).zip", "/sauvegarde.zip", "/x.ZIP", "/a_b.zip")) assertArrayEquals(n, ZIP, e.nodes[n])
        assertArrayEquals(data(9), e.lastData[0])
        noLeak()
    }

    @Test fun compressCancelRemovesPartial() {
        diskSrc().createFile("/", "gros.bin").second.use { it.write(data(3 * mib)) }
        e.onPread = { if (it == 2) Transfers.cancel() }
        val s = compress(volId, listOf("/gros.bin"), "/")
        assertTrue(s.cancelled)
        assertEquals(setOf("/", "/gros.bin"), e.nodes.keys)
        assertTrue(e.lastAdds.isEmpty())
        noLeak()
    }

    @Test fun oneOperationAtATime() {
        local("gros.bin", data(2 * mib))
        val inside = CountDownLatch(1); val release = CountDownLatch(1)
        e.onPwrite = { if (it == 1) { inside.countDown(); release.await(5, TimeUnit.SECONDS) } }
        Transfers.start("phone", listOf("/gros.bin"), volId, "/", false)
        assertTrue(inside.await(5, TimeUnit.SECONDS))
        for (op in listOf<() -> Unit>(
            { Archives.startCompress("phone", listOf("/gros.bin"), "/", null) },
            { Archives.startExtract("phone", "/gros.bin", "phone", "/", true) },
        )) {
            try {
                op()
                throw AssertionError("accepté")
            } catch (x: FilesException) {
                assertEquals(Transfers.MSG_BUSY, x.message)
            }
        }
        release.countDown()
        val s = await()
        assertNull(s.error); assertEquals("copy", s.kind)
    }

    private companion object {
        val ZIP = "PK\u0005\u0006 simulé".toByteArray()
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.TransferState
import com.mawena.fluluks.files.Transfers
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import java.nio.file.Files as NioFiles
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
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

/** Copies et déplacements entre une source locale (dossier temporaire) et un disque (moteur en mémoire). */
class TransfersTest {
    @get:Rule val tmp = TemporaryFolder()

    /** Moteur dont chaque pwrite et pread peut déclencher une action (annulation, éjection, attente). */
    private class Engine : MemEngine() {
        var onPwrite: (Int) -> Unit = {}
        var onPread: (Int) -> Unit = {}
        var pwrites = 0
        var preads = 0
        override fun pwrite(file: Long, offset: Long, buf: ByteArray, len: Int): Int {
            onPwrite(++pwrites)
            return super.pwrite(file, offset, buf, len)
        }
        override fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int {
            onPread(++preads)
            return super.pread(file, offset, buf, len)
        }
    }

    private lateinit var e: Engine
    private lateinit var volId: String
    private lateinit var diskId: String
    private val root: File get() = tmp.root
    private val mib = 1 shl 20

    @Before fun setup() {
        e = Engine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        diskId = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(diskId, 1, true) }.id
        Transfers.resetForTests()
        val local = LocalSource("phone", "phone", "Téléphone", root) { true }
        val disks = DiskSource(volId, e)
        Files.setResolverForTests { id ->
            when (id) {
                "phone" -> local
                volId -> disks.takeIf { VolumeManager.volume(volId) != null }
                else -> null
            }
        }
    }

    @After fun teardown() {
        Transfers.resetForTests()
        Files.setResolverForTests(null)
    }

    private fun data(n: Int, seed: Int = 7) = ByteArray(n) { (it * seed + it / 4093).toByte() }
    private fun local(path: String, bytes: ByteArray) = File(root, path).apply { parentFile!!.mkdirs(); writeBytes(bytes) }
    private fun disk(path: String) = e.nodes[path]
    private fun diskSrc(): FileSource = Files.source(volId)

    private fun run(srcId: String, paths: List<String>, dstId: String, dir: String, move: Boolean = false): TransferState {
        Transfers.start(srcId, paths, dstId, dir, move)
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

    @Test fun copyLocalToDiskWithProgress() {
        val a = data(1000); val b = data(3 * mib + 17, 3); val c = data(5)
        local("a.txt", a); local("b.bin", b); local("D/c.txt", c); File(root, "D/vide").mkdirs()
        val seen = ArrayList<TransferState>()
        e.onPwrite = { Transfers.state.value?.let(seen::add) }
        val s = run("phone", listOf("/a.txt", "/b.bin", "/D"), volId, "/")
        assertTrue(s.finished); assertFalse(s.cancelled); assertNull(s.error)
        assertEquals(3, s.fileCount); assertEquals(3, s.fileIndex)
        assertEquals(s.totalBytes, s.doneBytes)
        assertEquals((a.size + b.size + c.size).toLong(), s.totalBytes)
        assertArrayEquals(a, disk("/a.txt")); assertArrayEquals(b, disk("/b.bin")); assertArrayEquals(c, disk("/D/c.txt"))
        assertTrue(e.nodes.containsKey("/D/vide") && disk("/D/vide") == null)
        assertTrue(seen.any { it.currentName == "b.bin" && it.fileIndex == 2 })
        assertTrue(File(root, "a.txt").exists()) // copie : la source reste
        assertTrue(e.files.isEmpty())
    }

    @Test fun copyDiskToLocal() {
        val x = data(2 * mib + 3)
        diskSrc().mkdir("/", "P")
        diskSrc().createFile("/P", "x.bin").second.use { it.write(x) }
        File(root, "dest").mkdir()
        val s = run(volId, listOf("/P"), "phone", "/dest")
        assertNull(s.error)
        assertArrayEquals(x, File(root, "dest/P/x.bin").readBytes())
    }

    @Test fun copyRefusedWithoutSpace() {
        local("gros.bin", data(3 * mib))
        e.free = mib.toLong()
        val before = e.nodes.keys.toSet()
        val s = run("phone", listOf("/gros.bin"), volId, "/")
        assertEquals("Pas assez de place : 3,1 Mo nécessaires, 1,0 Mo libres", s.error)
        assertEquals(before, e.nodes.keys)
    }

    @Test fun copyIntoItselfRefused() {
        local("D/sous/f.txt", data(3))
        val before = root.walk().map { it.path }.toSet()
        assertEquals(Transfers.MSG_INTO_ITSELF, run("phone", listOf("/D"), "phone", "/D/sous").error)
        assertEquals(Transfers.MSG_INTO_ITSELF, run("phone", listOf("/D"), "phone", "/D", move = true).error)
        assertEquals(before, root.walk().map { it.path }.toSet())
        diskSrc().mkdir("/", "E")
        diskSrc().mkdir("/E", "F")
        val nodes = e.nodes.keys.toSet()
        assertEquals(Transfers.MSG_INTO_ITSELF, run(volId, listOf("/E"), volId, "/E/F").error)
        assertEquals(nodes, e.nodes.keys)
    }

    @Test fun copySanitizesNames() {
        local("photo:1.jpg", data(4))
        local("a?b", data(2))
        local("fin. ", data(1))
        val s = run("phone", root.list()!!.map { "/$it" }, volId, "/")
        assertNull(s.error)
        assertArrayEquals(data(4), disk("/photo_1.jpg"))
        assertArrayEquals(data(2), disk("/a_b"))
        assertArrayEquals(data(1), disk("/fin"))
    }

    @Test fun copyUniqueNamesCaseInsensitive() {
        e.caseInsensitive = true
        diskSrc().createFile("/", "X.txt").second.use { it.write(1) }
        local("x.txt", data(9))
        assertNull(run("phone", listOf("/x.txt"), volId, "/").error)
        assertArrayEquals(byteArrayOf(1), disk("/X.txt"))
        assertArrayEquals(data(9), disk("/x (1).txt"))
    }

    @Test fun moveSameSourceRenames() {
        diskSrc().mkdir("/", "D")
        diskSrc().createFile("/D", "f.bin").second.use { it.write(data(mib)) }
        diskSrc().mkdir("/", "E")
        e.preads = 0; e.pwrites = 0
        val s = run(volId, listOf("/D"), volId, "/E", move = true)
        assertNull(s.error)
        assertArrayEquals(data(mib), disk("/E/D/f.bin"))
        assertFalse(e.nodes.containsKey("/D"))
        assertEquals(0, e.preads); assertEquals(0, e.pwrites)
        // sur le téléphone aussi : renommage, même inode
        local("L/g.txt", data(3))
        File(root, "M").mkdir()
        val ino = NioFiles.getAttribute(File(root, "L/g.txt").toPath(), "unix:ino")
        assertNull(run("phone", listOf("/L"), "phone", "/M", move = true).error)
        assertEquals(ino, NioFiles.getAttribute(File(root, "M/L/g.txt").toPath(), "unix:ino"))
    }

    @Test fun moveDeletesSourceOnlyAfterCopy() {
        local("D/a.bin", data(2 * mib)); local("D/b.bin", data(2 * mib, 5)); local("c.txt", data(4))
        var sourceAlwaysThere = true
        e.onPwrite = {
            val copyingD = Transfers.state.value?.currentName in setOf("a.bin", "b.bin")
            if (copyingD && !(File(root, "D/a.bin").exists() && File(root, "D/b.bin").exists())) sourceAlwaysThere = false
        }
        val s = run("phone", listOf("/D", "/c.txt"), volId, "/", move = true)
        assertNull(s.error)
        assertTrue(sourceAlwaysThere)
        assertFalse(File(root, "D").exists()); assertFalse(File(root, "c.txt").exists())
        assertArrayEquals(data(2 * mib), disk("/D/a.bin")); assertArrayEquals(data(2 * mib, 5), disk("/D/b.bin"))
        assertArrayEquals(data(4), disk("/c.txt"))
    }

    @Test fun moveCancelledKeepsSource() {
        local("D/a.bin", data(2 * mib)); local("D/b.bin", data(3 * mib, 5))
        e.onPwrite = { if (it == 3) Transfers.cancel() } // pendant le deuxième fichier
        val s = run("phone", listOf("/D"), volId, "/", move = true)
        assertTrue(s.cancelled); assertTrue(s.finished)
        assertArrayEquals(data(2 * mib), File(root, "D/a.bin").readBytes())
        assertArrayEquals(data(3 * mib, 5), File(root, "D/b.bin").readBytes())
        assertArrayEquals(data(2 * mib), disk("/D/a.bin")) // complet : gardé
        assertFalse(e.nodes.containsKey("/D/b.bin")) // partiel : supprimé
        assertTrue(e.files.isEmpty())
    }

    @Test fun cancelRemovesPartial() {
        local("gros.bin", data(4 * mib))
        e.onPwrite = { if (it == 1) Transfers.cancel() }
        val s = run("phone", listOf("/gros.bin"), volId, "/")
        assertTrue(s.cancelled)
        assertFalse(e.nodes.containsKey("/gros.bin"))
        assertTrue(File(root, "gros.bin").exists())
    }

    @Test fun efbigRemovesPartialAndReports() {
        local("gros.bin", data(3 * mib))
        e.failPwriteAt = 2
        e.failPwriteCode = EngineError.FL_EFBIG
        val s = run("phone", listOf("/gros.bin"), volId, "/")
        assertEquals("Fichier trop gros pour ce disque (4 Gio au plus en FAT)", s.error)
        assertFalse(s.cancelled)
        assertFalse(e.nodes.containsKey("/gros.bin"))
        assertTrue(e.files.isEmpty())
    }

    @Test fun destinationGoneFails() {
        local("a.bin", data(3 * mib)); local("b.bin", data(mib))
        e.onPwrite = { if (it == 1) VolumeManager.eject(volId) }
        val s = run("phone", listOf("/a.bin", "/b.bin"), volId, "/")
        assertEquals(Transfers.MSG_SOURCE_GONE, s.error)
        assertTrue(File(root, "a.bin").exists())
        assertTrue(File(root, "b.bin").exists())
        // source disparue : même message
        File(root, "dest").mkdir()
        assertEquals(Transfers.MSG_SOURCE_GONE, run(volId, listOf("/a.bin"), "phone", "/dest").error)
    }

    @Test fun secondTransferRefused() {
        local("a.bin", data(2 * mib))
        val inside = CountDownLatch(1); val release = CountDownLatch(1)
        e.onPwrite = { if (it == 1) { inside.countDown(); release.await(5, TimeUnit.SECONDS) } }
        Transfers.start("phone", listOf("/a.bin"), volId, "/", false)
        assertTrue(inside.await(5, TimeUnit.SECONDS))
        assertTrue(Transfers.isBusy(volId)); assertTrue(Transfers.isBusy("phone")); assertFalse(Transfers.isBusy("sd:x"))
        try {
            Transfers.start("phone", listOf("/a.bin"), "phone", "/", false)
            throw AssertionError("accepté")
        } catch (x: FilesException) {
            assertEquals(Transfers.MSG_BUSY, x.message)
        }
        release.countDown()
        assertNull(await().error)
        assertFalse(Transfers.isBusy(volId))
    }

    @Test fun linksSkippedAndReported() {
        local("D/f.txt", data(3))
        NioFiles.createSymbolicLink(File(root, "D/lien").toPath(), File("/").toPath())
        val s = run("phone", listOf("/D"), volId, "/", move = true)
        assertEquals(Transfers.MSG_LINKS, s.error)
        assertFalse(s.cancelled)
        assertArrayEquals(data(3), disk("/D/f.txt"))
        assertFalse(e.nodes.containsKey("/D/lien"))
        assertTrue(File(root, "D/f.txt").exists()) // élément incomplet : la source reste
    }

    @Test fun readOnlyDestinationRefused() {
        local("a.txt", data(3))
        e.readOnly = true; e.roReason = "disque protégé en écriture"
        assertEquals("Volume en lecture seule : disque protégé en écriture", run("phone", listOf("/a.txt"), volId, "/").error)
        assertEquals(setOf("/"), e.nodes.keys)
    }

    /** Relecture finale : un fichier encore en cours d'écriture (téléchargement) n'est pas supprimé. */
    @Test fun moveKeepsSourceChangedDuringCopy() {
        local("dl.bin", data(3 * mib)); local("ok.txt", data(4))
        e.onPwrite = { if (it == 1) File(root, "dl.bin").appendBytes(data(10, 3)) }
        val s = run("phone", listOf("/dl.bin", "/ok.txt"), volId, "/", move = true)
        assertEquals(Transfers.MSG_CHANGED, s.error)
        assertEquals(3L * mib + 10, File(root, "dl.bin").length()) // source gardée, entière
        assertFalse(File(root, "ok.txt").exists()) // l'autre élément est bien déplacé
        assertArrayEquals(data(4), disk("/ok.txt"))
    }

    /** Phase 7 : disque débranché pendant un déplacement disque → téléphone : rien de perdu. */
    @Test fun detachDuringMoveKeepsSource() {
        local("a.bin", data(3 * mib))
        assertNull(run("phone", listOf("/a.bin"), volId, "/").error) // le fichier est maintenant sur le disque
        File(root, "a.bin").delete()
        File(root, "dest").mkdir()
        e.preads = 0
        e.onPread = { if (it == 3) VolumeManager.onUsbDetachedName(diskId) }
        val s = run(volId, listOf("/a.bin"), "phone", "/dest", move = true)
        assertEquals(Transfers.MSG_SOURCE_GONE, s.error)
        assertArrayEquals(data(3 * mib), disk("/a.bin")) // la source reste sur le disque
        assertTrue(File(root, "dest").list()!!.none { it == "a.bin" }) // jamais de fichier incomplet sous son nom
    }

    /** Phase 7 : durée de l'opération (débit affiché à la fin). */
    @Test fun elapsedMeasured() {
        var t = 1_000L
        Transfers.resetForTests(clock = { t })
        local("a.bin", data(mib))
        e.onPwrite = { t = 3_000L }
        val s = run("phone", listOf("/a.bin"), volId, "/")
        assertNull(s.error)
        assertEquals(2_000L, s.elapsedMs)
    }

    /** Phase 7 : pendant l'écriture, le fichier porte un nom temporaire ; son vrai nom seulement une fois vérifié. */
    @Test fun partialHasTemporaryName() {
        local("a.bin", data(3 * mib))
        val seen = mutableSetOf<String>()
        e.onPwrite = { seen += e.nodes.keys.filter { it != "/" } }
        val s = run("phone", listOf("/a.bin"), volId, "/")
        assertNull(s.error)
        assertEquals(setOf("/.a.bin.portail-partiel"), seen)
        assertEquals(setOf("/", "/a.bin"), e.nodes.keys)
        assertArrayEquals(data(3 * mib), disk("/a.bin"))
    }

    @Test fun failureRemovesTemporary() {
        local("gros.bin", data(3 * mib))
        e.failPwriteAt = 2
        e.failPwriteCode = EngineError.FL_EFBIG
        run("phone", listOf("/gros.bin"), volId, "/")
        assertEquals(setOf("/"), e.nodes.keys)
    }

    /** Phase 7 : un nom de 255 octets copié deux fois dans le même dossier : deux fichiers, noms ≤ 255 octets. */
    @Test fun copyLongNameTwice() {
        val long = "n".repeat(251) + ".bin" // 255 octets : la limite d'un nom local
        local(long, data(10))
        var tooLong = false
        e.onPwrite = { if (e.nodes.keys.any { it.substringAfterLast('/').toByteArray().size > 255 }) tooLong = true }
        assertNull(run("phone", listOf("/$long"), volId, "/").error)
        assertNull(run("phone", listOf("/$long"), volId, "/").error)
        assertFalse(tooLong)
        val names = e.nodes.keys.filter { it != "/" }.map { it.substring(1) }
        assertEquals(2, names.size)
        assertTrue(names.toString(), names.all { it.toByteArray().size <= 255 && it.endsWith(".bin") })
    }

    /** Relecture finale : un « .nom.portail-partiel » laissé par une copie interrompue est remplacé, pas doublé. */
    @Test fun stalePartialReplaced() {
        e.nodes["/.a.bin.portail-partiel"] = data(5) // reste d'une copie interrompue (processus tué)
        local("a.bin", data(mib))
        assertNull(run("phone", listOf("/a.bin"), volId, "/").error)
        assertEquals(setOf("/", "/a.bin"), e.nodes.keys)
    }
}

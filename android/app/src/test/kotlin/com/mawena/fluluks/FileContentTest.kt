package com.mawena.fluluks

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileContent
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.Transfers
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import java.io.FileNotFoundException
import java.io.RandomAccessFile
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Images et textes des lecteurs, sur une source locale (dossier temporaire) et un disque en mémoire. */
class FileContentTest {
    @get:Rule val tmp = TemporaryFolder()

    private lateinit var e: MemEngine
    private lateinit var volId: String
    private val root: File get() = File(tmp.root, "racine")
    private val bom = byteArrayOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte())

    @Before fun setup() {
        root.mkdir()
        e = MemEngine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        val diskId = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(diskId, 1, true) }.id
        FileContent.heicToJpeg = null
    }

    @After fun teardown() {
        FileContent.heicToJpeg = null
    }

    private fun phone() = LocalSource("phone", "phone", "Téléphone", root) { true }
    private fun disk(): FileSource = DiskSource(volId, e)
    private fun local(name: String, bytes: ByteArray) = File(root, name).apply { writeBytes(bytes) }
    private fun onDisk(name: String, bytes: ByteArray) = disk().createFile("/", name).second.use { it.write(bytes) }
    private fun sparse(name: String, size: Long) = File(root, name).also { RandomAccessFile(it, "rw").use { f -> f.setLength(size) } }

    private inline fun <reified T : Throwable> refused(block: () -> Unit): T {
        try { block() } catch (t: Throwable) { if (t is T) return t; throw t }
        throw AssertionError("accepté")
    }

    /** Aucun fichier temporaire d'enregistrement laissé, ni sur le téléphone ni sur le disque. */
    private fun noPartial() {
        assertEquals(emptyList<File>(), root.walk().filter { it.name.endsWith(Transfers.PARTIAL_SUFFIX) }.toList())
        assertFalse(e.nodes.keys.any { it.endsWith(Transfers.PARTIAL_SUFFIX) })
    }

    @Test fun readImageReturnsBytes() {
        val b = ByteArray(300_000) { (it * 7).toByte() }
        local("photo.JPG", b)
        assertArrayEquals(b, FileContent.readImage(phone(), "/photo.JPG"))
        onDisk("p.png", b)
        assertArrayEquals(b, FileContent.readImage(disk(), "/p.png"))
    }

    @Test fun imageTooBigRefused() {
        sparse("grande.jpg", FileContent.MAX_IMAGE + 1)
        val x = refused<FilesException> { FileContent.readImage(phone(), "/grande.jpg") }
        assertEquals(FileContent.MSG_IMAGE_TOO_BIG, x.message)
    }

    @Test fun heicConverted() {
        val heic = "heic brut".toByteArray()
        var given: ByteArray? = null
        FileContent.heicToJpeg = { given = it; "jpeg".toByteArray() }
        local("IMG.HEIC", heic)
        assertArrayEquals("jpeg".toByteArray(), FileContent.readImage(phone(), "/IMG.HEIC"))
        assertArrayEquals(heic, given)
        local("b.heif", heic)
        assertArrayEquals("jpeg".toByteArray(), FileContent.readImage(phone(), "/b.heif"))
    }

    @Test fun readTextDetectsBomAndCrlf() {
        val f = local("notes.txt", bom + "a\r\nété\r\n".toByteArray())
        f.setLastModified(1_700_000_000_000)
        val d = FileContent.readText(phone(), "/notes.txt")
        assertEquals("a\nété\n", d.text)
        assertTrue(d.bom); assertTrue(d.crlf)
        assertEquals(f.length(), d.size); assertEquals(1_700_000_000L, d.mtime)
        assertNull(d.readOnlyReason)
        local("unix.md", "x\ny".toByteArray())
        val u = FileContent.readText(phone(), "/unix.md")
        assertEquals("x\ny", u.text); assertFalse(u.bom); assertFalse(u.crlf)
    }

    @Test fun notUtf8Refused() {
        local("latin1.txt", "café".toByteArray(Charsets.ISO_8859_1))
        assertEquals(FileContent.MSG_NOT_TEXT, refused<FilesException> { FileContent.readText(phone(), "/latin1.txt") }.message)
        local("binaire.txt", byteArrayOf(0x61, 0, 0x62))
        assertEquals(FileContent.MSG_NOT_TEXT, refused<FilesException> { FileContent.readText(phone(), "/binaire.txt") }.message)
    }

    @Test fun textTooBigRefused() {
        sparse("gros.log", FileContent.MAX_TEXT + 1)
        assertEquals(FileContent.MSG_TEXT_TOO_BIG, refused<FilesException> { FileContent.readText(phone(), "/gros.log") }.message)
    }

    @Test fun roundTripIdentical() {
        val b = bom + "ligne 1\r\nété\r\n".toByteArray()
        local("t.txt", b)
        onDisk("t.txt", b)
        for (s in listOf(phone(), disk())) {
            val d = FileContent.readText(s, "/t.txt")
            val r = FileContent.writeText(s, "/t.txt", d.text, d.bom, d.crlf, d.mtime, d.size, force = false)
            assertTrue(r.saved); assertFalse(r.changed)
            assertEquals(b.size.toLong(), r.size)
        }
        assertArrayEquals(b, File(root, "t.txt").readBytes())
        assertArrayEquals(b, e.nodes["/t.txt"])
        noPartial()
    }

    @Test fun editedTextSaved() {
        onDisk("t.txt", "a\r\nb".toByteArray())
        val d = FileContent.readText(disk(), "/t.txt")
        val r = FileContent.writeText(disk(), "/t.txt", "a\nb\nc", d.bom, d.crlf, d.mtime, d.size, force = false)
        assertTrue(r.saved)
        assertArrayEquals("a\r\nb\r\nc".toByteArray(), e.nodes["/t.txt"])
        assertEquals(setOf("/", "/t.txt"), e.nodes.keys)
    }

    @Test fun changedSinceOpenNotSaved() {
        val f = local("t.txt", "un".toByteArray())
        f.setLastModified(1_700_000_000_000)
        val d = FileContent.readText(phone(), "/t.txt")
        f.writeText("deux") // modifié par une autre application
        f.setLastModified(1_700_000_100_000)
        val r = FileContent.writeText(phone(), "/t.txt", "trois", d.bom, d.crlf, d.mtime, d.size, force = false)
        assertFalse(r.saved); assertTrue(r.changed)
        assertEquals("deux", f.readText())
        val forced = FileContent.writeText(phone(), "/t.txt", "trois", d.bom, d.crlf, d.mtime, d.size, force = true)
        assertTrue(forced.saved)
        assertEquals("trois", f.readText())
        // sur le disque : la taille suffit à voir le changement
        onDisk("d.txt", "un".toByteArray())
        val dd = FileContent.readText(disk(), "/d.txt")
        e.nodes["/d.txt"] = "un de plus".toByteArray()
        assertTrue(FileContent.writeText(disk(), "/d.txt", "x", dd.bom, dd.crlf, dd.mtime, dd.size, force = false).changed)
        assertArrayEquals("un de plus".toByteArray(), e.nodes["/d.txt"])
        noPartial()
    }

    @Test fun failedWriteKeepsOriginal() {
        onDisk("t.txt", "original".toByteArray())
        val d = FileContent.readText(disk(), "/t.txt")
        e.failPwriteAt = 1
        e.failPwriteCode = EngineError.FL_ENOSPC
        refused<Exception> { FileContent.writeText(disk(), "/t.txt", "nouveau texte", d.bom, d.crlf, d.mtime, d.size, force = false) }
        assertArrayEquals("original".toByteArray(), e.nodes["/t.txt"])
        noPartial()
    }

    /** Relecture finale (I2) : le même fichier est réécrit (droits, inode gardés), jamais supprimé puis recréé. */
    @Test fun saveRewritesSameFile() {
        val f = local("deploy.sh", "echo un\n".toByteArray())
        assertTrue(f.setExecutable(true))
        val ino = java.nio.file.Files.getAttribute(f.toPath(), "unix:ino")
        val d = FileContent.readText(phone(), "/deploy.sh")
        assertTrue(FileContent.writeText(phone(), "/deploy.sh", "echo deux\n", d.bom, d.crlf, d.mtime, d.size, force = false).saved)
        assertEquals("echo deux\n", f.readText())
        assertTrue(f.canExecute())
        assertEquals(ino, java.nio.file.Files.getAttribute(f.toPath(), "unix:ino"))
        noPartial()
        // disque : réécrit aussi en place
        onDisk("t.txt", "original".toByteArray())
        val dd = FileContent.readText(disk(), "/t.txt")
        assertTrue(FileContent.writeText(disk(), "/t.txt", "nouveau", dd.bom, dd.crlf, dd.mtime, dd.size, force = false).saved)
        assertArrayEquals("nouveau".toByteArray(), e.nodes["/t.txt"])
        noPartial()
    }

    /** Réécriture en place interrompue : le texte complet reste dans le temporaire, et le message le dit. */
    @Test fun failedRewriteKeepsFullTextInTemp() {
        local("notes.txt", "ancien".toByteArray())
        val inner = phone()
        val failing = object : FileSource by inner {
            override fun openOverwrite(path: String): java.io.OutputStream = object : java.io.OutputStream() {
                override fun write(b: Int) = throw java.io.IOException("disque arraché")
                override fun write(b: ByteArray, off: Int, len: Int) = throw java.io.IOException("disque arraché")
            }
        }
        val d = FileContent.readText(failing, "/notes.txt")
        val x = refused<FileNotFoundException> {
            FileContent.writeText(failing, "/notes.txt", "texte précieux", d.bom, d.crlf, d.mtime, d.size, force = false)
        }
        val tmpName = Transfers.partialName("notes.txt")
        assertTrue(x.message!!, x.message!!.contains(tmpName))
        assertEquals("texte précieux", File(root, tmpName).readText())
    }

    @Test fun readOnlySourceRefused() {
        local("t.txt", "lu".toByteArray())
        root.setWritable(false) // stockage en lecture seule
        try {
            val ro = phone()
            assumeTrue(ro.readOnlyReason != null) // pas sous root
            val d = FileContent.readText(ro, "/t.txt")
            assertEquals("lu", d.text)
            assertNotNull(d.readOnlyReason)
            refused<FileNotFoundException> { FileContent.writeText(ro, "/t.txt", "écrit", d.bom, d.crlf, d.mtime, d.size, force = true) }
            assertEquals("lu", File(root, "t.txt").readText())
        } finally {
            root.setWritable(true)
        }
    }

    @Test fun directoryRefused() {
        File(root, "dossier").mkdir()
        assertEquals(EngineError.FL_EISDIR, refused<FilesException> { FileContent.readText(phone(), "/dossier") }.code)
    }
}

package com.mawena.fluluks

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import android.provider.DocumentsContract.Root
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import android.system.OsConstants
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.provider.FluLuksDocumentsProvider
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import java.io.FileInputStream
import java.io.FileNotFoundException
import java.io.IOException
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Écriture par le DocumentsProvider, à travers ContentResolver et DocumentsContract, sur une copie d'une
 * image ext4 dans le cache de l'application. `diskFullFailsCleanly` laisse sa petite image en place :
 * `tools/android/check_image.sh <serial> provider-plein.img` la vérifie avec e2fsck sur le PC.
 */
@RunWith(AndroidJUnit4::class)
class ProviderWriteTest {
    private val ctx get() = InstrumentationRegistry.getInstrumentation().targetContext
    private val cr: ContentResolver get() = ctx.contentResolver
    private val auth = VolumeManager.AUTHORITY
    private var diskId: String? = null
    private lateinit var vol: String

    @Before fun setUp() {
        VolumeManager.init(ctx)
        VolumeManager.ejectAll()
        waitFor("volumes vides") { VolumeManager.volumes.value.isEmpty() }
    }

    @After fun tearDown() {
        VolumeManager.ejectAll()
        waitFor("volumes vides") { VolumeManager.volumes.value.isEmpty() }
        diskId?.let { VolumeManager.removeDiskForTests(it) }
    }

    /** Copie la fixture dans le cache sous `copy` et la monte (en écriture si `writable`). */
    private fun mount(fixture: String = "ext4-rw-reel.img", copy: String = "provider-ecriture.img", writable: Boolean = true) {
        val img = File(ctx.cacheDir, copy)
        Fixtures.file(fixture).copyTo(img, overwrite = true)
        val id = VolumeManager.addImageDiskForTests(img.absolutePath, readOnly = !writable)
        diskId = id
        val part = VolumeManager.disks.value.first { it.id == id }.scan!!.parts.first()
        vol = runBlocking { VolumeManager.mountPlain(id, part.index, writable) }.id
    }

    private fun waitFor(what: String, timeoutMs: Long = 10_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé : $what" }
            Thread.sleep(50)
        }
    }

    private fun docUri(path: String): Uri = DocumentsContract.buildDocumentUri(auth, "$vol:$path")
    private fun pathOf(uri: Uri): String = DocumentsContract.getDocumentId(uri).substringAfter(':')

    private fun create(parent: String, name: String, mime: String = "application/octet-stream"): Uri =
        DocumentsContract.createDocument(cr, docUri(parent), mime, name)
            ?: throw FileNotFoundException("createDocument a rendu null")

    private fun mkdir(parent: String, name: String) = create(parent, name, Document.MIME_TYPE_DIR)

    private fun pattern(len: Int, seed: Int = 0) = ByteArray(len) { (((it + seed) * 31 + 7) and 0xFF).toByte() }

    private fun write(uri: Uri, data: ByteArray, mode: String = "w") {
        cr.openOutputStream(uri, mode)!!.use { it.write(data) }
    }

    private fun read(uri: Uri): ByteArray = cr.openFileDescriptor(uri, "r")!!.use { FileInputStream(it.fileDescriptor).readBytes() }

    private fun exists(path: String): Boolean = try {
        cr.query(docUri(path), null, null, null, null)?.use { it.moveToFirst() } ?: false
    } catch (e: Exception) {
        false
    }

    private fun childNames(path: String): Set<String> =
        cr.query(DocumentsContract.buildChildDocumentsUri(auth, "$vol:$path"), null, null, null, null)!!.use { c ->
            val out = HashSet<String>()
            while (c.moveToNext()) out.add(c.getString(c.getColumnIndexOrThrow(Document.COLUMN_DISPLAY_NAME)))
            out
        }

    private fun flags(path: String): Int = cr.query(docUri(path), null, null, null, null)!!.use {
        assertTrue(it.moveToFirst())
        it.getInt(it.getColumnIndexOrThrow(Document.COLUMN_FLAGS))
    }

    @Test fun writableFlags() {
        mount()
        cr.query(DocumentsContract.buildRootsUri(auth), null, null, null, null)!!.use { c ->
            assertTrue(c.moveToFirst())
            assertTrue(c.getInt(c.getColumnIndexOrThrow(Root.COLUMN_FLAGS)) and Root.FLAG_SUPPORTS_CREATE != 0)
            assertEquals("Lecture et écriture", c.getString(c.getColumnIndexOrThrow(Root.COLUMN_SUMMARY)))
        }
        assertEquals(Document.FLAG_DIR_SUPPORTS_CREATE, flags("/"))
        val f = create("/", "f.bin")
        val d = mkdir("/", "d")
        val common = Document.FLAG_SUPPORTS_DELETE or Document.FLAG_SUPPORTS_RENAME or
            Document.FLAG_SUPPORTS_MOVE or Document.FLAG_SUPPORTS_COPY
        assertEquals(common or Document.FLAG_SUPPORTS_WRITE, flags(pathOf(f)))
        assertEquals(common or Document.FLAG_DIR_SUPPORTS_CREATE, flags(pathOf(d)))
    }

    @Test fun createWriteReadBack() {
        mount()
        val uri = create("/", "trois.bin")
        assertEquals("/trois.bin", pathOf(uri))
        val data = pattern(3 shl 20)
        write(uri, data)
        assertArrayEquals(data, read(uri))
    }

    private fun rewrite(mode: String): ByteArray {
        mount()
        val uri = create("/", "un.bin")
        write(uri, pattern(1 shl 20))
        write(uri, ByteArray(10) { 0x55 }, mode)
        return read(uri)
    }

    @Test fun modeWTruncates() = assertEquals(10, rewrite("w").size)
    @Test fun modeWtTruncates() = assertEquals(10, rewrite("wt").size)
    @Test fun modeRwtTruncates() = assertEquals(10, rewrite("rwt").size)

    @Test fun modeRwKeeps() {
        val got = rewrite("rw")
        val want = pattern(1 shl 20).also { for (i in 0 until 10) it[i] = 0x55 }
        assertArrayEquals(want, got)
    }

    @Test fun modeWaAppends() {
        val got = rewrite("wa")
        assertEquals((1 shl 20) + 10, got.size)
        assertArrayEquals(pattern(1 shl 20), got.copyOf(1 shl 20))
        assertArrayEquals(ByteArray(10) { 0x55 }, got.copyOfRange(1 shl 20, got.size))
    }

    @Test fun createNameConflict() {
        mount()
        assertEquals("/a.txt", pathOf(create("/", "a.txt", "text/plain")))
        assertEquals("/a (1).txt", pathOf(create("/", "a.txt", "text/plain")))
        assertEquals("/a (2).txt", pathOf(create("/", "a.txt", "text/plain")))
        assertEquals("/d", pathOf(mkdir("/", "d")))
        assertEquals("/d (1)", pathOf(mkdir("/", "d")))
    }

    @Test fun forbiddenNames() {
        mount()
        for (bad in listOf("", ".", "..", "a/b", "x".repeat(256))) {
            try {
                create("/", bad)
                fail("nom accepté : ${bad.take(20)}")
            } catch (e: FileNotFoundException) { /* attendu */ }
        }
        val long = "x".repeat(255)
        assertEquals("/$long", pathOf(create("/", long)))
        val emoji = "☀ soleil 🚀.txt"
        assertEquals("/$emoji", pathOf(create("/", emoji)))
        assertTrue(emoji in childNames("/"))
    }

    @Test fun deleteRecursive() {
        mount()
        val top = mkdir("/", "haut")
        var n = 0
        var parent = pathOf(top)
        repeat(3) { level ->
            repeat(if (level < 2) 17 else 16) { write(create(parent, "f$it.txt"), pattern(100 + n, n)); n++ }
            parent = pathOf(mkdir(parent, "niveau$level"))
        }
        assertEquals(50, n)
        assertTrue(DocumentsContract.deleteDocument(cr, top))
        assertFalse(exists("/haut"))
        assertFalse("haut" in childNames("/"))
        // la racine ne se supprime pas
        try {
            DocumentsContract.deleteDocument(cr, docUri("/"))
            fail("racine supprimée")
        } catch (e: FileNotFoundException) { /* attendu */ }
    }

    @Test fun renameAndMove() {
        mount()
        val f = create("/", "avant.txt")
        write(f, "contenu".toByteArray())
        val renamed = DocumentsContract.renameDocument(cr, f, "après.txt")!!
        assertEquals("/après.txt", pathOf(renamed))
        assertFalse(exists("/avant.txt"))
        create("/", "pris.txt")
        try {
            DocumentsContract.renameDocument(cr, renamed, "pris.txt")
            fail("renommage sur un nom pris")
        } catch (e: FileNotFoundException) {
            assertEquals("Un élément porte déjà ce nom", e.message)
        }
        try {
            DocumentsContract.renameDocument(cr, renamed, "a/b")
            fail("nom invalide accepté")
        } catch (e: FileNotFoundException) { /* attendu */ }

        val d = mkdir("/", "cible")
        create(pathOf(d), "après.txt") // conflit : le déplacé prend le nom suivant
        val moved = DocumentsContract.moveDocument(cr, renamed, docUri("/"), d)!!
        assertEquals("/cible/après (1).txt", pathOf(moved))
        assertEquals("contenu", String(read(moved)))
        assertFalse(exists("/après.txt"))
        // un dossier ne se déplace pas dans lui-même
        val sub = mkdir(pathOf(d), "sous")
        try {
            DocumentsContract.moveDocument(cr, d, docUri("/"), sub)
            fail("dossier déplacé dans son descendant")
        } catch (e: FileNotFoundException) { /* attendu */ }
    }

    @Test fun copyFileAndDir() {
        mount()
        val src = mkdir("/", "source")
        val big = pattern((2 shl 20) + 123)
        write(create(pathOf(src), "gros.bin"), big)
        val inner = mkdir(pathOf(src), "dedans")
        write(create(pathOf(inner), "petit.txt"), "petit".toByteArray())
        mkdir(pathOf(inner), "vide")

        val fileCopy = DocumentsContract.copyDocument(cr, docUri("/source/gros.bin"), docUri("/source"))!!
        assertEquals("/source/gros (1).bin", pathOf(fileCopy))
        assertArrayEquals(big, read(fileCopy))

        val dst = mkdir("/", "dest")
        val dirCopy = DocumentsContract.copyDocument(cr, src, dst)!!
        assertEquals("/dest/source", pathOf(dirCopy))
        assertArrayEquals(big, read(docUri("/dest/source/gros.bin")))
        assertEquals("petit", String(read(docUri("/dest/source/dedans/petit.txt"))))
        assertTrue(exists("/dest/source/dedans/vide"))
        // la source est intacte
        assertArrayEquals(big, read(docUri("/source/gros.bin")))
        for (target in listOf(src, inner)) {
            try {
                DocumentsContract.copyDocument(cr, src, target)
                fail("dossier copié dans lui-même")
            } catch (e: FileNotFoundException) { /* attendu */ }
        }
    }

    @Test fun readOnlyVolumeRefusesWrite() {
        mount(copy = "provider-lecture.img", writable = false)
        assertEquals(0, flags("/"))
        try {
            DocumentsContract.createDocument(cr, docUri("/"), "text/plain", "non.txt")
            fail("création sur un volume en lecture seule")
        } catch (e: FileNotFoundException) {
            assertTrue(e.message, e.message!!.startsWith("Volume en lecture seule : "))
        }
        for (mode in listOf("w", "rw", "wt", "rwt", "wa")) { // ouverture en écriture : message exact
            try {
                cr.openFileDescriptor(docUri("/hello.txt"), mode)?.close()
                fail("mode $mode accepté")
            } catch (e: FileNotFoundException) {
                assertEquals("Volume en lecture seule : ouvert en lecture seule à votre demande", e.message)
            }
        }
        cr.query(DocumentsContract.buildChildDocumentsUri(auth, "$vol:/"), null, null, null, null)!!.use { c ->
            while (c.moveToNext()) assertEquals(0, c.getInt(c.getColumnIndexOrThrow(Document.COLUMN_FLAGS)))
        }
        cr.query(DocumentsContract.buildRootsUri(auth), null, null, null, null)!!.use { c ->
            assertTrue(c.moveToFirst())
            assertEquals(0, c.getInt(c.getColumnIndexOrThrow(Root.COLUMN_FLAGS)) and Root.FLAG_SUPPORTS_CREATE)
            assertEquals(
                "Lecture seule : ouvert en lecture seule à votre demande",
                c.getString(c.getColumnIndexOrThrow(Root.COLUMN_SUMMARY)),
            )
        }
    }

    @Test fun readWhileWriting() {
        mount()
        val existing = pattern(4 shl 20, 3)
        val a = create("/", "existant.bin")
        write(a, existing)
        val b = create("/", "nouveau.bin")
        val big = pattern(20 shl 20, 9)
        val failure = AtomicReference<Throwable?>(null)
        val reader = Thread {
            try {
                repeat(5) { assertArrayEquals(existing, read(a)) }
            } catch (t: Throwable) {
                failure.compareAndSet(null, t)
            }
        }.apply { start() }
        val writer = Thread {
            try {
                cr.openOutputStream(b, "w")!!.use { out ->
                    var off = 0
                    while (off < big.size) {
                        val len = minOf(256 * 1024, big.size - off)
                        out.write(big, off, len)
                        off += len
                    }
                }
            } catch (t: Throwable) {
                failure.compareAndSet(null, t)
            }
        }.apply { start() }
        reader.join(120_000)
        writer.join(120_000)
        assertFalse(reader.isAlive || writer.isAlive)
        failure.get()?.let { throw it }
        assertArrayEquals(big, read(b))
    }

    @Test fun diskFullFailsCleanly() {
        mount(fixture = "ext4-rw-1k.img", copy = "provider-plein.img")
        val uri = create("/", "plein.bin")
        val chunk = pattern(1 shl 20)
        var failed: IOException? = null
        var written = 0L
        try {
            cr.openOutputStream(uri, "w")!!.use { out ->
                while (written < (128L shl 20)) { // l'image fait 64 Mio : l'erreur doit venir avant
                    out.write(chunk)
                    written += chunk.size
                }
            }
        } catch (e: IOException) {
            failed = e
        }
        assertNotNull("aucune erreur après $written octets", failed)
        val text = generateSequence<Throwable>(failed) { it.cause }.joinToString(" / ") { it.toString() }
        assertTrue(
            "pas une erreur de disque plein : $text",
            text.contains("ENOSPC") || text.contains("No space left") || text.contains("plus d'espace disponible"),
        )
        assertTrue(DocumentsContract.deleteDocument(cr, uri))
        assertFalse(exists("/plein.bin"))
        // le volume reste utilisable
        val after = create("/", "après.txt")
        write(after, "ok".toByteArray())
        assertEquals("ok", String(read(after)))
    }

    @Test fun writeErrnoMapping() {
        val m = FluLuksDocumentsProvider.Companion
        assertEquals(OsConstants.ENOSPC, m.errnoFor(EngineError.FL_ENOSPC))
        assertEquals(OsConstants.EROFS, m.errnoFor(EngineError.FL_EROFS))
        assertEquals(OsConstants.ENXIO, m.errnoFor(EngineError.FL_ENODEV))
        assertEquals(OsConstants.EIO, m.errnoFor(EngineError.FL_EIO))
        assertEquals(OsConstants.EIO, m.errnoFor(EngineError.FL_ECORRUPT))
    }

    @Test fun crossVolumeUnsupported() {
        mount()
        val f = create("/", "x.txt")
        try {
            DocumentsContract.moveDocument(cr, f, docUri("/"), DocumentsContract.buildDocumentUri(auth, "autre:/"))
            fail("déplacement entre volumes")
        } catch (e: UnsupportedOperationException) { /* attendu */ }
        try {
            DocumentsContract.copyDocument(cr, f, DocumentsContract.buildDocumentUri(auth, "autre:/"))
            fail("copie entre volumes")
        } catch (e: UnsupportedOperationException) { /* attendu */ }
    }
}

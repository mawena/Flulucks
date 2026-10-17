package com.mawena.fluluks

import android.content.ContentResolver
import android.database.Cursor
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import android.provider.DocumentsContract.Root
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.volumes.VolumeManager
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.FileInputStream
import java.io.FileNotFoundException

@RunWith(AndroidJUnit4::class)
class ProviderTest {
    private val ctx get() = InstrumentationRegistry.getInstrumentation().targetContext
    private val cr: ContentResolver get() = ctx.contentResolver
    private val auth = VolumeManager.AUTHORITY
    private var diskId: String? = null
    private lateinit var vol: String

    @Before fun setUp() {
        VolumeManager.init(ctx)
        VolumeManager.ejectAll()
        waitFor("volumes vides") { VolumeManager.volumes.value.isEmpty() }
        val id = VolumeManager.addImageDiskForTests(Fixtures.file("gpt-luks2.img").absolutePath)
        diskId = id
        val part = VolumeManager.disks.value.first { it.id == id }.scan!!.parts.first { it.luks != null }
        vol = runBlocking { VolumeManager.unlock(id, part.index, Fixtures.bytes("pass.txt")) }.id
    }

    @After fun tearDown() {
        VolumeManager.ejectAll()
        diskId?.let { VolumeManager.removeDiskForTests(it) }
        waitFor("volumes vides") { VolumeManager.volumes.value.isEmpty() }
    }

    private fun waitFor(what: String, timeoutMs: Long = 10_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé : $what" }
            Thread.sleep(50)
        }
    }

    private fun docUri(path: String): Uri = DocumentsContract.buildDocumentUri(auth, "$vol:$path")

    private fun children(path: String): Cursor =
        cr.query(DocumentsContract.buildChildDocumentsUri(auth, "$vol:$path"), null, null, null, null)!!

    private fun pattern(i: Long) = ((i * 31 + 7) and 0xFF).toByte()

    @Test fun rootsListed() {
        cr.query(DocumentsContract.buildRootsUri(auth), null, null, null, null)!!.use { c ->
            assertEquals(1, c.count)
            c.moveToFirst()
            assertEquals("Chiffré (LUKS2) — lecture seule", c.getString(c.getColumnIndexOrThrow(Root.COLUMN_SUMMARY)))
            assertEquals("$vol:/", c.getString(c.getColumnIndexOrThrow(Root.COLUMN_DOCUMENT_ID)))
            assertEquals(
                Root.FLAG_LOCAL_ONLY or Root.FLAG_SUPPORTS_IS_CHILD,
                c.getInt(c.getColumnIndexOrThrow(Root.COLUMN_FLAGS)),
            )
        }
    }

    @Test fun rootsProjectionHonored() {
        cr.query(DocumentsContract.buildRootsUri(auth), arrayOf(Root.COLUMN_TITLE), null, null, null)!!.use { c ->
            assertEquals(1, c.columnCount)
            assertEquals(1, c.count)
        }
    }

    @Test fun childrenOfRoot() {
        children("/").use { c ->
            val names = HashMap<String, Pair<String, Long>>()
            while (c.moveToNext()) {
                names[c.getString(c.getColumnIndexOrThrow(Document.COLUMN_DISPLAY_NAME))] =
                    c.getString(c.getColumnIndexOrThrow(Document.COLUMN_MIME_TYPE)) to
                        c.getLong(c.getColumnIndexOrThrow(Document.COLUMN_SIZE))
                assertEquals(0, c.getInt(c.getColumnIndexOrThrow(Document.COLUMN_FLAGS)))
            }
            assertEquals("text/plain" to 16L, names["hello.txt"])
            assertEquals(Document.MIME_TYPE_DIR, names["sous-dossier"]?.first)
        }
    }

    @Test fun manyChildren() {
        children("/beaucoup").use { assertEquals(300, it.count) }
    }

    @Test fun queryDocumentRootAndFile() {
        cr.query(docUri("/"), null, null, null, null)!!.use { c ->
            assertTrue(c.moveToFirst())
            assertEquals(Document.MIME_TYPE_DIR, c.getString(c.getColumnIndexOrThrow(Document.COLUMN_MIME_TYPE)))
        }
        cr.query(docUri("/hello.txt"), arrayOf(Document.COLUMN_SIZE), null, null, null)!!.use { c ->
            assertTrue(c.moveToFirst())
            assertEquals(1, c.columnCount)
            assertEquals(16L, c.getLong(0))
        }
    }

    @Test fun readFile() {
        val data = cr.openFileDescriptor(docUri("/sous-dossier/données.bin"), "r")!!.use { pfd ->
            FileInputStream(pfd.fileDescriptor).readBytes()
        }
        assertEquals(1 shl 20, data.size)
        for (i in data.indices) assertEquals("octet $i", pattern(i.toLong()), data[i])
        val hello = cr.openFileDescriptor(docUri("/hello.txt"), "r")!!.use {
            FileInputStream(it.fileDescriptor).readBytes()
        }
        assertEquals("Bonjour FluLuks\n", String(hello, Charsets.UTF_8))
    }

    @Test fun emptyFile() {
        cr.openFileDescriptor(docUri("/vide.txt"), "r")!!.use {
            assertEquals(0, FileInputStream(it.fileDescriptor).readBytes().size)
        }
    }

    @Test fun seekRead() {
        cr.openFileDescriptor(docUri("/gros.bin"), "r")!!.use { pfd ->
            val ch = FileInputStream(pfd.fileDescriptor).channel
            ch.position(4_000_000)
            val buf = java.nio.ByteBuffer.allocate(4096)
            while (buf.hasRemaining()) assertTrue(ch.read(buf) > 0)
            for (i in 0 until 4096) assertEquals("octet $i", pattern(4_000_000L + i), buf.array()[i])
            // lecture à cheval sur la fin : octets réellement présents seulement
            ch.position(5L * 1024 * 1024 - 100)
            val tail = java.nio.ByteBuffer.allocate(4096)
            var n = 0
            while (true) { val r = ch.read(tail); if (r <= 0) break; n += r }
            assertEquals(100, n)
        }
    }

    @Test fun sparseFileEnd() {
        cr.openFileDescriptor(docUri("/clairsemé.bin"), "r")!!.use { pfd ->
            val ch = FileInputStream(pfd.fileDescriptor).channel
            val end = "FIN-FLULUKS".toByteArray()
            ch.position(5L * 1024 * 1024 * 1024 - end.size)
            val buf = java.nio.ByteBuffer.allocate(end.size)
            while (buf.hasRemaining()) assertTrue(ch.read(buf) > 0)
            assertArrayEquals(end, buf.array())
        }
    }

    @Test fun writeRefused() {
        for (mode in listOf("w", "rw", "wt", "rwt", "wa", "r+", "", "rx")) {
            try {
                cr.openFileDescriptor(docUri("/hello.txt"), mode)?.close()
                fail("mode $mode accepté")
            } catch (e: FileNotFoundException) { /* attendu */ }
        }
    }

    @Test fun directoryAndMissingRefused() {
        for (p in listOf("/sous-dossier", "/", "/absent.txt")) {
            try {
                cr.openFileDescriptor(docUri(p), "r")?.close()
                fail("$p ouvert")
            } catch (e: FileNotFoundException) { /* attendu */ }
        }
    }

    @Test fun emojiDocument() {
        children("/").use { c ->
            var found: String? = null
            while (c.moveToNext()) {
                if (c.getString(c.getColumnIndexOrThrow(Document.COLUMN_DISPLAY_NAME)) == "accentué é ☀.txt")
                    found = c.getString(c.getColumnIndexOrThrow(Document.COLUMN_DOCUMENT_ID))
            }
            assertEquals("$vol:/accentué é ☀.txt", found)
        }
        cr.openFileDescriptor(docUri("/accentué é ☀.txt"), "r")!!.use { assertTrue(it.statSize >= 0) }
    }

    @Test fun unknownDocumentAfterRestart() {
        val uri = DocumentsContract.buildDocumentUri(auth, "v999:/hello.txt")
        try { cr.openFileDescriptor(uri, "r")?.close(); fail() } catch (e: FileNotFoundException) { }
        // Binder ne transporte pas FileNotFoundException pour les requêtes : ContentResolver rend null
        assertNull(cr.query(uri, null, null, null, null))
        assertNull(cr.query(DocumentsContract.buildChildDocumentsUri(auth, "v999:/"), null, null, null, null))
    }

    @Test fun malformedIdsRejected() {
        for (id in listOf("$vol:../x", "$vol:rel", "$vol:/a//b", "rien", "$vol:/hello.txt/", "$vol:/a\u0000b")) {
            try {
                cr.openFileDescriptor(DocumentsContract.buildDocumentUri(auth, id), "r")?.close()
                fail("id accepté : $id")
            } catch (e: FileNotFoundException) { }
        }
    }

    @Test fun isChildDocumentWholeComponents() {
        val c = cr.acquireContentProviderClient(auth)!!
        try {
            val call = { parent: String, doc: String ->
                DocumentsContract.isChildDocument(cr, DocumentsContract.buildDocumentUri(auth, "$vol:$parent"), DocumentsContract.buildDocumentUri(auth, "$vol:$doc"))
            }
            assertTrue(call("/sous-dossier", "/sous-dossier/données.bin"))
            assertTrue(!call("/sous", "/sous-dossier/données.bin"))
            assertTrue(call("/", "/hello.txt"))
        } finally { c.close() }
    }

    @Test fun ejectRemovesRoot() {
        VolumeManager.ejectAll()
        waitFor("volumes vides") { VolumeManager.volumes.value.isEmpty() }
        cr.query(DocumentsContract.buildRootsUri(auth), null, null, null, null)!!.use { assertEquals(0, it.count) }
    }

    @Test fun readAfterEjectFailsCleanly() {
        val pfd = cr.openFileDescriptor(docUri("/gros.bin"), "r")!!
        VolumeManager.ejectAll()
        waitFor("volumes vides") { VolumeManager.volumes.value.isEmpty() }
        try {
            val buf = ByteArray(4096)
            try { FileInputStream(pfd.fileDescriptor).read(buf); fail("lecture réussie après éjection") }
            catch (e: java.io.IOException) { /* attendu */ }
        } finally { pfd.close() }
        // le processus tient toujours
        cr.query(DocumentsContract.buildRootsUri(auth), null, null, null, null)!!.use { assertEquals(0, it.count) }
    }
}

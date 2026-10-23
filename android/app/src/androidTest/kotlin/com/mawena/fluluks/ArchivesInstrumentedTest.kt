package com.mawena.fluluks

import android.os.Environment
import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Archives
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.files.TransferState
import com.mawena.fluluks.files.Transfers
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import java.util.Random
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Archives sur le Pixel, avec le vrai moteur (libarchive) : un dossier d'une image de disque compressé, puis
 * extrait sur une autre image et dans le stockage du téléphone (par descripteur) ; un ZIP écrit sur le téléphone
 * relu par java.util.zip ; archives 7z, RAR5 et ZIP hostile de la suite de tests extraites. Les images sont des
 * copies de `ext4-rw-reel.img` dans le cache de l'application ; le dossier Download/portail-tests/ est supprimé à
 * la fin.
 */
@RunWith(AndroidJUnit4::class)
class ArchivesInstrumentedTest {
    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx get() = instr.targetContext
    private val diskIds = ArrayList<String>()
    private lateinit var volA: String
    private lateinit var volB: String

    @Suppress("DEPRECATION")
    private val testDir = File(Environment.getExternalStorageDirectory(), "Download/portail-tests")
    private val phoneDir = "/Download/portail-tests"

    private val texte = "Portail, archive de test : été, œuvre, ünïcödé.\n".repeat(30_000).toByteArray()
    private val alea = ByteArray(3 shl 20).also { Random(42).nextBytes(it) }
    private val photo = ByteArray(200_000) { (it * 31 + it / 977).toByte() }
    private val petit = "é€".toByteArray()

    private fun shell(cmd: String) =
        ParcelFileDescriptor.AutoCloseInputStream(instr.uiAutomation.executeShellCommand(cmd)).use { it.readBytes() }

    private fun mountCopy(name: String): String {
        val img = File(ctx.cacheDir, name)
        Fixtures.file("ext4-rw-reel.img").copyTo(img, overwrite = true)
        val id = VolumeManager.addImageDiskForTests(img.absolutePath, readOnly = false)
        diskIds.add(id)
        val part = VolumeManager.disks.value.first { it.id == id }.scan!!.parts.first()
        return runBlocking { VolumeManager.mountPlain(id, part.index, true) }.id
    }

    @Before fun setUp() {
        shell("appops set ${ctx.packageName} MANAGE_EXTERNAL_STORAGE allow")
        Sources.init(ctx)
        VolumeManager.init(ctx)
        VolumeManager.ejectAll()
        waitFor { VolumeManager.volumes.value.isEmpty() }
        assumeTrue("accès à tous les fichiers refusé", Sources.hasAllFilesAccess())
        testDir.deleteRecursively()
        assertTrue(testDir.mkdirs())
        volA = mountCopy("archives-a.img")
        volB = mountCopy("archives-b.img")
    }

    @After fun tearDown() {
        testDir.deleteRecursively()
        VolumeManager.ejectAll()
        waitFor { VolumeManager.volumes.value.isEmpty() }
        diskIds.forEach { VolumeManager.removeDiskForTests(it) }
        File(ctx.cacheDir, "archives-a.img").delete()
        File(ctx.cacheDir, "archives-b.img").delete()
    }

    private fun waitFor(timeoutMs: Long = 10_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé" }
            Thread.sleep(20)
        }
    }

    private fun run(start: () -> Unit): TransferState {
        start()
        waitFor(120_000) {
            Transfers.state.value?.finished == true &&
                listOf(Sources.PHONE, volA, volB).none(Transfers::isBusy)
        }
        return Transfers.state.value!!
    }

    private fun readDisk(vol: String, path: String) = Files.source(vol).openRead(path).use { it.readBytes() }

    /** Arborescence de référence sous [dir] d'une source : été.txt, aléa.bin, photo.jpg, sous/petit.txt, vide/. */
    private fun writeTree(vol: String, dir: String) {
        val s = Files.source(vol)
        val src = s.mkdir(dir, "src")
        s.createFile(src, "été.txt").second.use { it.write(texte) }
        s.createFile(src, "aléa.bin").second.use { it.write(alea) }
        s.createFile(src, "photo.jpg").second.use { it.write(photo) }
        s.createFile(s.mkdir(src, "sous"), "petit.txt").second.use { it.write(petit) }
        s.mkdir(src, "vide")
    }

    private fun checkTree(read: (String) -> ByteArray, isDir: (String) -> Boolean, base: String) {
        assertArrayEquals(texte, read("$base/été.txt"))
        assertArrayEquals(alea, read("$base/aléa.bin"))
        assertArrayEquals(photo, read("$base/photo.jpg"))
        assertArrayEquals(petit, read("$base/sous/petit.txt"))
        assertTrue(isDir("$base/vide"))
    }

    @Test fun compressDiskExtractOnOtherDiskAndPhone() {
        writeTree(volA, "/")
        val c = run { Archives.startCompress(volA, listOf("/src"), "/", null) }
        assertNull(c.error)
        assertEquals("compress", c.kind)
        assertEquals((texte.size + alea.size + photo.size + petit.size).toLong(), c.doneBytes)
        assertTrue(Files.source(volA).stat("/src.zip")!!.size in 1L until c.doneBytes) // texte compressé

        // disque → autre disque, dans un nouveau dossier « src »
        val x = run { Archives.startExtract(volA, "/src.zip", volB, "/", true) }
        assertNull(x.error)
        assertEquals("extract", x.kind)
        assertEquals(4, x.fileCount)
        checkTree({ readDisk(volB, it) }, { Files.source(volB).stat(it)?.type == FileType.DIR }, "/src/src")

        // disque → téléphone (descripteur), dans le dossier choisi
        val y = run { Archives.startExtract(volA, "/src.zip", Sources.PHONE, phoneDir, false) }
        assertNull(y.error)
        checkTree({ File(testDir, it).readBytes() }, { File(testDir, it).isDirectory }, "src")
    }

    @Test fun zipWrittenOnPhoneReadByJavaZip() {
        writeTree(Sources.PHONE, phoneDir)
        val c = run { Archives.startCompress(Sources.PHONE, listOf("$phoneDir/src"), phoneDir, "archive") }
        assertNull(c.error)
        ZipFile(File(testDir, "archive.zip")).use { z ->
            val names = z.entries().toList().map { it.name }.toSet()
            assertEquals(setOf("src/", "src/été.txt", "src/aléa.bin", "src/photo.jpg", "src/sous/", "src/sous/petit.txt", "src/vide/"), names)
            fun bytes(n: String) = z.getInputStream(z.getEntry(n)).use { it.readBytes() }
            assertArrayEquals(texte, bytes("src/été.txt"))
            assertArrayEquals(alea, bytes("src/aléa.bin"))
            assertArrayEquals(photo, bytes("src/photo.jpg"))
            assertArrayEquals(petit, bytes("src/sous/petit.txt"))
            assertEquals(ZipEntry.STORED, z.getEntry("src/photo.jpg").method)
            assertEquals(ZipEntry.DEFLATED, z.getEntry("src/été.txt").method)
        }
        // téléphone → disque
        val x = run { Archives.startExtract(Sources.PHONE, "$phoneDir/archive.zip", volB, "/", true) }
        assertNull(x.error)
        checkTree({ readDisk(volB, it) }, { Files.source(volB).stat(it)?.type == FileType.DIR }, "/archive/src")
    }

    @Test fun extracts7zRar5AndHostileZip() {
        Fixtures.file("test_read_format_7zip_lzma2.7z").copyTo(File(testDir, "a.7z"))
        Fixtures.file("test_read_format_rar5_multiple_files.rar").copyTo(File(testDir, "b.rar"))
        Fixtures.file("hostile.zip").copyTo(File(testDir, "h.zip"))

        assertNull(run { Archives.startExtract(Sources.PHONE, "$phoneDir/a.7z", Sources.PHONE, phoneDir, true) }.error)
        assertEquals(2844L, File(testDir, "a/file1").length())

        assertNull(run { Archives.startExtract(Sources.PHONE, "$phoneDir/b.rar", Sources.PHONE, phoneDir, true) }.error)
        for (i in 1..4) {
            val b = File(testDir, "b/test$i.bin").readBytes()
            assertEquals(4096, b.size)
            // verify_data de libarchive : entiers 32 bits petit-boutistes k² - 3k + 1 + i (bornés à 0)
            for (k in 1..1024) {
                val v = maxOf(0, k * k - 3 * k + 1 + i)
                val o = 4 * (k - 1)
                val lu = (b[o].toInt() and 0xFF) or (b[o + 1].toInt() and 0xFF shl 8) or
                    (b[o + 2].toInt() and 0xFF shl 16) or (b[o + 3].toInt() and 0xFF shl 24)
                assertEquals("test$i.bin[$k]", v, lu)
            }
        }

        // « ../x », « /abs » et « a/../../x » ignorés ; « a\b » est lu « a/b » par libarchive
        val h = run { Archives.startExtract(Sources.PHONE, "$phoneDir/h.zip", Sources.PHONE, phoneDir, true) }
        assertEquals(Archives.msgSkipped(3), h.error)
        assertEquals(setOf("a", "ok.txt"), File(testDir, "h").list()!!.toSet())
        assertArrayEquals("hostile\n".toByteArray(), File(testDir, "h/a/b").readBytes())
        assertEquals(setOf("a.7z", "b.rar", "h.zip", "a", "b", "h"), testDir.list()!!.toSet())
    }
}

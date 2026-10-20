package com.mawena.fluluks

import android.content.Intent
import android.net.Uri
import android.os.Environment
import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Gestionnaire de fichiers sur le Pixel : stockage du téléphone (dossier jetable Download/portail-tests/,
 * supprimé à la fin) et copie de `ext4-rw-reel.img` montée en écriture. L'accès à tous les fichiers est
 * accordé par appops avant les tests.
 */
@RunWith(AndroidJUnit4::class)
class FilesInstrumentedTest {
    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx get() = instr.targetContext
    private var diskId: String? = null
    private lateinit var vol: String

    @Suppress("DEPRECATION")
    private val testDir = File(Environment.getExternalStorageDirectory(), "Download/portail-tests")
    private val phoneDir = "/Download/portail-tests"

    private fun shell(cmd: String) =
        ParcelFileDescriptor.AutoCloseInputStream(instr.uiAutomation.executeShellCommand(cmd)).use { it.readBytes() }

    @Before fun setUp() {
        shell("appops set ${ctx.packageName} MANAGE_EXTERNAL_STORAGE allow")
        Sources.init(ctx)
        VolumeManager.init(ctx)
        VolumeManager.ejectAll()
        waitFor { VolumeManager.volumes.value.isEmpty() }
        assumeTrue("accès à tous les fichiers refusé", Sources.hasAllFilesAccess())
        testDir.deleteRecursively()
        assertTrue(testDir.mkdirs())
        val img = File(ctx.cacheDir, "fichiers-ecriture.img")
        Fixtures.file("ext4-rw-reel.img").copyTo(img, overwrite = true)
        val id = VolumeManager.addImageDiskForTests(img.absolutePath, readOnly = false)
        diskId = id
        val part = VolumeManager.disks.value.first { it.id == id }.scan!!.parts.first()
        vol = runBlocking { VolumeManager.mountPlain(id, part.index, true) }.id
    }

    @After fun tearDown() {
        testDir.deleteRecursively()
        VolumeManager.ejectAll()
        waitFor { VolumeManager.volumes.value.isEmpty() }
        diskId?.let { VolumeManager.removeDiskForTests(it) }
    }

    private fun waitFor(timeoutMs: Long = 10_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé" }
            Thread.sleep(50)
        }
    }

    private fun inner(chooser: Intent): Intent {
        assertEquals(Intent.ACTION_CHOOSER, chooser.action)
        @Suppress("DEPRECATION")
        return chooser.getParcelableExtra(Intent.EXTRA_INTENT)!!
    }

    private fun read(uri: Uri): ByteArray = ctx.contentResolver.openInputStream(uri)!!.use { it.readBytes() }

    private fun writeDisk(name: String, data: ByteArray): String {
        val (path, out) = Files.source(vol).createFile("/", name)
        out.use { it.write(data) }
        return path
    }

    @Test fun viewIntentUriReadable() {
        val data = ByteArray(300_000) { (it * 13).toByte() }
        File(testDir, "photo.jpg").writeBytes(data)
        val local = inner(Files.viewIntent(Sources.PHONE, "$phoneDir/photo.jpg"))
        assertEquals(Intent.ACTION_VIEW, local.action)
        assertEquals("image/jpeg", local.type)
        assertTrue(local.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0)
        assertEquals(Files.FILE_AUTHORITY, local.data!!.authority)
        assertArrayEquals(data, read(local.data!!))

        val diskPath = writeDisk("film.mp4", data)
        val disk = inner(Files.viewIntent(vol, diskPath))
        assertEquals("video/mp4", disk.type)
        assertEquals(VolumeManager.AUTHORITY, disk.data!!.authority)
        assertTrue(disk.flags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION != 0)
        assertArrayEquals(data, read(disk.data!!))
    }

    @Test fun shareIntentHasAllUris() {
        File(testDir, "a.jpg").writeBytes(byteArrayOf(1))
        File(testDir, "b.png").writeBytes(byteArrayOf(2))
        val many = inner(Files.shareIntent(Sources.PHONE, listOf("$phoneDir/a.jpg", "$phoneDir/b.png")))
        assertEquals(Intent.ACTION_SEND_MULTIPLE, many.action)
        assertEquals("*/*", many.type)
        @Suppress("DEPRECATION")
        val uris = many.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)!!
        assertEquals(2, uris.size)
        assertEquals(2, many.clipData!!.itemCount)
        assertArrayEquals(byteArrayOf(2), read(uris[1]))

        val one = inner(Files.shareIntent(vol, listOf(writeDisk("x.txt", byteArrayOf(9)))))
        assertEquals(Intent.ACTION_SEND, one.action)
        assertEquals("text/plain", one.type)
        @Suppress("DEPRECATION")
        assertArrayEquals(byteArrayOf(9), read(one.getParcelableExtra(Intent.EXTRA_STREAM)!!))
    }

    @Test fun shareRefusesDirectory() {
        File(testDir, "d").mkdir()
        File(testDir, "f.txt").writeText("f")
        try {
            Files.shareIntent(Sources.PHONE, listOf("$phoneDir/f.txt", "$phoneDir/d"))
            fail("accepté")
        } catch (e: FilesException) {
            assertEquals(Files.MSG_SHARE_DIR, e.message)
        }
    }

    @Test fun listsPhoneDirectory() {
        File(testDir, "b.txt").writeText("b")
        File(testDir, "A").mkdir()
        assertEquals(listOf("A", "b.txt"), Files.list(Sources.PHONE, phoneDir, false).map { it.name })
    }
}

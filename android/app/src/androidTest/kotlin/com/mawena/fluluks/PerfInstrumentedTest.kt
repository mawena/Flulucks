package com.mawena.fluluks

import android.os.Environment
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.files.TransferState
import com.mawena.fluluks.files.Transfers
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import java.io.FileInputStream
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Performances du § 5 de la spec, mesurées sur le Pixel (phase 7). Chaque mesure est écrite dans le journal
 * sous l'étiquette `PortailPerf` (recueillie par tools/android/run_phase7_tests.sh). Disque : copie de
 * `ext4-rw-reel.img` (256 Mio) montée en écriture ; téléphone : Download/portail-tests/, supprimé à la fin.
 * Les mesures sur image ne disent rien du débit USB : le vrai disque est mesuré par l'utilisateur (T10).
 */
@RunWith(AndroidJUnit4::class)
class PerfInstrumentedTest {
    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx get() = instr.targetContext
    private var diskId: String? = null
    private lateinit var vol: String
    private val mib = 1 shl 20

    @Suppress("DEPRECATION")
    private val testDir = File(Environment.getExternalStorageDirectory(), "Download/portail-tests")
    private val phoneDir = "/Download/portail-tests"

    private fun shell(cmd: String) =
        ParcelFileDescriptor.AutoCloseInputStream(instr.uiAutomation.executeShellCommand(cmd)).use { it.readBytes() }

    private fun log(msg: String) = Log.i("PortailPerf", msg)

    @Before fun setUp() {
        shell("appops set ${ctx.packageName} MANAGE_EXTERNAL_STORAGE allow")
        Sources.init(ctx)
        VolumeManager.init(ctx)
        VolumeManager.ejectAll()
        waitFor { VolumeManager.volumes.value.isEmpty() }
        assumeTrue("accès à tous les fichiers refusé", Sources.hasAllFilesAccess())
        testDir.deleteRecursively()
        assertTrue(testDir.mkdirs())
        val img = File(ctx.cacheDir, "perf.img")
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
        File(ctx.cacheDir, "perf.img").delete()
    }

    private fun waitFor(timeoutMs: Long = 60_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé" }
            Thread.sleep(20)
        }
    }

    private fun transfer(srcId: String, path: String, dstId: String, dir: String): TransferState {
        Transfers.start(srcId, listOf(path), dstId, dir, false)
        waitFor(120_000) { Transfers.state.value?.finished == true && !Transfers.isBusy(srcId) }
        return Transfers.state.value!!
    }

    private fun rate(bytes: Long, ms: Long) = "%.1f Mo/s".format(bytes / 1000.0 / ms.coerceAtLeast(1))

    /** § 5 : listage d'un dossier de 1 000 fichiers en moins de 2 s (disque). */
    @Test fun list1000FilesUnder2s() {
        val d = Files.source(vol)
        d.mkdir("/", "mille")
        for (i in 0 until 1000) d.createFile("/mille", "f%04d.txt".format(i)).second.use { it.write(byteArrayOf(1)) }
        val t0 = SystemClock.elapsedRealtime()
        val l = Files.list(vol, "/mille", false)
        val ms = SystemClock.elapsedRealtime() - t0
        log("listage disque, 1 000 fichiers : $ms ms")
        assertEquals(1000, l.size)
        assertTrue("$ms ms", ms < 2000)
    }

    /** § 5, appliqué au téléphone : même dossier de 1 000 fichiers. */
    @Test fun listPhoneFolder1000Under2s() {
        val d = File(testDir, "mille").apply { mkdirs() }
        for (i in 0 until 1000) File(d, "f%04d.txt".format(i)).writeBytes(byteArrayOf(1))
        val t0 = SystemClock.elapsedRealtime()
        val l = Files.list(Sources.PHONE, "$phoneDir/mille", false)
        val ms = SystemClock.elapsedRealtime() - t0
        log("listage téléphone, 1 000 fichiers : $ms ms")
        assertEquals(1000, l.size)
        assertTrue("$ms ms", ms < 2000)
    }

    /** Débit des copies téléphone → disque → téléphone (64 Mio), sans seuil : le § 5 n'en fixe pas. */
    @Test fun copyThroughput() {
        val n = 64L * mib // nom propre au test : l'image contient déjà un gros.bin
        File(testDir, "perf-64.bin").outputStream().use { o -> val b = ByteArray(mib) { it.toByte() }; repeat(64) { o.write(b) } }
        val aller = transfer(Sources.PHONE, "$phoneDir/perf-64.bin", vol, "/")
        assertNull(aller.error)
        log("copie téléphone → disque, 64 Mio : ${aller.elapsedMs} ms, ${rate(n, aller.elapsedMs)}")
        File(testDir, "retour").mkdirs()
        val retour = transfer(vol, "/perf-64.bin", Sources.PHONE, "$phoneDir/retour")
        assertNull(retour.error)
        log("copie disque → téléphone, 64 Mio : ${retour.elapsedMs} ms, ${rate(n, retour.elapsedMs)}")
        assertEquals(n, File(testDir, "retour/perf-64.bin").length())
    }

    /** § 5 : lecture séquentielle d'au moins 20 Mo/s, ici par le provider (comme VLC), sur image. */
    @Test fun sequentialReadAtLeast20MBps() {
        val n = 64L * mib
        val src = Files.source(vol)
        src.createFile("/", "lecture.bin").second.use { o -> val b = ByteArray(mib) { (it * 7).toByte() }; repeat(64) { o.write(b) } }
        val uri = src.contentUri("/lecture.bin")
        val buf = ByteArray(mib)
        var total = 0L
        val t0 = SystemClock.elapsedRealtime()
        ctx.contentResolver.openFileDescriptor(uri, "r")!!.use { pfd ->
            FileInputStream(pfd.fileDescriptor).use { inp ->
                while (true) {
                    val k = inp.read(buf)
                    if (k < 0) break
                    total += k
                }
            }
        }
        val ms = SystemClock.elapsedRealtime() - t0
        log("lecture séquentielle par le provider, 64 Mio : $ms ms, ${rate(total, ms)}")
        assertEquals(n, total)
        assertTrue(rate(total, ms), total / 1000.0 / ms.coerceAtLeast(1) >= 20.0)
    }
}

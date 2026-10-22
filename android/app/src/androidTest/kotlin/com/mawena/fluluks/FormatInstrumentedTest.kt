package com.mawena.fluluks

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.NativeEngine
import com.mawena.fluluks.engine.OpenFlags
import com.mawena.fluluks.volumes.FormatOptions
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import java.io.RandomAccessFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Formatage sur le Pixel (phase 8, T6), sur une image de 128 Mio dans le cache de l'application (jamais un
 * disque) : exFAT en clair puis ext4 chiffré LUKS2 (Argon2 réglé comme dans l'application), chacun relu,
 * monté en écriture et écrit par le moteur.
 */
@RunWith(AndroidJUnit4::class)
class FormatInstrumentedTest {
    private val ctx get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var img: File
    private var diskId: String? = null

    @Before fun setUp() {
        VolumeManager.init(ctx)
        VolumeManager.ejectAll()
        waitFor { VolumeManager.volumes.value.isEmpty() }
        img = File(ctx.cacheDir, "format.img")
        img.delete()
        RandomAccessFile(img, "rw").use { it.setLength(128L shl 20) }
        diskId = VolumeManager.addImageDiskForTests(img.absolutePath, readOnly = false)
    }

    @After fun tearDown() {
        VolumeManager.ejectAll()
        waitFor { VolumeManager.volumes.value.isEmpty() }
        diskId?.let { VolumeManager.removeDiskForTests(it) }
        img.delete()
    }

    private fun waitFor(timeoutMs: Long = 60_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé" }
            Thread.sleep(20)
        }
    }

    private fun onlyPart() = VolumeManager.disks.value.first { it.id == diskId }.scan!!.parts.single()

    /** Écrit un petit fichier puis un dossier dans le volume monté, par le moteur. */
    private fun write(volId: String) {
        val v = VolumeManager.nativeVolume(volId)
        val f = NativeEngine.openFile(v, "/été.txt".toByteArray(), OpenFlags.RDWR or OpenFlags.CREAT)
        assertTrue("ouverture : $f", f > 0)
        val data = "écrit par Portail".toByteArray()
        assertEquals(data.size, NativeEngine.pwrite(f, 0, data, data.size))
        assertEquals(0, NativeEngine.closeFile(f))
        assertEquals(0, NativeEngine.mkdir(v, "/Photos".toByteArray()))
    }

    @Test fun formatExfatThenEncryptedExt4() = runBlocking {
        val id = diskId!!
        // exFAT en clair, progression publiée
        val seen = async(Dispatchers.Default) {
            withTimeout(60_000) { VolumeManager.formatting.first { it != null && it > 0 } }
        }
        VolumeManager.format(id, FormatOptions("exfat", false, "Clé"), null)
        assertTrue(seen.await()!! > 0)
        assertEquals(null, VolumeManager.formatting.value)
        var part = onlyPart()
        assertEquals("exFAT", part.content)
        assertEquals("Clé", part.label)
        var vol = VolumeManager.mountPlain(id, part.index, true)
        assertTrue(!vol.readOnly)
        write(vol.id)

        // volume ouvert : refus
        try {
            VolumeManager.format(id, FormatOptions("ext4", false, ""), null)
            fail("EngineError attendue")
        } catch (e: EngineError) {
            assertEquals(VolumeManager.MSG_FORMAT_OPEN, e.message)
        }
        VolumeManager.eject(vol.id)
        waitFor { VolumeManager.volumes.value.isEmpty() }

        // ext4 chiffré : déverrouillé avec la phrase, écrit
        val pass = "phrase de passe".toByteArray()
        VolumeManager.format(id, FormatOptions("ext4", true, "Données"), pass)
        assertTrue(pass.all { it == 0.toByte() })
        part = onlyPart()
        assertEquals("LUKS2", part.content)
        vol = VolumeManager.unlock(id, part.index, "phrase de passe".toByteArray(), true)
        assertTrue(!vol.readOnly)
        assertEquals("ext4", vol.fs)
        assertTrue(vol.encrypted)
        write(vol.id)
        VolumeManager.eject(vol.id)
        waitFor { VolumeManager.volumes.value.isEmpty() }
    }
}

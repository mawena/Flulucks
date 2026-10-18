package com.mawena.fluluks

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.service.VolumeService
import com.mawena.fluluks.volumes.VolumeManager
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class VolumeManagerInstrumentedTest {
    private var diskId: String? = null

    @Before fun setUp() {
        VolumeManager.init(InstrumentationRegistry.getInstrumentation().targetContext)
    }

    @After fun tearDown() {
        VolumeManager.ejectAll()
        diskId?.let { VolumeManager.removeDiskForTests(it) }
        waitFor("arrêt du service") { !VolumeService.running }
    }

    private fun waitFor(what: String, timeoutMs: Long = 10_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé : $what" }
            Thread.sleep(50)
        }
    }

    @Test fun unlockStartsServiceAndEjectAllStopsIt() = runBlocking {
        val id = VolumeManager.addImageDiskForTests(Fixtures.file("gpt-luks2.img").absolutePath)
        diskId = id
        val disk = VolumeManager.disks.value.first { it.id == id }
        assertNull(disk.error)
        val luksPart = disk.scan!!.parts.first { it.luks != null }

        val v = VolumeManager.unlock(id, luksPart.index, Fixtures.bytes("pass.txt"))
        assertEquals(1, VolumeManager.volumes.value.size)
        assertTrue(VolumeManager.nativeVolume(v.id) > 0)
        waitFor("démarrage du service") { VolumeService.running }

        // re-déverrouiller la même partition ne crée pas de second volume
        assertEquals(v, VolumeManager.unlock(id, luksPart.index, Fixtures.bytes("pass.txt")))
        assertEquals(1, VolumeManager.volumes.value.size)

        VolumeManager.ejectAll()
        waitFor("volumes vides") { VolumeManager.volumes.value.isEmpty() }
        waitFor("arrêt du service") { !VolumeService.running }
        assertNull(VolumeManager.volume(v.id))
        assertNotNull(VolumeManager.disks.value.firstOrNull { it.id == id })
    }
}

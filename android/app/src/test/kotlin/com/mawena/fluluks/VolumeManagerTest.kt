package com.mawena.fluluks

import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.volumes.VolumeManager
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test

/** Moteur factice : une partition LUKS (index 1, Argon2 de `kdfKib` Kio) et une ext4 (index 2). */
class FakeEngine(private val kdfKib: Long = 1024) : Engine {
    var mountCalls = 0
    val unmounts = mutableListOf<Pair<Long, Boolean>>()
    val closedDisks = mutableListOf<Long>()
    var lastPass: ByteArray? = null
    var expectedPass = "secret".toByteArray()
    var openFiles = 0
    val events = mutableListOf<String>()
    var statfsFails = false
    private var next = 100L

    override fun version() = "fake"
    override fun strerror(code: Int) = "erreur $code".toByteArray()
    override fun openImage(path: ByteArray) = next++
    override fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int) = next++
    override fun closeDisk(disk: Long): Int { closedDisks += disk; events += "closeDisk"; return 0 }
    override fun scan(disk: Long) = """{"sector_size":512,"size":1048576,"table":"gpt","parts":[
        {"index":1,"first_lba":2048,"size":524288,"content":"LUKS2","label":"","uuid":"u1","supported":true,
         "luks":{"version":2,"cipher":"aes-xts-plain64","primary_damaged":false,"max_kdf_memory_kib":$kdfKib}},
        {"index":2,"first_lba":4096,"size":262144,"content":"ext4","label":"Photos","uuid":"u2","supported":true}]}""".toByteArray()
    override fun mount(disk: Long, partIndex: Int, pass: ByteArray?): Long {
        mountCalls++
        lastPass = pass
        if (partIndex == 1 && (pass == null || !pass.contentEquals(expectedPass))) return EngineError.FL_EBADPASS.toLong()
        return next++
    }
    override fun unmount(vol: Long, force: Boolean): Int {
        unmounts += vol to force
        events += if (force) "unmount(force)" else "unmount"
        return if (!force && openFiles > 0) EngineError.FL_EBUSY else 0
    }
    override fun statfs(vol: Long) = if (statfsFails) """{"total":""".toByteArray() else
        """{"total":1000,"free":400,"label":"MonDisque","read_only":true,"ro_reason":"lecture seule","needs_recovery":false}""".toByteArray()
    override fun stat(vol: Long, path: ByteArray) = ByteArray(0)
    override fun list(vol: Long, path: ByteArray) = ByteArray(0)
    override fun openFile(vol: Long, path: ByteArray): Long { openFiles++; return next++ }
    override fun pread(file: Long, offset: Long, buf: ByteArray, len: Int) = 0
    override fun closeFile(file: Long): Int { openFiles--; return 0 }
}

class VolumeManagerTest {
    private lateinit var engine: FakeEngine
    private lateinit var disk: String

    private fun setup(kdf: Long = 1024, avail: Long = Long.MAX_VALUE) {
        engine = FakeEngine(kdf)
        VolumeManager.resetForTests(engine, { avail })
        disk = VolumeManager.addImageDiskForTests("/x/gpt-luks2.img")
    }

    @Before fun before() = setup()

    @Test fun unlockPublishesVolume() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        assertTrue(Regex("v[0-9a-f]{16}-1").matches(v.id))
        assertEquals(1000L, v.totalBytes)
        assertEquals(listOf(v), VolumeManager.volumes.value)
        assertEquals(v, VolumeManager.volume(v.id))
        assertTrue(VolumeManager.nativeVolume(v.id) > 0)
        assertEquals(0L, VolumeManager.nativeVolume("nope"))
    }

    @Test fun mountingTwiceReturnsExistingVolume() = runBlocking {
        val a = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        val b = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        assertEquals(a, b)
        assertEquals(1, engine.mountCalls)
        assertEquals(1, VolumeManager.volumes.value.size)
    }

    @Test fun detachWhileFileOpen() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        val handle = VolumeManager.nativeVolume(v.id)
        engine.openFile(handle, "/a".toByteArray())
        VolumeManager.onUsbDetachedName(disk)
        assertEquals(listOf(handle to true), engine.unmounts)
        assertEquals(listOf("unmount(force)", "closeDisk"), engine.events)
        assertEquals(1, engine.closedDisks.size)
        assertTrue(VolumeManager.volumes.value.isEmpty())
        assertTrue(VolumeManager.disks.value.isEmpty())
    }

    @Test fun unknownDeviceProducesNoDiskState() {
        val before = VolumeManager.disks.value
        VolumeManager.onUsbAttachedName("/dev/bus/usb/001/099") // absent de deviceList : intent forgé
        assertEquals(before, VolumeManager.disks.value)
    }

    @Test fun detachOfUnknownNameDoesNothing() = runBlocking {
        VolumeManager.unlock(disk, 1, "secret".toByteArray())
        VolumeManager.onUsbDetachedName("inconnu")
        assertEquals(1, VolumeManager.volumes.value.size)
        assertTrue(engine.events.isEmpty())
    }

    @Test fun titleIsFilesystemLabelThenPartitionIndex() = runBlocking {
        assertEquals("MonDisque", VolumeManager.unlock(disk, 1, "secret".toByteArray()).title)
    }

    @Test fun statfsFailureUnmountsHandle() = runBlocking {
        engine.statfsFails = true
        try { VolumeManager.mountPlain(disk, 2); fail("erreur attendue") } catch (_: Exception) {}
        assertEquals(listOf("unmount(force)"), engine.events)
        assertTrue(VolumeManager.volumes.value.isEmpty())
    }

    @Test fun ejectUnmountsWithForce() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        val handle = VolumeManager.nativeVolume(v.id)
        VolumeManager.eject(v.id)
        assertEquals(listOf(handle to true), engine.unmounts)
        assertTrue(VolumeManager.volumes.value.isEmpty())
        assertEquals(1, VolumeManager.disks.value.size)
    }

    @Test fun wrongPassKeepsDisk() = runBlocking {
        try {
            VolumeManager.unlock(disk, 1, "mauvais".toByteArray())
            fail("EngineError attendue")
        } catch (e: EngineError) {
            assertEquals(EngineError.FL_EBADPASS, e.code)
        }
        assertEquals(1, VolumeManager.disks.value.size)
        assertTrue(VolumeManager.volumes.value.isEmpty())
    }

    @Test fun passZeroedOnError() = runBlocking {
        val pass = "mauvais".toByteArray()
        try { VolumeManager.unlock(disk, 1, pass) } catch (_: EngineError) {}
        assertTrue(pass.all { it == 0.toByte() })
        assertSame(pass, engine.lastPass) // le faux moteur ne zéroïse pas : c'est bien VolumeManager qui le fait
        val ok = "secret".toByteArray()
        VolumeManager.unlock(disk, 1, ok)
        assertTrue(ok.all { it == 0.toByte() })
        val refused = "secret".toByteArray()
        try { VolumeManager.unlock(disk, 7, refused) } catch (_: EngineError) {}
        assertTrue(refused.all { it == 0.toByte() })
    }

    @Test fun unlockRefusedWhenLowMemory() = runBlocking {
        setup(kdf = 1_048_576, avail = 1L shl 30) // 1 Gio d'Argon2 pour 1 Gio disponible
        val pass = "secret".toByteArray()
        try {
            VolumeManager.unlock(disk, 1, pass)
            fail("EngineError attendue")
        } catch (e: EngineError) {
            assertEquals(EngineError.FL_ENOMEM, e.code)
            assertEquals(VolumeManager.MSG_LOW_MEMORY, e.message)
        }
        assertEquals(0, engine.mountCalls)
        assertTrue(pass.all { it == 0.toByte() })
    }

    @Test fun unlockAllowedWhenMemorySufficient() = runBlocking {
        setup(kdf = 65_536, avail = 1L shl 30) // 64 Mio < 80 % de 1 Gio
        VolumeManager.unlock(disk, 1, "secret".toByteArray())
        assertEquals(1, engine.mountCalls)
    }

    @Test fun ninthVolumeRefused() = runBlocking {
        val disks = (1..8).map { VolumeManager.addImageDiskForTests("/x/$it.img") }
        disks.forEach { VolumeManager.mountPlain(it, 2) }
        assertEquals(8, VolumeManager.volumes.value.size)
        try {
            VolumeManager.mountPlain(disk, 2)
            fail("EngineError attendue")
        } catch (e: EngineError) {
            assertEquals(VolumeManager.MSG_TOO_MANY, e.message)
        }
        assertEquals(8, engine.mountCalls)
        assertFalse(VolumeManager.volumes.value.any { it.diskId == disk })
    }
}

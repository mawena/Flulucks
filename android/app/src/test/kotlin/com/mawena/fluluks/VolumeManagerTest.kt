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
    var label = "MonDisque"
    var readOnly = true
    var syncResult = 0
    var syncs = 0
    var lastWritable: Boolean? = null
    private var next = 100L

    override fun version() = "fake"
    override fun strerror(code: Int) = "erreur $code".toByteArray()
    override fun openImage(path: ByteArray, readOnly: Boolean) = next++
    override fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int) = next++
    override fun closeDisk(disk: Long): Int { closedDisks += disk; events += "closeDisk"; return 0 }
    override fun scan(disk: Long) = """{"sector_size":512,"size":1048576,"table":"gpt","parts":[
        {"index":1,"first_lba":2048,"size":524288,"content":"LUKS2","label":"","uuid":"u1","supported":true,
         "luks":{"version":2,"cipher":"aes-xts-plain64","primary_damaged":false,"max_kdf_memory_kib":$kdfKib}},
        {"index":2,"first_lba":4096,"size":262144,"content":"ext4","label":"Photos","uuid":"u2","supported":true}]}""".toByteArray()
    override fun mount(disk: Long, partIndex: Int, pass: ByteArray?, writable: Boolean): Long {
        mountCalls++
        lastPass = pass
        lastWritable = writable
        if (partIndex == 1 && (pass == null || !pass.contentEquals(expectedPass))) return EngineError.FL_EBADPASS.toLong()
        return next++
    }
    override fun unmount(vol: Long, force: Boolean): Int {
        unmounts += vol to force
        events += if (force) "unmount(force)" else "unmount"
        return if (!force && openFiles > 0) EngineError.FL_EBUSY else 0
    }
    override fun statfs(vol: Long) = if (statfsFails) """{"total":""".toByteArray() else
        """{"total":1000,"free":400,"label":"$label","read_only":$readOnly,"ro_reason":"${if (readOnly) "lecture seule" else ""}","needs_recovery":false}""".toByteArray()
    override fun stat(vol: Long, path: ByteArray) = ByteArray(0)
    override fun list(vol: Long, path: ByteArray) = ByteArray(0)
    override fun openFile(vol: Long, path: ByteArray, flags: Int): Long { openFiles++; return next++ }
    override fun pread(file: Long, offset: Long, buf: ByteArray, len: Int) = 0
    override fun pwrite(file: Long, offset: Long, buf: ByteArray, len: Int) = len
    override fun truncate(file: Long, size: Long) = 0
    override fun closeFile(file: Long): Int { openFiles--; return 0 }
    override fun mkdir(vol: Long, path: ByteArray) = 0
    override fun unlink(vol: Long, path: ByteArray) = 0
    override fun rmdir(vol: Long, path: ByteArray) = 0
    override fun rename(vol: Long, from: ByteArray, to: ByteArray) = 0
    override fun sync(vol: Long): Int { syncs++; events += "sync"; return syncResult }
}

/** Horloge virtuelle des synchronisations différées : les tâches partent quand on avance le temps. */
class FakeDelayer : VolumeManager.Delayer {
    var now = 0L
    private val tasks = mutableListOf<Triple<Long, Runnable, BooleanArray>>()
    override fun schedule(delayMs: Long, task: Runnable): () -> Unit {
        val cancelled = BooleanArray(1)
        tasks += Triple(now + delayMs, task, cancelled)
        return { cancelled[0] = true }
    }
    fun advance(ms: Long) {
        now += ms
        val due = tasks.filter { it.first <= now }
        tasks.removeAll(due)
        due.filter { !it.third[0] }.forEach { it.second.run() }
    }
}

private const val SYNC = VolumeManager.SYNC_DELAY_MS

class VolumeManagerTest {
    private lateinit var engine: FakeEngine
    private lateinit var disk: String

    private lateinit var delayer: FakeDelayer
    private val warnings = mutableListOf<String>()

    private fun setup(kdf: Long = 1024, avail: Long = Long.MAX_VALUE) {
        engine = FakeEngine(kdf)
        delayer = FakeDelayer()
        warnings.clear()
        VolumeManager.resetForTests(engine, { avail }, delayer = delayer, detachWarner = { warnings += it })
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

    @Test fun titleIsFilesystemLabelThenDiskSize() = runBlocking {
        assertEquals("MonDisque", VolumeManager.unlock(disk, 1, "secret".toByteArray()).title)
        engine.label = "  "
        assertEquals("Disque 0,3 Mo", VolumeManager.mountPlain(disk, 2).title) // partition de 262 144 octets
        assertEquals("Disque 512,1 Go", VolumeManager.defaultTitle("", 512_110_190_592))
        assertEquals("Photos", VolumeManager.defaultTitle("Photos", 512_110_190_592))
    }

    /** Mêmes valeurs que Fr.humanSize (Dart, test/disks_screen_test.dart). */
    @Test fun humanSizeMatchesDartRule() {
        assertEquals("taille inconnue", VolumeManager.humanSize(-1))
        assertEquals("1,5 Go", VolumeManager.humanSize(1_500_000_000))
        assertEquals("250,0 Mo", VolumeManager.humanSize(250_000_000))
        assertEquals("2,0 Go", VolumeManager.humanSize(2_000_000_000))
        assertEquals("0,0 Mo", VolumeManager.humanSize(0))
        assertEquals("1,0 Go", VolumeManager.humanSize(1_000_000_000))
        assertEquals("1000,0 Mo", VolumeManager.humanSize(999_999_999))
        assertEquals("512,1 Go", VolumeManager.humanSize(512_110_190_592))
        assertEquals("9223372036,9 Go", VolumeManager.humanSize(Long.MAX_VALUE))
    }

    @Test fun serviceRetriedWhileVolumesOpenAndNotRunning() = runBlocking {
        var running = false
        var starts = 0
        VolumeManager.setServiceHooksForTests({ running }, { starts++ })
        VolumeManager.ensureService()
        assertEquals(0, starts) // aucun volume : rien à protéger
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        assertEquals(1, starts) // publication des volumes
        VolumeManager.ensureService() // ex. MainActivity.onResume après un refus en arrière-plan
        assertEquals(2, starts)
        running = true
        VolumeManager.ensureService()
        VolumeManager.mountPlain(disk, 2)
        assertEquals(2, starts) // service en route : idempotent
        running = false
        VolumeManager.eject(v.id)
        assertEquals(3, starts) // il reste un volume ouvert
        VolumeManager.ejectAll()
        VolumeManager.ensureService()
        assertEquals(3, starts)
    }

    @Test fun statfsFailureUnmountsHandle() = runBlocking {
        engine.statfsFails = true
        try { VolumeManager.mountPlain(disk, 2); fail("erreur attendue") } catch (_: Exception) {}
        assertEquals(listOf("unmount(force)"), engine.events)
        assertTrue(VolumeManager.volumes.value.isEmpty())
    }

    @Test fun ejectSyncsBeforeUnmountAndRelease() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        val handle = VolumeManager.nativeVolume(v.id)
        VolumeManager.eject(v.id)
        assertEquals(listOf("sync", "unmount", "closeDisk"), engine.events) // démontage normal, puis libération
        assertEquals(listOf(handle to false), engine.unmounts)
        assertTrue(VolumeManager.volumes.value.isEmpty())
        assertEquals(1, VolumeManager.disks.value.size) // le disque reste listé
        assertFalse(VolumeManager.ejecting.value)
        assertEquals(null, VolumeManager.ejectError.value)
        // rouvert à la demande
        val again = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        assertTrue(VolumeManager.nativeVolume(again.id) > 0)
    }

    @Test fun ejectFailureKeepsVolume() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        engine.syncResult = EngineError.FL_ENOSPC
        VolumeManager.eject(v.id)
        assertEquals(listOf("sync"), engine.events) // ni démontage ni libération
        assertEquals(listOf(v), VolumeManager.volumes.value)
        assertTrue(VolumeManager.ejectError.value!!.startsWith("Éjection impossible : "))
        // fichier encore ouvert : démontage normal refusé (FL_EBUSY), volume gardé
        engine.syncResult = 0
        engine.openFile(VolumeManager.nativeVolume(v.id), "/a".toByteArray())
        VolumeManager.eject(v.id)
        assertEquals(listOf(v), VolumeManager.volumes.value)
        assertEquals(0, engine.closedDisks.size)
    }

    @Test fun writeSchedulesSyncAfter3s() = runBlocking {
        engine.readOnly = false
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        assertTrue(engine.lastWritable == true)
        assertFalse(v.readOnly)
        VolumeManager.noteWrite(v.id)
        delayer.advance(2000)
        VolumeManager.noteWrite(v.id) // rafale : la synchronisation repart de cette écriture
        delayer.advance(2000)
        assertEquals(0, engine.syncs)
        assertTrue(VolumeManager.hasUnsyncedWrites(v.id))
        delayer.advance(1000)
        assertEquals(1, engine.syncs)
        assertFalse(VolumeManager.hasUnsyncedWrites(v.id))
        delayer.advance(10_000)
        assertEquals(1, engine.syncs) // une seule pour la rafale
        VolumeManager.noteWrite("inconnu") // volume absent : rien
        delayer.advance(SYNC)
        assertEquals(1, engine.syncs)
    }

    @Test fun detachDuringWriteWarns() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        VolumeManager.noteWrite(v.id)
        VolumeManager.onUsbDetachedName(disk)
        assertEquals(listOf(VolumeManager.MSG_DETACH_DURING_WRITE), warnings)
        delayer.advance(SYNC)
        assertEquals(0, engine.syncs) // la synchronisation programmée est annulée
        // sans écriture en attente : aucun avertissement
        setup()
        VolumeManager.unlock(disk, 1, "secret".toByteArray())
        VolumeManager.onUsbDetachedName(disk)
        assertTrue(warnings.isEmpty())
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

package com.mawena.fluluks

import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FormatProgress
import com.mawena.fluluks.volumes.FormatOptions
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
    var fs = "ext4"
    var readOnly = true
    var roReason = "lecture seule"
    var syncResult = 0
    var unmountResult = 0
    var syncs = 0
    var lastWritable: Boolean? = null
    var scanCalls = 0
    var formatted = false
    var formatResult = 0
    var formatCalls = 0
    var lastFormatOpts: org.json.JSONObject? = null
    var lastFormatPass: ByteArray? = null
    var progressSeen: Int? = null
    private var next = 100L

    override fun version() = "fake"
    override fun strerror(code: Int) = "erreur $code".toByteArray()
    override fun openImage(path: ByteArray, readOnly: Boolean) = next++
    override fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int) = next++
    override fun closeDisk(disk: Long): Int { closedDisks += disk; events += "closeDisk"; return 0 }
    override fun scan(disk: Long): ByteArray { scanCalls++; return if (formatted) scanFormatted() else scanInitial() }
    private fun scanFormatted() = """{"sector_size":512,"size":1048576,"table":"mbr","parts":[
        {"index":1,"first_lba":2048,"size":1046528,"content":"exFAT","label":"Clé","uuid":"u9","supported":true}]}""".toByteArray()
    override fun format(disk: Long, opts: ByteArray, pass: ByteArray?, progress: FormatProgress?): Int {
        formatCalls++
        lastFormatOpts = org.json.JSONObject(String(opts, Charsets.UTF_8))
        lastFormatPass = pass
        progress?.onProgress(50)
        progressSeen = VolumeManager.formatting.value
        progress?.onProgress(100)
        if (formatResult == 0) formatted = true
        return formatResult
    }
    private fun scanInitial() = """{"sector_size":512,"size":1048576,"table":"gpt","parts":[
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
        return if (!force && openFiles > 0) EngineError.FL_EBUSY else if (!force) unmountResult else 0
    }
    override fun statfs(vol: Long) = if (statfsFails) """{"total":""".toByteArray() else
        """{"total":1000,"free":400,"label":"$label","read_only":$readOnly,"ro_reason":"${if (readOnly) roReason else ""}","needs_recovery":false,"fs":"$fs"}""".toByteArray()
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

    /** Phase 7 : le système de fichiers (conseil au débranchement) et le chiffrement (verrouillage automatique). */
    @Test fun unlockMarksEncryptedAndFs() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray())
        assertTrue(v.encrypted)
        assertEquals("ext4", v.fs)
        engine.fs = "FAT32"
        val p = VolumeManager.mountPlain(disk, 2)
        assertFalse(p.encrypted)
        assertEquals("FAT32", p.fs)
    }

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
        engine.readOnly = false
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        engine.syncResult = EngineError.FL_ENOSPC
        VolumeManager.eject(v.id)
        assertEquals(listOf("sync"), engine.events) // ni démontage ni libération
        assertEquals(listOf(v), VolumeManager.volumes.value)
        assertTrue(VolumeManager.ejectError.value!!.startsWith("Éjection impossible : "))
    }

    /** Fichier encore ouvert : après un sync réussi, démontage forcé (le moteur ferme le fichier et resynchronise). */
    @Test fun ejectWithOpenFileForcesAfterSync() = runBlocking {
        engine.readOnly = false
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        engine.openFile(VolumeManager.nativeVolume(v.id), "/a".toByteArray())
        engine.events.clear()
        VolumeManager.eject(v.id)
        assertEquals(listOf("sync", "unmount", "unmount(force)"), engine.events.filter { it.startsWith("sync") || it.startsWith("unmount") })
        assertTrue(VolumeManager.volumes.value.isEmpty())
        assertEquals(1, engine.closedDisks.size)
        assertEquals(null, VolumeManager.ejectError.value)
    }

    /** Le vidage échoue et le moteur gèle le volume : démontage enchaîné, avertissement, disque libéré. */
    @Test fun ejectAfterFreezeUnmounts() = runBlocking {
        engine.readOnly = false
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        engine.syncResult = EngineError.FL_EIO
        engine.readOnly = true // gelé par le moteur pendant ce sync
        engine.roReason = "erreur d'écriture : volume passé en lecture seule"
        engine.unmountResult = EngineError.FL_EIO
        VolumeManager.eject(v.id)
        assertTrue(engine.events.contains("unmount"))
        assertTrue(VolumeManager.volumes.value.isEmpty())
        assertEquals(1, engine.closedDisks.size)
        assertTrue(VolumeManager.ejectError.value!!.contains("Vérifiez le disque sous Ubuntu"))
    }

    @Test fun ejectUnmountErrorForgetsDeadVolume() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        val handle = VolumeManager.nativeVolume(v.id)
        engine.unmountResult = EngineError.FL_EIO
        VolumeManager.eject(v.id)
        // le moteur a déjà détruit le volume : oublié, disque libéré
        assertEquals(listOf("sync", "unmount", "closeDisk"), engine.events)
        assertTrue(VolumeManager.volumes.value.isEmpty())
        assertEquals(0L, VolumeManager.nativeVolume(v.id))
        assertEquals(1, engine.closedDisks.size)
        val detail = EngineError(EngineError.FL_EIO).message!!
        assertEquals(
            VolumeManager.msgEjectFailed(detail) + " Éjection incomplète : des données ont pu ne pas être écrites ($detail). " +
                "Vérifiez le disque sous Ubuntu.",
            VolumeManager.ejectError.value,
        )
        // seconde éjection : rien, le moteur n'est plus appelé pour ce handle
        val before = engine.events.toList()
        VolumeManager.eject(v.id)
        VolumeManager.ejectAll()
        assertEquals(before, engine.events)
        assertEquals(listOf(handle to false), engine.unmounts)
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

    /** Le moteur passe le volume en lecture seule en cours de session : l'état publié suit. */
    @Test fun readOnlyMidSessionIsPublished() = runBlocking {
        engine.readOnly = false
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        assertFalse(v.readOnly)
        engine.readOnly = true
        engine.roReason = "erreur d'écriture : volume passé en lecture seule"
        VolumeManager.noteWrite(v.id)
        delayer.advance(SYNC)
        val now = VolumeManager.volume(v.id)!!
        assertTrue(now.readOnly)
        assertEquals("erreur d'écriture : volume passé en lecture seule", now.roReason)
        assertEquals(now, VolumeManager.volumes.value.single())
        VolumeManager.refresh(v.id) // sans changement : rien de neuf
        assertEquals(now, VolumeManager.volume(v.id))
        VolumeManager.refresh("inconnu")
    }

    @Test fun markSyncedClearsOnlyIfNoNewWrite() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        VolumeManager.noteWrite(v.id)
        val gen = VolumeManager.writeGeneration(v.id)
        VolumeManager.markSynced(v.id, gen) // synchronisation faite par le provider (fermeture, fsync)
        assertFalse(VolumeManager.hasUnsyncedWrites(v.id))
        VolumeManager.noteWrite(v.id)
        VolumeManager.markSynced(v.id, gen) // une écriture est arrivée depuis : encore à synchroniser
        assertTrue(VolumeManager.hasUnsyncedWrites(v.id))
        VolumeManager.onUsbDetachedName(disk)
        assertEquals(listOf(VolumeManager.detachAdvice("ext4")), warnings)
    }

    @Test fun writeRequestedOnReadOnlyVolumeRemounts() = runBlocking {
        engine.roReason = "ouvert en lecture seule à votre demande"
        val ro = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = false)
        assertTrue(ro.readOnly)
        val h1 = VolumeManager.nativeVolume(ro.id)
        engine.readOnly = false
        val rw = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        assertFalse(rw.readOnly)
        assertEquals(2, engine.mountCalls)
        assertEquals(true, engine.lastWritable)
        assertEquals(listOf(h1 to false), engine.unmounts)
        assertEquals(listOf(rw), VolumeManager.volumes.value)
        assertEquals(0L, VolumeManager.nativeVolume(ro.id))
        // et l'inverse : lecture seule demandée sur un volume en écriture
        engine.readOnly = true
        val back = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = false)
        assertTrue(back.readOnly)
        assertEquals(3, engine.mountCalls)
        // même mode : le volume ouvert est rendu tel quel
        assertEquals(back, VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = false))
        assertEquals(3, engine.mountCalls)
    }

    @Test fun modeChangeRefusedWhileFileOpen() = runBlocking {
        engine.roReason = "ouvert en lecture seule à votre demande"
        val ro = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = false)
        engine.openFile(VolumeManager.nativeVolume(ro.id), "/a".toByteArray())
        engine.readOnly = false
        try {
            VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
            fail("EngineError attendue")
        } catch (e: EngineError) {
            assertEquals(EngineError.FL_EBUSY, e.code)
            assertEquals(VolumeManager.msgModeChange(true, VolumeManager.MSG_FILES_OPEN), e.message)
        }
        assertEquals(listOf(ro), VolumeManager.volumes.value) // volume d'origine intact
        assertEquals(1, engine.mountCalls)
    }

    @Test fun hardReadOnlyVolumeIsNotRemounted() = runBlocking {
        engine.roReason = "disque protégé en écriture"
        val ro = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        assertTrue(ro.readOnly)
        assertEquals(ro, VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true))
        assertEquals(1, engine.mountCalls)
    }

    /** Phase 7 : le conseil après un débranchement pendant une écriture dépend du système de fichiers. */
    @Test fun detachAdviceByFs() {
        val ext4 = VolumeManager.detachAdvice("ext4")
        assertTrue(ext4, ext4.contains("Ubuntu") && ext4.contains("journal") && ext4.contains("e2fsck"))
        for (fat in listOf("FAT12", "FAT16", "FAT32", "exFAT")) {
            val m = VolumeManager.detachAdvice(fat)
            assertTrue(m, m.contains("chkdsk") && m.contains("fsck.fat") && m.contains("lecture seule"))
        }
        val ntfs = VolumeManager.detachAdvice("NTFS")
        assertTrue(ntfs, ntfs.contains("Windows") && !ntfs.contains("fsck"))
        val autre = VolumeManager.detachAdvice("")
        assertTrue(autre, autre.contains("ordinateur") && !autre.contains("journal"))
        for (m in listOf(ext4, ntfs, autre)) assertTrue(m.startsWith("Disque débranché pendant une écriture"))
    }

    @Test fun detachWarnsWithFsAdvice() = runBlocking {
        engine.fs = "FAT32"
        val v = VolumeManager.mountPlain(disk, 2, writable = true)
        VolumeManager.noteWrite(v.id)
        VolumeManager.onUsbDetachedName(disk)
        assertEquals(listOf(VolumeManager.detachAdvice("FAT32")), warnings)
    }

    @Test fun detachWithoutWritesSilent() = runBlocking {
        VolumeManager.mountPlain(disk, 2, writable = true)
        VolumeManager.onUsbDetachedName(disk)
        assertTrue(warnings.isEmpty())
    }

    @Test fun detachDuringWriteWarns() = runBlocking {
        val v = VolumeManager.unlock(disk, 1, "secret".toByteArray(), writable = true)
        VolumeManager.noteWrite(v.id)
        VolumeManager.onUsbDetachedName(disk)
        assertEquals(listOf(VolumeManager.detachAdvice("ext4")), warnings)
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

    // ------------------------------------------------------------------ formatage (phase 8)

    private fun formatErr(o: FormatOptions, pass: ByteArray? = null): EngineError = runBlocking {
        try {
            VolumeManager.format(disk, o, pass)
            fail("EngineError attendue"); throw IllegalStateException()
        } catch (e: EngineError) { e }
    }

    @Test fun formatRefusedWithOpenVolume() = runBlocking {
        VolumeManager.mountPlain(disk, 2)
        val e = formatErr(FormatOptions("exfat", false, "Clé"))
        assertEquals(VolumeManager.MSG_FORMAT_OPEN, e.message)
        assertEquals(0, engine.formatCalls)
    }

    @Test fun formatRescans() = runBlocking {
        val before = engine.scanCalls
        VolumeManager.format(disk, FormatOptions("exfat", false, "Clé"), null)
        assertEquals(1, engine.formatCalls)
        val o = engine.lastFormatOpts!!
        assertEquals("exfat", o.getString("fs"))
        assertFalse(o.getBoolean("encrypted"))
        assertEquals("Clé", o.getString("label"))
        assertEquals(1000, o.getInt("uid"))
        assertEquals(1000, o.getInt("gid"))
        assertTrue(engine.scanCalls > before)
        val parts = VolumeManager.disks.value.single { it.id == disk }.scan!!.parts
        assertEquals(1, parts.size)
        assertEquals("exFAT", parts[0].content)
        assertEquals(50, engine.progressSeen)
        assertEquals(null, VolumeManager.formatting.value)
    }

    @Test fun formatInterruptedReportsAndRescans() = runBlocking {
        engine.formatResult = EngineError.FL_ENODEV
        val before = engine.scanCalls
        val e = formatErr(FormatOptions("ext4", false, ""))
        assertEquals(VolumeManager.MSG_FORMAT_DETACHED, e.message)
        assertTrue(engine.scanCalls > before)
        assertEquals(null, VolumeManager.formatting.value)
        engine.formatResult = EngineError.FL_ETIMEDOUT
        assertEquals(VolumeManager.MSG_FORMAT_DETACHED, formatErr(FormatOptions("ext4", false, "")).message)
    }

    @Test fun formatReadOnlyDisk() {
        engine.formatResult = EngineError.FL_EROFS
        assertEquals(VolumeManager.MSG_FORMAT_RO, formatErr(FormatOptions("exfat", false, "")).message)
    }

    @Test fun passZeroedAfterFormat() = runBlocking {
        val pass = "secret".toByteArray()
        VolumeManager.format(disk, FormatOptions("ext4", true, "Données"), pass)
        assertSame(pass, engine.lastFormatPass)
        assertTrue(pass.all { it == 0.toByte() })
        assertTrue(engine.lastFormatOpts!!.getBoolean("encrypted"))
        engine.formatResult = EngineError.FL_EIO
        val pass2 = "secret".toByteArray()
        formatErr(FormatOptions("ext4", true, ""), pass2)
        assertTrue(pass2.all { it == 0.toByte() })
        val pass3 = "secret".toByteArray()
        formatErr(FormatOptions("btrfs", true, ""), pass3) // refusé avant le moteur
        assertTrue(pass3.all { it == 0.toByte() })
    }

    /** Argon2 : 1 Gio, ou 256 Mio si la mémoire disponible ne le permet pas (même règle que le déverrouillage). */
    @Test fun formatKdfMemoryFollowsAvailableMemory() = runBlocking {
        VolumeManager.format(disk, FormatOptions("ext4", true, ""), "s".toByteArray())
        assertEquals(1_048_576, engine.lastFormatOpts!!.getInt("kdf_mem_kib"))
        setup(avail = 1L shl 30)
        VolumeManager.format(disk, FormatOptions("ext4", true, ""), "s".toByteArray())
        assertEquals(262_144, engine.lastFormatOpts!!.getInt("kdf_mem_kib"))
        setup(avail = 256L shl 20)
        val e = formatErr(FormatOptions("ext4", true, ""), "s".toByteArray())
        assertEquals(VolumeManager.MSG_LOW_MEMORY, e.message)
        assertEquals(0, engine.formatCalls)
    }

    @Test fun formatUnknownFsRefused() {
        val e = formatErr(FormatOptions("btrfs", false, ""))
        assertEquals(EngineError.FL_EINVAL, e.code)
        assertEquals(0, engine.formatCalls)
    }
}

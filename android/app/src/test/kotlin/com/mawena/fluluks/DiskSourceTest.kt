package com.mawena.fluluks

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.volumes.VolumeManager
import java.io.FileNotFoundException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Source disque (volume ouvert) sur le moteur en mémoire, montée par VolumeManager. */
class DiskSourceTest {
    private lateinit var e: MemEngine
    private lateinit var delayer: FakeDelayer
    private lateinit var volId: String

    @Before fun setup() {
        e = MemEngine()
        delayer = FakeDelayer()
        VolumeManager.resetForTests(e, delayer = delayer, clock = { delayer.now })
        val disk = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(disk, 1, true) }.id
    }

    private fun src() = DiskSource(volId, e)

    @Test fun identity() {
        val s = src()
        assertEquals(volId, s.id)
        assertEquals("disk:uuid-mem", s.stableKey)
        assertEquals("Mem", s.label)
        assertEquals(null, s.readOnlyReason)
    }

    @Test fun writesTakeSharedLockAndNoteWrite() {
        val s = src()
        val t: Thread
        synchronized(VolumeManager.writeLock) {
            t = Thread { s.mkdir("/", "D") }.apply { start() }
            Thread.sleep(200)
            assertFalse(e.nodes.containsKey("/D")) // bloqué tant que le verrou est pris
        }
        t.join(5000)
        assertTrue(e.nodes.containsKey("/D"))
        assertTrue(VolumeManager.hasUnsyncedWrites(volId))
    }

    @Test fun readOnlyVolumeRefusesWrites() {
        e.readOnly = true
        e.roReason = "disque protégé en écriture"
        val s = src()
        assertEquals("disque protégé en écriture", s.readOnlyReason)
        val before = e.nodes.toMap()
        for (op in listOf<() -> Unit>({ s.mkdir("/", "D") }, { s.createFile("/", "f").second.close() })) {
            try { op(); throw AssertionError("accepté") } catch (x: FileNotFoundException) {
                assertEquals("Volume en lecture seule : disque protégé en écriture", x.message)
            }
        }
        assertEquals(before.keys, e.nodes.keys)
    }

    @Test fun streamsRoundTrip3MiB() {
        val s = src()
        val data = ByteArray(3 shl 20) { (it * 7).toByte() }
        val (path, out) = s.createFile("/", "a.bin")
        out.use { it.write(data) }
        assertEquals("/a.bin", path)
        assertArrayEquals(data, s.openRead(path).use { it.readBytes() })
        val st = s.stat(path)!!
        assertEquals(FileType.FILE, st.type)
        assertEquals(data.size.toLong(), st.size)
        assertEquals(listOf("a.bin"), s.list("/").map { it.name })
        assertTrue(e.files.isEmpty()) // tous les fichiers du moteur refermés
    }

    @Test fun everyCallNotesActivity() {
        val s = src()
        delayer.now = 1000
        s.list("/")
        assertEquals(1000L, VolumeManager.lastActivity(volId))
        delayer.now = 5000
        s.stat("/")
        assertEquals(5000L, VolumeManager.lastActivity(volId))
    }

    @Test fun closedVolumeFails() {
        val s = src()
        VolumeManager.eject(volId)
        try { s.list("/"); throw AssertionError("accepté") } catch (x: FileNotFoundException) {
            assertEquals("Volume fermé", x.message)
        }
    }
}

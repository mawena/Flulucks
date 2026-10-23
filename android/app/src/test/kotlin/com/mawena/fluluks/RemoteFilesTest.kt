package com.mawena.fluluks

import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.ThumbDecoder
import com.mawena.fluluks.files.AndroidThumbDecoder
import com.mawena.fluluks.files.Thumbnails
import com.mawena.fluluks.files.TransferState
import com.mawena.fluluks.files.Transfers
import com.mawena.fluluks.remote.RemoteSource
import com.mawena.fluluks.remote.ServerConfig
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Dossier distant (faux client) dans le gestionnaire : suppression, copies, place inconnue, vignettes. */
class RemoteFilesTest {
    @get:Rule val tmp = TemporaryFolder()

    private lateinit var server: File
    private lateinit var phone: File
    private lateinit var client: FakeRemoteClient
    private lateinit var e: MemEngine
    private lateinit var volId: String
    private val rid = "remote:s1"
    private val mib = 1 shl 20

    @Before fun setup() {
        server = tmp.newFolder("serveur")
        phone = tmp.newFolder("telephone")
        client = FakeRemoteClient(server)
        e = MemEngine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        val disk = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(disk, 1, true) }.id
        Transfers.resetForTests()
        val local = LocalSource("phone", "phone", "Téléphone", phone) { true }
        val remote = RemoteSource(ServerConfig("s1", "sftp", "Maison", "h", 22, "u", "/", false, null, null), client)
        val disks = DiskSource(volId, e)
        Files.setResolverForTests { id ->
            when (id) {
                "phone" -> local
                rid -> remote
                volId -> disks.takeIf { VolumeManager.volume(volId) != null }
                else -> null
            }
        }
    }

    @After fun teardown() {
        Transfers.resetForTests()
        Files.setResolverForTests(null)
        Thumbnails.decoder = AndroidThumbDecoder
        Thumbnails.clearForTests()
    }

    private fun data(n: Int, seed: Int = 7) = ByteArray(n) { (it * seed + it / 4093).toByte() }

    private fun run(srcId: String, paths: List<String>, dstId: String, dir: String): TransferState {
        Transfers.start(srcId, paths, dstId, dir, false)
        val end = System.currentTimeMillis() + 10_000
        while (true) {
            val s = Transfers.state.value
            if (s != null && s.finished && !Transfers.isBusy(srcId) && !Transfers.isBusy(dstId)) return s
            check(System.currentTimeMillis() < end) { "délai dépassé : $s" }
            Thread.sleep(10)
        }
    }

    @Test fun deleteOnRemoteIsPermanent() {
        File(server, "a.txt").writeText("x")
        File(server, "d").mkdir(); File(server, "d/b.txt").writeText("y")
        Files.delete(rid, listOf("/a.txt", "/d"), permanent = false)
        assertEquals(emptyList<String>(), server.list()!!.toList()) // pas de .Portail-corbeille
        assertFalse(Files.source(rid).hasTrash)
        assertTrue(Files.source("phone").hasTrash)
    }

    @Test fun copyBothWaysWithRemote() {
        val b = data(3 * mib + 17)
        File(phone, "film.bin").writeBytes(b)
        assertNull(run("phone", listOf("/film.bin"), rid, "/").error)
        assertArrayEquals(b, File(server, "film.bin").readBytes())
        assertNull(run(rid, listOf("/film.bin"), volId, "/").error)
        val back = Files.source(volId).openRead("/film.bin").use { it.readBytes() }
        assertArrayEquals(b, back)
    }

    @Test fun cancelledUploadLeavesNothing() {
        File(phone, "gros.bin").writeBytes(data(3 * mib))
        client.onWrite = { if (it == 2) Transfers.cancel() }
        val s = run("phone", listOf("/gros.bin"), rid, "/")
        assertTrue(s.cancelled)
        assertEquals(emptyList<String>(), server.list()!!.toList()) // partiel distant supprimé
    }

    @Test fun unknownSpaceNotChecked() {
        assertEquals(-1L to -1L, Files.source(rid).space())
        File(phone, "a.txt").writeText("contenu")
        val s = run("phone", listOf("/a.txt"), rid, "/")
        assertNull(s.error)
        assertEquals("contenu", File(server, "a.txt").readText())
    }

    @Test fun remoteThumbnailsImagesOnly() {
        val calls = ArrayList<String>()
        Thumbnails.decoder = object : ThumbDecoder {
            override fun image(src: FileSource, path: String, sizePx: Int): ByteArray { calls += "image $path"; return ByteArray(10) }
            override fun video(src: FileSource, path: String, sizePx: Int): ByteArray { calls += "video $path"; return ByteArray(10) }
        }
        Thumbnails.clearForTests()
        File(server, "petite.jpg").writeBytes(ByteArray(1000))
        File(server, "grande.jpg").writeBytes(ByteArray(8 * mib + 1))
        File(server, "film.mp4").writeBytes(ByteArray(1000))
        assertNotNull(Thumbnails.get(rid, "/petite.jpg", 128))
        assertNull(Thumbnails.get(rid, "/grande.jpg", 128))
        assertNull(Thumbnails.get(rid, "/film.mp4", 128))
        assertEquals(listOf("image /petite.jpg"), calls)
    }
}

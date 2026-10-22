package com.mawena.fluluks

import com.mawena.fluluks.files.AndroidThumbDecoder
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.ThumbDecoder
import com.mawena.fluluks.files.Thumbnails
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Cache des vignettes, avec un décodeur injecté (BitmapFactory n'existe pas sur la JVM). */
class ThumbnailsTest {
    @get:Rule val tmp = TemporaryFolder()

    private class Decoder : ThumbDecoder {
        val calls = ArrayList<String>()
        var size = 100
        override fun image(src: FileSource, path: String, sizePx: Int): ByteArray {
            calls += "image $path $sizePx"
            return ByteArray(size) { 1 }
        }
        override fun video(src: FileSource, path: String, sizePx: Int): ByteArray {
            calls += "video $path $sizePx"
            return ByteArray(size) { 2 }
        }
    }

    private lateinit var dec: Decoder
    private lateinit var volId: String
    private lateinit var e: MemEngine

    @Before fun setup() {
        e = MemEngine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        val disk = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(disk, 1, true) }.id
        val local = LocalSource("phone", "phone", "Téléphone", tmp.root) { true }
        val d = DiskSource(volId, e)
        Files.setResolverForTests { id -> if (id == "phone") local else d.takeIf { id == volId } }
        dec = Decoder()
        Thumbnails.decoder = dec
        Thumbnails.clearForTests()
    }

    @After fun teardown() {
        Thumbnails.decoder = AndroidThumbDecoder
        Thumbnails.clearForTests()
        Files.setResolverForTests(null)
    }

    private fun diskFile(name: String) = Files.source(volId).createFile("/", name).second.use { it.write(byteArrayOf(1, 2, 3)) }

    @Test fun cacheHitDoesNotDecodeTwice() {
        File(tmp.root, "a.jpg").writeBytes(byteArrayOf(1))
        val first = Thumbnails.get("phone", "/a.jpg", 256)!!
        assertArrayEquals(first, Thumbnails.get("phone", "/a.jpg", 256))
        assertEquals(listOf("image /a.jpg 256"), dec.calls)
        Thumbnails.get("phone", "/a.jpg", 1000) // borné à 256 : même entrée
        assertEquals(1, dec.calls.size)
    }

    @Test fun cacheDroppedOnEject() {
        diskFile("v.mp4")
        File(tmp.root, "a.jpg").writeBytes(byteArrayOf(1))
        Thumbnails.get(volId, "/v.mp4", 128)
        Thumbnails.get("phone", "/a.jpg", 128)
        assertEquals(listOf("video /v.mp4 128", "image /a.jpg 128"), dec.calls)
        assertEquals(200L, Thumbnails.cachedBytes())
        VolumeManager.eject(volId)
        assertEquals(100L, Thumbnails.cachedBytes()) // seule la vignette du téléphone reste
        assertNull(Thumbnails.get(volId, "/v.mp4", 128))
    }

    @Test fun unknownTypeReturnsNull() {
        File(tmp.root, "notes.txt").writeText("x")
        File(tmp.root, "D.jpg").mkdir()
        assertNull(Thumbnails.get("phone", "/notes.txt", 128))
        assertNull(Thumbnails.get("phone", "/D.jpg", 128))
        assertNull(Thumbnails.get("phone", "/absent.jpg", 128))
        assertNull(Thumbnails.get("phone", "/../x.jpg", 128))
        assertTrue(dec.calls.isEmpty())
    }

    @Test fun keyChangesWithMtime() {
        val f = File(tmp.root, "a.png").apply { writeBytes(byteArrayOf(1)) }
        f.setLastModified(1_000_000_000_000)
        Thumbnails.get("phone", "/a.png", 64)
        f.setLastModified(1_000_000_100_000)
        Thumbnails.get("phone", "/a.png", 64)
        assertEquals(2, dec.calls.size)
    }

    @Test fun cacheBoundedTo32MiB() {
        dec.size = 5 shl 20
        for (i in 0 until 10) {
            File(tmp.root, "p$i.jpg").writeBytes(byteArrayOf(1))
            Thumbnails.get("phone", "/p$i.jpg", 256)
        }
        assertTrue(Thumbnails.cachedBytes() <= Thumbnails.CACHE_BYTES)
        dec.calls.clear()
        Thumbnails.get("phone", "/p9.jpg", 256) // la plus récente est gardée
        Thumbnails.get("phone", "/p0.jpg", 256) // la plus ancienne est sortie
        assertEquals(listOf("image /p0.jpg 256"), dec.calls)
    }
}

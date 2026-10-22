package com.mawena.fluluks

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.os.Environment
import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.files.Thumbnails
import com.mawena.fluluks.volumes.VolumeManager
import java.io.ByteArrayOutputStream
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Vignettes réelles (BitmapFactory, MediaMetadataRetriever) sur le téléphone et sur un disque. */
@RunWith(AndroidJUnit4::class)
class ThumbnailsInstrumentedTest {
    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx get() = instr.targetContext
    private var diskId: String? = null
    private lateinit var vol: String

    @Suppress("DEPRECATION")
    private val testDir = File(Environment.getExternalStorageDirectory(), "Download/portail-tests")

    @Before fun setUp() {
        ParcelFileDescriptor.AutoCloseInputStream(
            instr.uiAutomation.executeShellCommand("appops set ${ctx.packageName} MANAGE_EXTERNAL_STORAGE allow"),
        ).use { it.readBytes() }
        Sources.init(ctx)
        VolumeManager.init(ctx)
        VolumeManager.ejectAll()
        assumeTrue("accès à tous les fichiers refusé", Sources.hasAllFilesAccess())
        Thumbnails.clearForTests()
        testDir.deleteRecursively()
        assertTrue(testDir.mkdirs())
        val img = File(ctx.cacheDir, "vignettes.img")
        Fixtures.file("ext4-rw-reel.img").copyTo(img, overwrite = true)
        val id = VolumeManager.addImageDiskForTests(img.absolutePath, readOnly = false)
        diskId = id
        val part = VolumeManager.disks.value.first { it.id == id }.scan!!.parts.first()
        vol = runBlocking { VolumeManager.mountPlain(id, part.index, true) }.id
    }

    @After fun tearDown() {
        testDir.deleteRecursively()
        Thumbnails.clearForTests()
        VolumeManager.ejectAll()
        Thread.sleep(500)
        diskId?.let { VolumeManager.removeDiskForTests(it) }
    }

    private fun bigJpeg(): ByteArray {
        val bmp = Bitmap.createBitmap(4000, 3000, Bitmap.Config.RGB_565)
        bmp.eraseColor(Color.rgb(200, 80, 30))
        return ByteArrayOutputStream().also { bmp.compress(Bitmap.CompressFormat.JPEG, 70, it); bmp.recycle() }.toByteArray()
    }

    private fun assertThumb(bytes: ByteArray?) {
        assertNotNull(bytes)
        val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes!!.size, o)
        assertTrue("${o.outWidth}×${o.outHeight}", o.outWidth in 1..256 && o.outHeight in 1..256)
        assertTrue(o.outWidth == 256 || o.outHeight == 256)
    }

    @Test fun jpegFromPhoneAndDisk() {
        val jpeg = bigJpeg()
        File(testDir, "photo.jpg").writeBytes(jpeg)
        assertThumb(Thumbnails.get(Sources.PHONE, "/Download/portail-tests/photo.jpg", 256))
        val (path, out) = Files.source(vol).createFile("/", "photo.jpg")
        out.use { it.write(jpeg) }
        assertThumb(Thumbnails.get(vol, path, 256))
    }

    @Test fun videoFrameFromPhone() {
        val mp4 = File(testDir, "film.mp4")
        assumeTrue("encodeur H.264 indisponible", encodeVideo(mp4))
        assertThumb(Thumbnails.get(Sources.PHONE, "/Download/portail-tests/film.mp4", 256))
    }

    /** MP4 H.264 de 2 s, 320 × 240, à 15 images/s, par MediaCodec (entrée YUV 420) et MediaMuxer. */
    private fun encodeVideo(out: File): Boolean = try {
        val w = 320; val h = 240; val fps = 15
        val fmt = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, w, h).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
            setInteger(MediaFormat.KEY_BIT_RATE, 500_000)
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
        }
        val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        codec.configure(fmt, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        codec.start()
        val muxer = MediaMuxer(out.path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
        var track = -1
        var frame = 0
        val total = fps * 2
        val info = MediaCodec.BufferInfo()
        var done = false
        while (!done) {
            if (frame <= total) {
                val i = codec.dequeueInputBuffer(10_000)
                if (i >= 0) {
                    if (frame == total) {
                        codec.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                    } else {
                        val img = codec.getInputImage(i)!!
                        for (p in img.planes.indices) {
                            val plane = img.planes[p]
                            val buf = plane.buffer
                            val v = if (p == 0) (frame * 8) % 256 else 128
                            for (k in 0 until buf.remaining()) buf.put(k, v.toByte())
                        }
                        codec.queueInputBuffer(i, 0, w * h * 3 / 2, frame * 1_000_000L / fps, 0)
                    }
                    frame++
                }
            }
            val o = codec.dequeueOutputBuffer(info, 10_000)
            if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                track = muxer.addTrack(codec.outputFormat); muxer.start()
            } else if (o >= 0) {
                if (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0 && info.size > 0 && track >= 0) {
                    muxer.writeSampleData(track, codec.getOutputBuffer(o)!!, info)
                }
                codec.releaseOutputBuffer(o, false)
                if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) done = true
            }
        }
        codec.stop(); codec.release()
        muxer.stop(); muxer.release()
        out.length() > 0
    } catch (e: Exception) {
        false
    }
}

package com.mawena.fluluks

import android.os.Environment
import android.os.ParcelFileDescriptor
import androidx.media3.common.C
import androidx.media3.exoplayer.ExoPlayer
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.media.AndroidMusic
import com.mawena.fluluks.media.Music
import com.mawena.fluluks.media.Subtitles
import com.mawena.fluluks.media.VideoActivity
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Base64
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Lecteurs de Portail sur le Pixel, sans allumer l'écran : un WAV muet généré par le test lu par le lecteur de
 * musique (service Media3), et la vidéo de référence lue par le lecteur de [VideoActivity] avec un sous-titre en
 * Windows-1252. Fichiers dans Download/portail-tests/, supprimé à la fin.
 */
@RunWith(AndroidJUnit4::class)
class ReadersInstrumentedTest {
    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx get() = instr.targetContext

    @Suppress("DEPRECATION")
    private val testDir = File(Environment.getExternalStorageDirectory(), "Download/portail-tests")
    private val phoneDir = "/Download/portail-tests"

    private fun shell(cmd: String) =
        ParcelFileDescriptor.AutoCloseInputStream(instr.uiAutomation.executeShellCommand(cmd)).use { it.readBytes() }

    private fun waitFor(timeoutMs: Long = 15_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé" }
            Thread.sleep(50)
        }
    }

    @Before fun setUp() {
        shell("appops set ${ctx.packageName} MANAGE_EXTERNAL_STORAGE allow")
        Sources.init(ctx)
        assumeTrue("accès à tous les fichiers refusé", Sources.hasAllFilesAccess())
        testDir.deleteRecursively()
        assertTrue(testDir.mkdirs())
    }

    @After fun tearDown() {
        Music.stop()
        testDir.deleteRecursively()
    }

    /** WAV PCM 16 bits mono à 8 kHz, muet. */
    private fun silentWav(seconds: Int): ByteArray {
        val data = 8000 * 2 * seconds
        return ByteBuffer.allocate(44 + data).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(36 + data); put("WAVE".toByteArray())
            put("fmt ".toByteArray()); putInt(16); putShort(1); putShort(1); putInt(8000); putInt(16000); putShort(2); putShort(16)
            put("data".toByteArray()); putInt(data)
        }.array()
    }

    @Test fun musicPlaysAndAdvances() {
        File(testDir, "son.wav").writeBytes(silentWav(8))
        File(testDir, "notes.txt").writeText("pas un son")
        if (Music.backend == null) Music.backend = AndroidMusic(ctx)
        Music.play("phone", listOf("$phoneDir/notes.txt", "$phoneDir/son.wav"), 1)
        waitFor { Music.state.value.first?.playing == true }
        val s = Music.state.value.first!!
        assertEquals("son.wav", s.title)
        assertEquals(1, s.count)
        val p1 = s.positionMs
        waitFor { (Music.state.value.first?.positionMs ?: 0) > p1 + 1000 } // position publiée qui avance
        Music.stop()
        waitFor { Music.state.value.first == null }
    }

    @Test fun videoWithWindows1252Subtitle() {
        Fixtures.file("video-phase10.mp4").copyTo(File(testDir, "film.mp4"))
        val srt = "1\n00:00:00,000 --> 00:00:03,000\nÉté à Lomé\n".toByteArray(charset("windows-1252"))
        File(testDir, "film.fr.srt").writeBytes(srt)
        val src = Files.source("phone")
        val subs = Subtitles.load(src, "$phoneDir/film.mp4")
        assertEquals(listOf("fr"), subs.map { it.language })
        val text = String(Base64.getDecoder().decode(subs[0].dataUri.substringAfter(',')), Charsets.UTF_8)
        assertTrue(text, text.contains("Été à Lomé"))
        lateinit var player: ExoPlayer
        instr.runOnMainSync {
            player = VideoActivity.newPlayer(ctx, src.contentUri("$phoneDir/film.mp4"), subs)
            player.volume = 0f
            player.prepare()
            player.play()
        }
        try {
            fun main(read: () -> Boolean): Boolean { var r = false; instr.runOnMainSync { r = read() }; return r }
            waitFor { main { player.playerError != null || player.isPlaying } }
            var error: String? = null
            instr.runOnMainSync { error = player.playerError?.message }
            assertEquals(null, error)
            waitFor { main { player.currentPosition > 1000 } }
            assertTrue("sous-titre non choisi", main { player.currentTracks.isTypeSelected(C.TRACK_TYPE_TEXT) })
            assertTrue(main { player.currentTracks.isTypeSelected(C.TRACK_TYPE_VIDEO) })
        } finally {
            instr.runOnMainSync { player.release() }
        }
    }

    /** Relecture finale (C1) : musique d'un dossier distant, lancée par le pont depuis le fil principal. */
    @Test fun remoteMusicPreparedOffMainThread() {
        val root = File(ctx.cacheDir, "distant-musique").apply { deleteRecursively(); mkdirs() }
        File(root, "son.wav").writeBytes(silentWav(8))
        val onMain = java.util.concurrent.atomic.AtomicInteger()
        val client = object : com.mawena.fluluks.remote.RemoteClient {
            private fun f(p: String) = File(root, p.removePrefix("/"))
            private fun check() { if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) onMain.incrementAndGet() }
            override fun list(path: String) = f(path).listFiles()!!.map { stat("/${it.name}")!! }
            override fun stat(path: String): com.mawena.fluluks.files.Entry? {
                check()
                return f(path).takeIf { it.exists() }?.let {
                    com.mawena.fluluks.files.Entry(it.name, path, if (it.isDirectory) com.mawena.fluluks.engine.FileType.DIR
                    else com.mawena.fluluks.engine.FileType.FILE, it.length(), 0)
                }
            }
            override fun openReadAt(path: String, offset: Long): java.io.InputStream {
                check()
                return java.io.FileInputStream(f(path)).also { it.skip(offset) }
            }
            override fun create(parent: String, name: String): java.io.OutputStream = throw UnsupportedOperationException()
            override fun mkdir(parent: String, name: String) = throw UnsupportedOperationException()
            override fun rename(path: String, newPath: String) = throw UnsupportedOperationException()
            override fun delete(path: String, dir: Boolean) = throw UnsupportedOperationException()
            override fun close() {}
        }
        val store = com.mawena.fluluks.remote.RemoteServers(File(ctx.cacheDir, "distant-fiches").apply { deleteRecursively() },
            object : com.mawena.fluluks.remote.SecretBox {
                override fun seal(plain: ByteArray) = plain.copyOf()
                override fun open(sealed: ByteArray) = sealed.copyOf()
            })
        val saved = store.save(com.mawena.fluluks.remote.ServerConfig("", "sftp", "Essai", "h", 22, "u", "/", false, null, null), null, null)
        com.mawena.fluluks.remote.Remotes.resetForTests(store) { _, _ -> client }
        try {
            val src = com.mawena.fluluks.remote.Remotes.connect(saved.id, null)
            if (Music.backend == null) Music.backend = AndroidMusic(ctx)
            val api = com.mawena.fluluks.bridge.HostApiImpl(ctx, object : io.flutter.plugin.common.BinaryMessenger {
                override fun send(channel: String, message: java.nio.ByteBuffer?) {}
                override fun send(channel: String, message: java.nio.ByteBuffer?, callback: io.flutter.plugin.common.BinaryMessenger.BinaryReply?) {}
                override fun setMessageHandler(channel: String, handler: io.flutter.plugin.common.BinaryMessenger.BinaryMessageHandler?) {}
            })
            var result: Result<Unit>? = null
            instr.runOnMainSync { api.playAudio(src.id, listOf("/son.wav"), 0) { result = it } }
            waitFor { result != null }
            assertTrue("échec : ${result?.exceptionOrNull()?.message}", result!!.isSuccess)
            waitFor { Music.state.value.first?.playing == true }
            assertEquals(0, onMain.get()) // aucun accès au serveur sur le fil principal
        } finally {
            Music.stop()
            com.mawena.fluluks.remote.Remotes.resetForTests(null, null)
            root.deleteRecursively()
        }
    }
}

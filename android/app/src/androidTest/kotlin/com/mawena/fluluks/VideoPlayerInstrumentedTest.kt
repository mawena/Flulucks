package com.mawena.fluluks

import android.app.Activity
import android.content.pm.ActivityInfo
import android.os.Environment
import android.os.ParcelFileDescriptor
import android.view.View
import android.view.ViewGroup
import androidx.media3.common.C
import androidx.media3.exoplayer.ExoPlayer
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.media.PlayerTracks
import com.mawena.fluluks.media.Subtitles
import com.mawena.fluluks.media.TrackInfo
import com.mawena.fluluks.media.TrackKind
import com.mawena.fluluks.media.TrackMenu
import com.mawena.fluluks.media.VideoActivity
import com.mawena.fluluks.media.VideoOrientation
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Lecteur vidéo de la phase 12 sur le Pixel : pistes de `video-phase12.mkv` (deux pistes audio, un sous-titre
 * intégré ; `tools/fixtures/make_video_phase12.sh`, à pousser dans /data/local/tmp/fluluks/fixtures/) et d'un
 * `film.fr.srt` posé à côté, puis orientation demandée par [VideoActivity] et ses boutons. Fichiers dans
 * Download/portail-tests/, supprimé à la fin.
 */
@RunWith(AndroidJUnit4::class)
class VideoPlayerInstrumentedTest {
    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx get() = instr.targetContext

    @Suppress("DEPRECATION")
    private val testDir = File(Environment.getExternalStorageDirectory(), "Download/portail-tests")
    private val phoneDir = "/Download/portail-tests"
    private var player: ExoPlayer? = null

    private fun shell(cmd: String) =
        ParcelFileDescriptor.AutoCloseInputStream(instr.uiAutomation.executeShellCommand(cmd)).use { it.readBytes() }

    private fun waitFor(timeoutMs: Long = 15_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            check(System.currentTimeMillis() < end) { "délai dépassé" }
            Thread.sleep(50)
        }
    }

    private fun <T> main(read: () -> T): T {
        var r: Result<T>? = null
        instr.runOnMainSync { r = runCatching(read) }
        return r!!.getOrThrow()
    }

    @Before fun setUp() {
        shell("appops set ${ctx.packageName} MANAGE_EXTERNAL_STORAGE allow")
        Sources.init(ctx)
        assumeTrue("accès à tous les fichiers refusé", Sources.hasAllFilesAccess())
        testDir.deleteRecursively()
        assertTrue(testDir.mkdirs())
        Fixtures.file("video-phase12.mkv").copyTo(File(testDir, "film.mkv"))
        File(testDir, "film.fr.srt").writeText("1\n00:00:00,000 --> 00:00:05,000\nÉté à Lomé\n")
    }

    @After fun tearDown() {
        player?.let { p -> instr.runOnMainSync { p.release() } }
        testDir.deleteRecursively()
    }

    /** Lecteur de [VideoActivity.newPlayer] sur la vidéo et son `.srt`, prêt, muet. */
    private fun readyPlayer(): ExoPlayer {
        val src = Files.source("phone")
        val subs = Subtitles.load(src, "$phoneDir/film.mkv")
        val p = main { VideoActivity.newPlayer(ctx, src.contentUri("$phoneDir/film.mkv"), subs).apply { volume = 0f; prepare() } }
        player = p
        waitFor { main { p.playerError != null || !p.currentTracks.isEmpty } }
        assertEquals(null, main { p.playerError?.message })
        return p
    }

    private fun infos(p: ExoPlayer): List<TrackInfo> = main { PlayerTracks.infos(p.currentTracks) }

    @Test fun tracksListedInFrench() {
        val p = readyPlayer()
        val infos = infos(p)
        assertEquals(listOf("Français · Original · Stéréo", "Anglais · Commentaires · Mono"), TrackMenu.audio(infos).map { it.title })
        val text = TrackMenu.text(infos).map { it.title }
        assertEquals(TrackMenu.OFF, text.first())
        assertTrue(text.toString(), "Français · film.fr.srt" in text)
        assertTrue(text.toString(), text.any { it.startsWith("Anglais") })
    }

    @Test fun audioTrackSwitches() {
        val p = readyPlayer()
        val second = TrackMenu.audio(infos(p))[1]
        instr.runOnMainSync { PlayerTracks.apply(p, p.currentTracks, second, TrackKind.AUDIO) }
        waitFor { infos(p).filter { it.kind == TrackKind.AUDIO }.map { it.selected } == listOf(false, true) }
    }

    @Test fun subtitlesCanBeDisabled() {
        val p = readyPlayer()
        val choices = TrackMenu.text(infos(p))
        instr.runOnMainSync { PlayerTracks.apply(p, p.currentTracks, choices.first(), TrackKind.TEXT) }
        waitFor { !main { p.currentTracks.isTypeSelected(C.TRACK_TYPE_TEXT) } }
        val srt = choices.first { it.title == "Français · film.fr.srt" }
        instr.runOnMainSync { PlayerTracks.apply(p, p.currentTracks, srt, TrackKind.TEXT) }
        waitFor { infos(p).any { it.kind == TrackKind.TEXT && it.selected && it.label == "film.fr.srt" } }
    }

    /** Bouton de la barre du haut dont la description TalkBack est [description]. */
    private fun button(root: View, description: String): View? {
        if (root.contentDescription == description) return root
        if (root is ViewGroup) for (i in 0 until root.childCount) button(root.getChildAt(i), description)?.let { return it }
        return null
    }

    @Test fun activityStartsInUserOrientationAndLocks() {
        val monitor = instr.addMonitor(VideoActivity::class.java.name, null, false)
        ctx.startActivity(VideoActivity.intent(ctx, "phone", "$phoneDir/film.mkv"))
        val a: Activity = assertNotNullAndGet(monitor.waitForActivityWithTimeout(10_000))
        try {
            assertEquals(ActivityInfo.SCREEN_ORIENTATION_USER, main { a.requestedOrientation })
            val root = a.window.decorView
            fun click(description: String) = instr.runOnMainSync { button(root, description)!!.performClick() }
            click("Verrouiller l'orientation")
            assertEquals(ActivityInfo.SCREEN_ORIENTATION_LOCKED, main { a.requestedOrientation })
            click(VideoOrientation.ROTATE)
            assertTrue(main { a.requestedOrientation } in setOf(ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE,
                ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT))
            click("Déverrouiller l'orientation")
            assertEquals(ActivityInfo.SCREEN_ORIENTATION_USER, main { a.requestedOrientation })
        } finally {
            instr.runOnMainSync { a.finish() }
            instr.removeMonitor(monitor)
        }
    }

    private fun <T : Any> assertNotNullAndGet(v: T?): T {
        assertNotNull("lecteur vidéo non lancé", v)
        return v!!
    }
}

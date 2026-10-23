package com.mawena.fluluks.media

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.WindowManager
import android.widget.Toast
import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import com.mawena.fluluks.files.Files

/**
 * Lecteur vidéo de Portail (phase 10) : plein écran, paysage selon le capteur, recul et avance de 10 s, sous-titres
 * `.srt` trouvés à côté de la vidéo (le premier actif). La vidéo est lue par l'URI de contenu de sa source (fournisseur
 * de flux pour un dossier distant) ; l'écran reste allumé pendant la lecture. Erreur de lecture ou stockage fermé :
 * message, puis fermeture.
 */
@OptIn(UnstableApi::class)
class VideoActivity : Activity() {
    private var player: ExoPlayer? = null
    private lateinit var sourceId: String
    @Volatile private var closed = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sourceId = intent.getStringExtra(EXTRA_SOURCE) ?: return finish()
        val path = intent.getStringExtra(EXTRA_PATH) ?: return finish()
        val view = PlayerView(this).apply {
            setShowSubtitleButton(true)
            setShowNextButton(false)
            setShowPreviousButton(false)
        }
        setContentView(view)
        // URI et sous-titres hors du fil principal : un dossier distant les lit par le réseau
        Thread {
            val prepared = runCatching {
                val src = Files.source(sourceId)
                src.contentUri(path) to Subtitles.load(src, path)
            }
            runOnUiThread {
                if (closed) return@runOnUiThread
                prepared.fold({ (uri, subs) -> start(view, uri, subs) }, { fail(Files.toFilesException(it, "openDocument").message) })
            }
        }.start()
    }

    private fun start(view: PlayerView, uri: Uri, subs: List<SubtitleTrack>) {
        val p = newPlayer(this, uri, subs)
        p.addListener(object : Player.Listener {
            override fun onIsPlayingChanged(isPlaying: Boolean) {
                if (isPlaying) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }

            override fun onPlayerError(error: PlaybackException) {
                fail(if (Files.sourceOrNull(sourceId) == null) Files.MSG_SOURCE_CLOSED else MSG_PLAY_ERROR)
            }
        })
        view.player = p
        player = p
        p.prepare()
        p.play()
    }

    private fun fail(message: String?) {
        Toast.makeText(this, message ?: MSG_PLAY_ERROR, Toast.LENGTH_LONG).show()
        finish()
    }

    override fun onStart() {
        super.onStart()
        // stockage fermé pendant que la vidéo était cachée
        if (::sourceId.isInitialized && Files.sourceOrNull(sourceId) == null) fail(Files.MSG_SOURCE_CLOSED)
    }

    override fun onStop() {
        super.onStop()
        player?.pause()
    }

    override fun onDestroy() {
        closed = true
        player?.release()
        player = null
        super.onDestroy()
    }

    companion object {
        const val EXTRA_SOURCE = "source"
        const val EXTRA_PATH = "path"
        const val SEEK_MS = 10_000L
        const val MSG_PLAY_ERROR = "Lecture impossible"

        /**
         * Lecteur de la vidéo [uri] avec ses sous-titres (le premier actif), pas encore préparé ; repris tel quel par
         * l'essai instrumenté.
         */
        fun newPlayer(context: Context, uri: Uri, subs: List<SubtitleTrack>): ExoPlayer {
            val item = MediaItem.Builder().setUri(uri).setSubtitleConfigurations(subs.mapIndexed { i, s ->
                MediaItem.SubtitleConfiguration.Builder(Uri.parse(s.dataUri))
                    .setMimeType(MimeTypes.APPLICATION_SUBRIP)
                    .setLanguage(s.language)
                    .setLabel(s.language ?: s.path.substringAfterLast('/'))
                    .setSelectionFlags(if (i == 0) C.SELECTION_FLAG_DEFAULT else 0)
                    .build()
            }).build()
            val p = ExoPlayer.Builder(context).setSeekBackIncrementMs(SEEK_MS).setSeekForwardIncrementMs(SEEK_MS).build()
            p.trackSelectionParameters = p.trackSelectionParameters.buildUpon()
                .setPreferredTextLanguage(subs.firstOrNull()?.language).setSelectUndeterminedTextLanguage(true).build()
            p.setMediaItem(item)
            return p
        }

        fun intent(context: Context, sourceId: String, path: String): Intent =
            Intent(context, VideoActivity::class.java).putExtra(EXTRA_SOURCE, sourceId).putExtra(EXTRA_PATH, path)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }
}

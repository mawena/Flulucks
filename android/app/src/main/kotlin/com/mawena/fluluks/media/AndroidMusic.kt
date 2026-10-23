package com.mawena.fluluks.media

import android.content.ComponentName
import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import com.google.common.util.concurrent.MoreExecutors
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.provider.WriteOps

/**
 * Lecteur réel de [Music] : un MediaController relié à [MusicService], sur le fil principal. L'état est publié à
 * chaque changement et toutes les 500 ms pendant la lecture ; une erreur du lecteur passe par [Music.onError].
 */
class AndroidMusic(private val context: Context) : MusicBackend {
    private val main = Handler(Looper.getMainLooper())
    private var controller: MediaController? = null
    private var sourceId: String? = null

    /** Noms de la file : le titre ne revient pas toujours par le MediaController (vide sur le Pixel). */
    private var titles: List<String> = emptyList()

    private val tick = object : Runnable {
        override fun run() {
            publish()
            if (controller?.isPlaying == true) main.postDelayed(this, POSITION_MS)
        }
    }

    private val listener = object : Player.Listener {
        override fun onEvents(player: Player, events: Player.Events) {
            publish()
            main.removeCallbacks(tick)
            if (player.isPlaying) main.postDelayed(tick, POSITION_MS)
        }

        override fun onPlayerError(error: PlaybackException) {
            Music.onError()
        }
    }

    override fun start(sourceId: String, paths: List<String>, startIndex: Int) {
        // URI préparées ici (fil du pont) : le fournisseur de flux d'un dossier distant les crée sans réseau
        val src = Files.source(sourceId)
        val names = paths.map(WriteOps::nameOf)
        val items = paths.map { p ->
            MediaItem.Builder().setMediaId(src.contentUri(p).toString())
                .setMediaMetadata(MediaMetadata.Builder().setTitle(WriteOps.nameOf(p)).build()).build()
        }
        main.post {
            this.sourceId = sourceId
            titles = names
            withController { c ->
                c.setMediaItems(items, startIndex, 0L)
                c.prepare()
                c.play()
            }
        }
    }

    override fun command(command: String, positionMs: Long?) {
        main.post {
            val c = controller ?: return@post
            when (command) {
                "play" -> c.play()
                "pause" -> c.pause()
                "next" -> c.seekToNextMediaItem()
                "previous" -> c.seekToPrevious()
                "back" -> c.seekBack()
                "forward" -> c.seekForward()
                "seek" -> c.seekTo(positionMs ?: 0L)
                "stop" -> Music.stop()
            }
        }
    }

    override fun stop() {
        main.post {
            main.removeCallbacks(tick)
            controller?.run {
                removeListener(listener)
                stop()
                clearMediaItems()
                release()
            }
            controller = null
            sourceId = null
            titles = emptyList()
        }
    }

    private fun withController(body: (MediaController) -> Unit) {
        controller?.let { return body(it) }
        val token = SessionToken(context, ComponentName(context, MusicService::class.java))
        val future = MediaController.Builder(context, token).buildAsync()
        future.addListener({
            val c = runCatching { future.get() }.getOrNull() ?: return@addListener Music.onError()
            c.addListener(listener)
            controller = c
            body(c)
        }, MoreExecutors.directExecutor())
    }

    private fun publish() {
        val c = controller ?: return
        val id = sourceId ?: return
        if (c.mediaItemCount == 0 || c.playbackState == Player.STATE_IDLE && !c.playWhenReady) return
        val d = c.duration
        Music.publish(
            MusicState(
                id, titles.getOrNull(c.currentMediaItemIndex) ?: c.mediaMetadata.title?.toString() ?: "",
                c.currentMediaItemIndex, c.mediaItemCount,
                c.isPlaying || (c.playWhenReady && c.playbackState == Player.STATE_BUFFERING),
                c.currentPosition, if (d == C.TIME_UNSET) -1 else d,
            ),
        )
    }

    private companion object {
        const val POSITION_MS = 500L
    }
}

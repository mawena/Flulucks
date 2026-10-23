package com.mawena.fluluks.media

import android.net.Uri
import androidx.annotation.OptIn
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture

/**
 * Service de lecture de la musique (phase 10) : ExoPlayer avec le focus audio et la pause au débranchement des
 * écouteurs, session Media3 et sa notification par défaut. Les morceaux arrivent par [AndroidMusic] (même processus).
 */
@OptIn(UnstableApi::class)
class MusicService : MediaSessionService() {
    private var session: MediaSession? = null

    override fun onCreate() {
        super.onCreate()
        val player = ExoPlayer.Builder(this)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MUSIC).build(), true)
            .setHandleAudioBecomingNoisy(true)
            .setSeekBackIncrementMs(VideoActivity.SEEK_MS)
            .setSeekForwardIncrementMs(VideoActivity.SEEK_MS)
            .build()
        session = MediaSession.Builder(this, player).setCallback(object : MediaSession.Callback {
            // l'URI voyage dans mediaId : la configuration locale d'un MediaItem n'est pas toujours transmise
            override fun onAddMediaItems(
                mediaSession: MediaSession, controller: MediaSession.ControllerInfo, mediaItems: MutableList<MediaItem>,
            ): ListenableFuture<MutableList<MediaItem>> =
                Futures.immediateFuture(mediaItems.map { it.buildUpon().setUri(Uri.parse(it.mediaId)).build() }.toMutableList())
        }).build()
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession? = session

    override fun onTaskRemoved(rootIntent: android.content.Intent?) {
        val p = session?.player
        if (p == null || !p.playWhenReady || p.mediaItemCount == 0) stopSelf()
    }

    override fun onDestroy() {
        session?.run {
            player.release()
            release()
        }
        session = null
        super.onDestroy()
    }
}

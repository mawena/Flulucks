package com.mawena.fluluks.media

import android.content.Context
import android.net.Uri
import androidx.annotation.OptIn
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.core.app.NotificationCompat
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.session.CommandButton
import androidx.media3.session.DefaultMediaNotificationProvider
import androidx.media3.session.MediaNotification
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import com.google.common.collect.ImmutableList
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import com.mawena.fluluks.R

/**
 * Service de lecture de la musique (phase 10) : ExoPlayer avec le focus audio et la pause au débranchement des
 * écouteurs, session Media3 et sa notification par défaut, à l'icône et à la couleur de Portail
 * ([PortailNotifications]). Les morceaux arrivent par [AndroidMusic] (même processus).
 */
@OptIn(UnstableApi::class)
class MusicService : MediaSessionService() {
    private var session: MediaSession? = null

    override fun onCreate() {
        super.onCreate()
        setMediaNotificationProvider(PortailNotifications(this))
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

/**
 * Notification de lecture de Media3, avec la silhouette de l'icône de Portail (`ic_launcher_mono`, comme les icônes à
 * thème d'Android 13) en petite icône et le terracotta de la charte (phase 11) en couleur d'accent.
 */
@OptIn(UnstableApi::class)
private class PortailNotifications(context: Context) : DefaultMediaNotificationProvider(context) {
    private val accent = context.getColor(R.color.portail_terracotta)

    init {
        setSmallIcon(R.drawable.ic_launcher_mono)
    }

    // seul endroit où Media3 prête le constructeur de la notification
    override fun addNotificationActions(
        mediaSession: MediaSession,
        mediaButtons: ImmutableList<CommandButton>,
        builder: NotificationCompat.Builder,
        actionFactory: MediaNotification.ActionFactory,
    ): IntArray {
        builder.setColor(accent)
        return super.addNotificationActions(mediaSession, mediaButtons, builder, actionFactory)
    }
}

package com.mawena.fluluks.media

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Bundle
import android.view.MotionEvent
import android.view.ViewConfiguration
import android.view.WindowManager
import android.widget.PopupMenu
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
 * Lecteur vidéo de Portail (phases 10 et 12) : plein écran, recul et avance de 10 s, sous-titres `.srt` trouvés à
 * côté de la vidéo (le premier actif). Portrait par défaut, rotation selon le réglage d'Android, boutons « Tourner »
 * et cadenas ([VideoOrientation]). La vidéo est lue par l'URI de contenu de sa source (fournisseur de flux pour un
 * dossier distant) ; l'écran reste allumé pendant la lecture. Erreur de lecture ou stockage fermé : message, puis
 * fermeture.
 */
@OptIn(UnstableApi::class)
class VideoActivity : Activity() {
    private var player: ExoPlayer? = null
    private lateinit var sourceId: String
    @Volatile private var closed = false
    private var mode: OrientationMode = OrientationMode.Auto
    private lateinit var overlay: VideoOverlay
    private lateinit var playerView: PlayerView
    private lateinit var gestures: SeekGestures
    private var controllerWasVisible = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sourceId = intent.getStringExtra(EXTRA_SOURCE) ?: return finish()
        val path = intent.getStringExtra(EXTRA_PATH) ?: return finish()
        requestedOrientation = VideoOrientation.requested(mode)
        val view = PlayerView(this).apply {
            setShowSubtitleButton(false) // remplacé par le bouton Pistes
            setShowNextButton(false)
            setShowPreviousButton(false)
        }
        playerView = view
        val vc = ViewConfiguration.get(this)
        gestures = SeekGestures(resources.displayMetrics.density, vc.scaledTouchSlop.toFloat(), vc.scaledDoubleTapSlop.toFloat(),
            ViewConfiguration.getDoubleTapTimeout().toLong(), stepMs = SEEK_MS)
        overlay = VideoOverlay(this).apply {
            attach(view)
            setTitle(path.substringAfterLast('/'))
            rotateButton.setOnClickListener {
                setMode(VideoOrientation.rotate(mode, resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE))
            }
            lockButton.setOnClickListener { setMode(VideoOrientation.toggleLock(mode)) }
            tracksButton.setOnClickListener { showTracks(it) }
        }
        view.setControllerVisibilityListener(PlayerView.ControllerVisibilityListener { v ->
            overlay.setBarVisible(v == android.view.View.VISIBLE)
        })
        setContentView(overlay.root)
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

    /**
     * Gestes comme VLC ([SeekGestures]) : chaque événement passe par l'automate avant le lecteur, qui ne reçoit que ce
     * que l'automate lui laisse.
     */
    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        if (!::gestures.isInitialized) return super.dispatchTouchEvent(ev)
        val o = when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN ->
                gestures.down(ev.x, ev.y, ev.eventTime, playerView.width, onControl(ev.x, ev.y), playback())
            MotionEvent.ACTION_MOVE -> gestures.move(ev.x, ev.y)
            MotionEvent.ACTION_UP -> gestures.up(ev.x, ev.y, ev.eventTime)
            MotionEvent.ACTION_CANCEL -> gestures.cancel()
            else -> null
        } ?: return super.dispatchTouchEvent(ev)
        if (ev.actionMasked == MotionEvent.ACTION_DOWN && o.dispatch) controllerWasVisible = playerView.isControllerFullyVisible
        if (o.cancelChildren) {
            val c = MotionEvent.obtain(ev).apply { action = MotionEvent.ACTION_CANCEL }
            super.dispatchTouchEvent(c)
            c.recycle()
        }
        o.effect?.let(::onGesture)
        return if (o.dispatch) super.dispatchTouchEvent(ev) else true
    }

    /** Point sur une commande affichée : barre du haut, barre du bas, barre de progression, boutons du centre. */
    private fun onControl(x: Float, y: Float): Boolean {
        val ids = listOf(androidx.media3.ui.R.id.exo_bottom_bar, androidx.media3.ui.R.id.exo_progress,
            androidx.media3.ui.R.id.exo_center_controls)
        val rects = overlay.controlRects() + ids.mapNotNull { VideoOverlay.windowRect(playerView.findViewById(it)) }
        return rects.any { it.contains(x.toInt(), y.toInt()) }
    }

    private fun playback(): SeekGestures.Playback {
        val p = player ?: return SeekGestures.Playback(0, -1, false)
        val d = p.duration
        return SeekGestures.Playback(p.currentPosition, if (d == C.TIME_UNSET) -1 else d, p.isCurrentMediaItemSeekable)
    }

    private fun onGesture(e: SeekGestures.Effect) {
        when (e) {
            is SeekGestures.Effect.Preview -> overlay.showInfo(TimeText.preview(e.targetMs, e.deltaMs, playback().durationMs), null)
            is SeekGestures.Effect.DragEnd -> {
                overlay.hideInfo()
                if (e.deltaMs != 0L) player?.seekTo(e.targetMs)
            }
            is SeekGestures.Effect.Jump -> {
                val p = player ?: return
                p.seekTo(TimeText.clamp(p.currentPosition + e.stepMs, playback().durationMs))
                overlay.showJump(e.stepMs > 0, TimeText.seconds(e.totalMs), JUMP_MS)
                // le premier toucher du double a montré ou caché les commandes : on les remet comme avant
                if (e.restoreControls) {
                    if (controllerWasVisible) playerView.showController() else playerView.hideController()
                }
            }
        }
    }

    /**
     * Menu « Pistes » : sous-menus Audio et Sous-titres, un choix coché par sous-menu. Le choix s'applique sur
     * l'instantané des pistes qui a servi à construire le menu.
     */
    private fun showTracks(anchor: android.view.View) {
        val p = player ?: return
        val tracks = p.currentTracks
        val infos = PlayerTracks.infos(tracks)
        val menu = PopupMenu(this, anchor)
        fun section(name: String, group: Int, kind: TrackKind, choices: List<TrackChoice>, empty: String?) {
            val sub = menu.menu.addSubMenu(name)
            if (choices.isEmpty() && empty != null) sub.add(empty).isEnabled = false
            choices.forEachIndexed { i, c ->
                sub.add(group, i, i, c.title).setChecked(c.checked).setOnMenuItemClickListener {
                    PlayerTracks.apply(p, tracks, c, kind)
                    true
                }
            }
            sub.setGroupCheckable(group, true, true)
        }
        section(TrackMenu.AUDIO, 1, TrackKind.AUDIO, TrackMenu.audio(infos), TrackMenu.NO_AUDIO)
        section(TrackMenu.TEXT, 2, TrackKind.TEXT, TrackMenu.text(infos), null)
        menu.show()
        playerView.showController()
    }

    /** Nouveau mode d'orientation : demandé à Android, montré par le cadenas et une bulle brève. */
    private fun setMode(m: OrientationMode) {
        mode = m
        requestedOrientation = VideoOrientation.requested(m)
        overlay.setLocked(VideoOrientation.isLocked(m), VideoOrientation.lockDescription(m))
        overlay.showInfo(VideoOrientation.message(m), INFO_MS)
        playerView.showController() // relance la temporisation des commandes
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
        private const val INFO_MS = 1_500L
        private const val JUMP_MS = 700L

        /**
         * Lecteur de la vidéo [uri] avec ses sous-titres (le premier actif), pas encore préparé ; repris tel quel par
         * l'essai instrumenté.
         */
        fun newPlayer(context: Context, uri: Uri, subs: List<SubtitleTrack>): ExoPlayer {
            val item = MediaItem.Builder().setUri(uri).setSubtitleConfigurations(subs.mapIndexed { i, s ->
                MediaItem.SubtitleConfiguration.Builder(Uri.parse(s.dataUri))
                    .setMimeType(MimeTypes.APPLICATION_SUBRIP)
                    .setLanguage(s.language)
                    .setLabel(s.path.substringAfterLast('/'))
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

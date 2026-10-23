package com.mawena.fluluks.media

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.Player
import androidx.media3.common.TrackSelectionOverride
import androidx.media3.common.Tracks
import androidx.media3.common.util.UnstableApi

/** Passage entre les pistes de Media3 et le menu « Pistes » ([TrackMenu]) du lecteur vidéo (phase 12). */
@OptIn(UnstableApi::class)
object PlayerTracks {
    /** Pistes audio et de sous-titres de [tracks] que l'appareil sait lire. */
    fun infos(tracks: Tracks): List<TrackInfo> = tracks.groups.flatMapIndexed { g, group ->
        val kind = when (group.type) {
            C.TRACK_TYPE_AUDIO -> TrackKind.AUDIO
            C.TRACK_TYPE_TEXT -> TrackKind.TEXT
            else -> return@flatMapIndexed emptyList()
        }
        (0 until group.length).filter { group.isTrackSupported(it) }.map { i ->
            val f = group.getTrackFormat(i)
            TrackInfo(kind, g, i, f.language, f.label, if (f.channelCount == Format.NO_VALUE) -1 else f.channelCount,
                group.isTrackSelected(i))
        }
    }

    /** Joue la piste [choice] de type [kind], choisie dans le menu construit sur [tracks] ; texte sans piste : coupé. */
    fun apply(player: Player, tracks: Tracks, choice: TrackChoice, kind: TrackKind) {
        val type = if (kind == TrackKind.AUDIO) C.TRACK_TYPE_AUDIO else C.TRACK_TYPE_TEXT
        val t = choice.track
        val b = player.trackSelectionParameters.buildUpon()
        if (t == null) {
            b.clearOverridesOfType(type).setTrackTypeDisabled(type, true)
        } else {
            b.setTrackTypeDisabled(type, false)
                .setOverrideForType(TrackSelectionOverride(tracks.groups[t.group].mediaTrackGroup, t.index))
        }
        player.trackSelectionParameters = b.build()
    }
}

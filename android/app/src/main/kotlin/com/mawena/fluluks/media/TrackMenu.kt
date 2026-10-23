package com.mawena.fluluks.media

import java.util.Locale

enum class TrackKind { AUDIO, TEXT }

/**
 * Piste audio ou de sous-titres de la vidéo : [group] et [index] situent la piste dans `Tracks.groups` et dans son
 * groupe ; [channels] ≤ 0 si inconnu ; [selected] si le lecteur la joue.
 */
data class TrackInfo(
    val kind: TrackKind,
    val group: Int,
    val index: Int,
    val language: String?,
    val label: String?,
    val channels: Int,
    val selected: Boolean,
)

/** Entrée du menu « Pistes » : [track] nul = sous-titres désactivés. */
data class TrackChoice(val title: String, val track: TrackInfo?, val checked: Boolean)

/** Menu « Pistes » du lecteur vidéo (phase 12) : titres en français et choix proposés, sans Media3. */
object TrackMenu {
    const val TITLE = "Pistes"
    const val AUDIO = "Audio"
    const val TEXT = "Sous-titres"
    const val OFF = "Désactivés"
    const val NO_AUDIO = "Aucune piste audio"

    /** Nom français de la langue [code] (« fr », « pt-BR »…), avec une capitale ; null si absente ou indéterminée. */
    fun languageName(code: String?): String? {
        if (code.isNullOrBlank() || code.equals("und", ignoreCase = true)) return null
        val name = Locale.forLanguageTag(code).getDisplayName(Locale.FRENCH).ifBlank { code }
        return name.replaceFirstChar { it.titlecase(Locale.FRENCH) }
    }

    private fun channelsText(n: Int): String? = when {
        n <= 0 -> null
        n == 1 -> "Mono"
        n == 2 -> "Stéréo"
        n == 6 -> "5.1"
        n == 8 -> "7.1"
        else -> "$n canaux"
    }

    /** Titre de [track], [number]-ième de sa liste : langue, libellé, canaux (audio), ou « Piste N ». */
    fun title(track: TrackInfo, number: Int): String {
        val lang = languageName(track.language)
        val label = track.label?.trim()?.takeIf {
            it.isNotEmpty() && !it.equals(lang, ignoreCase = true) && !it.equals(track.language, ignoreCase = true)
        }
        val parts = listOfNotNull(lang, label, if (track.kind == TrackKind.AUDIO) channelsText(track.channels) else null)
        return if (parts.isEmpty()) "Piste $number" else parts.joinToString(" · ")
    }

    private fun choices(tracks: List<TrackInfo>): List<TrackChoice> {
        val titles = tracks.mapIndexed { i, t -> title(t, i + 1) }
        val counts = titles.groupingBy { it }.eachCount()
        return tracks.mapIndexed { i, t ->
            val title = if (counts.getValue(titles[i]) > 1) "${titles[i]} (${i + 1})" else titles[i]
            TrackChoice(title, t, t.selected)
        }
    }

    /** Pistes audio, dans l'ordre de la vidéo ; vide s'il n'y en a pas. */
    fun audio(tracks: List<TrackInfo>): List<TrackChoice> = choices(tracks.filter { it.kind == TrackKind.AUDIO })

    /** « Désactivés » (coché si aucun sous-titre n'est joué), puis les pistes de sous-titres. */
    fun text(tracks: List<TrackInfo>): List<TrackChoice> {
        val list = choices(tracks.filter { it.kind == TrackKind.TEXT })
        return listOf(TrackChoice(OFF, null, list.none { it.checked })) + list
    }
}

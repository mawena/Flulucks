package com.mawena.fluluks

import com.mawena.fluluks.media.TrackChoice
import com.mawena.fluluks.media.TrackInfo
import com.mawena.fluluks.media.TrackKind
import com.mawena.fluluks.media.TrackMenu
import com.mawena.fluluks.media.TrackMenu.languageName
import com.mawena.fluluks.media.TrackMenu.title
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Pistes audio et sous-titres décrites en français pour le menu « Pistes » du lecteur vidéo (phase 12). */
class TrackMenuTest {
    private fun a(g: Int, lang: String?, label: String? = null, ch: Int = 2, sel: Boolean = false) =
        TrackInfo(TrackKind.AUDIO, g, 0, lang, label, ch, sel)

    private fun t(g: Int, lang: String?, label: String?, sel: Boolean = false) =
        TrackInfo(TrackKind.TEXT, g, 0, lang, label, -1, sel)

    @Test fun languageNames() {
        assertEquals("Français", languageName("fr"))
        assertEquals("Anglais", languageName("en"))
        assertNull(languageName(null))
        assertNull(languageName("und"))
        assertNull(languageName(""))
        assertEquals("Portugais (Brésil)", languageName("pt-BR"))
    }

    @Test fun audioTitles() {
        assertEquals("Français · Stéréo", title(a(0, "fr"), 1))
        assertEquals("Anglais · Commentaires · 5.1", title(a(1, "en", "Commentaires", 6), 2))
        assertEquals("Mono", title(a(2, null, ch = 1), 3))
        assertEquals("Piste 4", title(a(3, null, ch = -1), 4))
        assertEquals("Français · 7.1", title(a(4, "fr", "Français", 8), 5)) // libellé égal à la langue : une fois
        assertEquals("Allemand · 3 canaux", title(a(5, "de", "de", 3), 6)) // libellé égal au code : omis
    }

    @Test fun textTitlesHaveNoChannels() = assertEquals("Français · film.fr.srt", title(t(0, "fr", "film.fr.srt"), 1))

    @Test fun audioChoicesKeepOrderAndSelection() {
        val c = TrackMenu.audio(listOf(a(0, "fr", sel = true), a(1, "en")))
        assertEquals(listOf("Français · Stéréo", "Anglais · Stéréo"), c.map { it.title })
        assertEquals(listOf(true, false), c.map { it.checked })
        assertEquals(1, c[1].track!!.group)
    }

    @Test fun duplicateTitlesAreNumbered() = assertEquals(
        listOf("Français · Stéréo (1)", "Français · Stéréo (2)", "Anglais · Stéréo"),
        TrackMenu.audio(listOf(a(0, "fr"), a(1, "fr"), a(2, "en"))).map { it.title },
    )

    /** L'activité affiche alors « Aucune piste audio », désactivé. */
    @Test fun noAudioTrackSaysSo() {
        assertTrue(TrackMenu.audio(emptyList()).isEmpty())
        assertEquals("Aucune piste audio", TrackMenu.NO_AUDIO)
    }

    @Test fun textChoicesStartWithOff() {
        val c = TrackMenu.text(listOf(t(0, "fr", "film.fr.srt", sel = true), t(1, null, "film.srt")))
        assertEquals(listOf("Désactivés", "Français · film.fr.srt", "film.srt"), c.map { it.title })
        assertEquals(listOf(false, true, false), c.map { it.checked })
        assertNull(c[0].track)
    }

    @Test fun textChoicesWithoutTracksOfferOnlyOff() =
        assertEquals(listOf(TrackChoice("Désactivés", null, true)), TrackMenu.text(emptyList()))

    @Test fun offIsCheckedWhenNoTextSelected() = assertTrue(TrackMenu.text(listOf(t(0, "fr", null))).first().checked)
}

package com.mawena.fluluks

import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.media.Subtitles
import java.io.File
import java.util.Base64
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Sous-titres SubRip trouvés à côté d'une vidéo, décodés et passés au lecteur en `data:` URI. */
class SubtitlesTest {
    @get:Rule val tmp = TemporaryFolder()

    private fun src() = LocalSource("phone", "phone", "Téléphone", tmp.root) { true }
    private fun put(name: String, bytes: ByteArray) = File(tmp.root, name).writeBytes(bytes)

    @Test fun findsSameNameAndLanguages() {
        put("Film.mkv", ByteArray(1))
        for (n in listOf("film.srt", "FILM.fr.srt", "film.eng.srt", "autre.srt", "film.francais.srt", "film.txt")) {
            put(n, "1\n00:00:01,000 --> 00:00:02,000\nx\n".toByteArray())
        }
        val tracks = Subtitles.load(src(), "/Film.mkv")
        assertEquals(listOf("/film.srt", "/film.eng.srt", "/FILM.fr.srt"), tracks.map { it.path })
        assertEquals(listOf(null, "eng", "fr"), tracks.map { it.language })
    }

    @Test fun windows1252Decoded() {
        assertEquals("Été €", Subtitles.decode(byteArrayOf(0xC9.toByte(), 't'.code.toByte(), 0xE9.toByte(), ' '.code.toByte(), 0x80.toByte())))
        assertEquals("Été", Subtitles.decode("﻿Été".toByteArray())) // UTF-8 avec BOM : BOM retiré
        assertEquals("Été", Subtitles.decode("Été".toByteArray()))
    }

    @Test fun tooBigSkipped() {
        put("v.mp4", ByteArray(1))
        put("v.srt", ByteArray((Subtitles.MAX + 1).toInt()) { 'a'.code.toByte() })
        put("v.fr.srt", "ok".toByteArray())
        assertEquals(listOf("/v.fr.srt"), Subtitles.load(src(), "/v.mp4").map { it.path })
    }

    @Test fun dataUriRoundTrip() {
        val uri = Subtitles.dataUri("Ça va ?\n")
        assertTrue(uri.startsWith("data:application/x-subrip;base64,"))
        assertEquals("Ça va ?\n", String(Base64.getDecoder().decode(uri.substringAfter(",")), Charsets.UTF_8))
    }
}

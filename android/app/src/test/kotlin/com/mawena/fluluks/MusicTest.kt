package com.mawena.fluluks

import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.media.Music
import com.mawena.fluluks.media.MusicBackend
import com.mawena.fluluks.media.MusicState
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** File d'attente et arrêt du lecteur de musique, avec un faux lecteur. */
class MusicTest {
    @get:Rule val tmp = TemporaryFolder()

    private class Backend : MusicBackend {
        val calls = ArrayList<String>()
        override fun start(sourceId: String, paths: List<String>, startIndex: Int) {
            calls += "start $sourceId ${paths.joinToString(",")} $startIndex"
        }
        override fun command(command: String, positionMs: Long?) { calls += "command $command $positionMs" }
        override fun stop() { calls += "stop" }
    }

    private val sources = HashMap<String, FileSource>()
    private lateinit var backend: Backend

    @Before fun setup() {
        sources["phone"] = LocalSource("phone", "phone", "Téléphone", tmp.root) { true }
        Files.setResolverForTests { sources[it] }
        backend = Backend()
        Music.resetForTests(backend)
    }

    @After fun teardown() {
        Music.resetForTests(null)
        Files.setResolverForTests(null)
    }

    @Test fun queueFromFolder() {
        val shown = listOf("/d/a.mp3", "/d/notes.txt", "/d/b.FLAC", "/d/c.ogg", "/d/photo.jpg")
        assertEquals(listOf("/d/a.mp3", "/d/b.FLAC", "/d/c.ogg") to 1, Music.queueFrom(shown, 2))
        assertEquals(listOf("/d/a.mp3", "/d/b.FLAC", "/d/c.ogg") to 0, Music.queueFrom(shown, 1)) // pas un son : début
        Music.play("phone", shown, 3)
        assertEquals(listOf("start phone /d/a.mp3,/d/b.FLAC,/d/c.ogg 2"), backend.calls)
    }

    @Test fun stopsWhenSourceGone() {
        Music.play("phone", listOf("/a.mp3"), 0)
        Music.publish(MusicState("phone", "a.mp3", 0, 1, true, 1000, 5000))
        Music.checkSources()
        assertEquals("a.mp3", Music.state.value.first?.title) // stockage encore là : rien ne change
        sources.remove("phone")
        Music.checkSources()
        assertTrue(backend.calls.contains("stop"))
        assertNull(Music.state.value.first)
        assertEquals("Lecture arrêtée : stockage fermé", Music.state.value.second)
    }

    @Test fun errorWithSourceGoneSaysClosed() {
        Music.play("phone", listOf("/a.mp3"), 0)
        sources.remove("phone")
        Music.onError()
        assertEquals("Lecture arrêtée : stockage fermé", Music.state.value.second)
        Music.resetForTests(backend)
        sources["phone"] = LocalSource("phone", "phone", "Téléphone", tmp.root) { true }
        Music.play("phone", listOf("/a.mp3"), 0)
        Music.onError()
        assertEquals("Lecture impossible", Music.state.value.second)
    }
}

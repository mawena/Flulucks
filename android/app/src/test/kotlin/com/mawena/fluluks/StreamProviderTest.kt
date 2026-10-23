package com.mawena.fluluks

import com.mawena.fluluks.provider.StreamTokens
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Jetons du fournisseur de flux : un jeton aléatoire par fichier servi, oublié avec sa source. */
class StreamProviderTest {
    @After fun teardown() = StreamTokens.clearForTests()

    @Test fun tokenServesFile() {
        val t = StreamTokens.register("remote:a", "/films/été.mkv", 123_456L)
        assertTrue(t.matches(Regex("[0-9a-f]{32}")))
        val s = StreamTokens.get(t)!!
        assertEquals("remote:a", s.sourceId); assertEquals("/films/été.mkv", s.path)
        assertEquals("été.mkv", s.name); assertEquals(123_456L, s.size)
    }

    @Test fun sameFileSameToken() {
        val a = StreamTokens.register("remote:a", "/x.jpg", 10)
        assertEquals(a, StreamTokens.register("remote:a", "/x.jpg", 10))
        assertNotEquals(a, StreamTokens.register("remote:a", "/y.jpg", 10))
        assertNotEquals(a, StreamTokens.register("remote:b", "/x.jpg", 10))
        // taille changée (fichier réécrit) : nouveau jeton
        assertNotEquals(a, StreamTokens.register("remote:a", "/x.jpg", 11))
    }

    @Test fun unknownTokenRefused() {
        assertNull(StreamTokens.get("0".repeat(32)))
        assertNull(StreamTokens.get("../../etc"))
    }

    @Test fun forgetSource() {
        val a = StreamTokens.register("remote:a", "/x.jpg", 10)
        val b = StreamTokens.register("remote:b", "/x.jpg", 10)
        StreamTokens.forget("remote:a")
        assertNull(StreamTokens.get(a))
        assertEquals("remote:b", StreamTokens.get(b)!!.sourceId)
    }
}

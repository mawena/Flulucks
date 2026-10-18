package com.mawena.fluluks

import com.mawena.fluluks.provider.DocNames
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DocNamesTest {
    @Test fun numberedLikeAndroid() {
        assertEquals("a (1).txt", DocNames.numbered("a.txt", 1))
        assertEquals("a (1)", DocNames.numbered("a", 1))
        assertEquals(".bashrc (1)", DocNames.numbered(".bashrc", 1))
        assertEquals("a.tar (1).gz", DocNames.numbered("a.tar.gz", 1))
        assertEquals("a (12).txt", DocNames.numbered("a.txt", 12))
    }

    @Test fun validity() {
        for (bad in listOf("", ".", "..", "a/b", "a\u0000b", "é".repeat(128))) assertFalse(bad, DocNames.isValid(bad))
        assertTrue(DocNames.isValid("x".repeat(255)))
        assertFalse(DocNames.isValid("x".repeat(256)))
        assertTrue(DocNames.isValid("☀ 🚀.txt"))
        assertTrue(DocNames.isValid("...")) // seul « . » et « .. » sont réservés
    }

    @Test fun uniqueTakesFirstFree() {
        val taken = mutableSetOf("a.txt", "a (1).txt", "a (2).txt")
        assertEquals("a (3).txt", DocNames.unique("a.txt") { it in taken })
        assertEquals("b.txt", DocNames.unique("b.txt") { it in taken })
        assertNull(DocNames.unique("c") { true })
        // variante trop longue : sautée, pas de nom invalide rendu
        assertNull(DocNames.unique("x".repeat(255)) { it == "x".repeat(255) })
    }
}

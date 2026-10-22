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

    @Test fun sanitizeForbiddenChars() {
        assertEquals("photo_1.jpg", DocNames.sanitize("photo:1.jpg"))
        assertEquals("a_b_c_d_e_f_g_h_i_", DocNames.sanitize("a/b\\c:d*e?f\"g<h>i|"))
        assertEquals("tab_nl_", DocNames.sanitize("tab\tnl\n"))
        assertEquals("déjà ☀.txt", DocNames.sanitize("déjà ☀.txt"))
    }

    @Test fun sanitizeTrailingDotsAndSpaces() {
        assertEquals("nom", DocNames.sanitize("nom. . "))
        assertEquals(".bashrc", DocNames.sanitize(".bashrc"))
    }

    @Test fun sanitizeLongNameKeepsExtension() {
        val n = DocNames.sanitize("é".repeat(150) + ".jpeg") // 300 octets
        assertTrue(n.endsWith(".jpeg"))
        assertTrue(n.toByteArray(Charsets.UTF_8).size <= 255)
        assertTrue(DocNames.isValid(n))
        assertEquals(n, DocNames.sanitize(n))
        val emoji = DocNames.sanitize("🚀".repeat(70)) // 280 octets, sans extension : pas de demi-caractère
        assertEquals("🚀".repeat(63), emoji)
    }

    @Test fun sanitizeEmpty() {
        for (n in listOf("", ".", "..", "   ", "...")) assertEquals(n, "fichier", DocNames.sanitize(n))
    }
}

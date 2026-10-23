package com.mawena.fluluks

import com.mawena.fluluks.files.QuickFilters
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Filtres rapides : classement d'un fichier, fenêtre des récents, chemin de l'index converti en stockage. */
class QuickFiltersTest {
    @Test fun classifyByExtensionAndMime() {
        val k = QuickFilters::kind
        assertEquals(setOf("photos"), k("IMG_0001.JPG", null))
        assertEquals(setOf("photos"), k("sans-extension", "image/png"))
        assertEquals(setOf("photos"), k("p.heic", null))
        assertEquals(setOf("videos"), k("v.mp4", null))
        assertEquals(setOf("videos"), k("x", "video/3gpp"))
        assertEquals(setOf("music"), k("s.flac", null))
        assertEquals(setOf("music"), k("x", "audio/mpeg"))
        for (e in listOf("pdf", "doc", "docx", "odt", "xls", "xlsx", "ods", "ppt", "pptx", "odp", "txt", "md", "epub")) {
            assertEquals(e, setOf("documents"), k("f.$e", null))
            assertEquals(e, setOf("documents"), k("F.${e.uppercase()}", null))
        }
        assertEquals(setOf("archives"), k("a.tar.gz", null))
        assertEquals(setOf("archives"), k("a.zip", "application/zip"))
        assertEquals(setOf("archives"), k("photo.jpg.zip", null)) // l'extension finale décide
        assertEquals(emptySet<String>(), k("x.bin", "application/octet-stream"))
        assertEquals(emptySet<String>(), k("pdf", null))
    }

    @Test fun recentIs30Days() {
        val now = 1_800_000_000_000L
        val day = 86_400L
        assertEquals(now / 1000 - 30 * day, QuickFilters.recentSince(now))
        assertTrue(QuickFilters.isRecent(now / 1000 - 29 * day, now))
        assertTrue(QuickFilters.isRecent(now / 1000 - 30 * day, now))
        assertFalse(QuickFilters.isRecent(now / 1000 - 31 * day, now))
    }

    @Test fun absolutePathToSource() {
        val roots = listOf("phone" to "/storage/emulated/0", "sd:1234-5678" to "/storage/1234-5678")
        assertEquals("phone" to "/Download/a.pdf", QuickFilters.locate("/storage/emulated/0/Download/a.pdf", roots))
        assertEquals("sd:1234-5678" to "/DCIM/b.jpg", QuickFilters.locate("/storage/1234-5678/DCIM/b.jpg", roots))
        assertNull(QuickFilters.locate("/storage/emulated/01/x.pdf", roots)) // préfixe, mais pas un dossier
        assertNull(QuickFilters.locate("/data/x.pdf", roots))
        assertNull(QuickFilters.locate("/storage/emulated/0", roots)) // la racine elle-même
        assertNull(QuickFilters.locate("/storage/emulated/0/.Portail-corbeille/files/x.pdf", roots)) // corbeille
        assertNull(QuickFilters.locate("/storage/emulated/0/.cache/x.pdf", roots)) // cachés, comme dans l'explorateur
        assertNull(QuickFilters.locate("/storage/emulated/0/a/../b.pdf", roots)) // chemin non normalisé
    }
}

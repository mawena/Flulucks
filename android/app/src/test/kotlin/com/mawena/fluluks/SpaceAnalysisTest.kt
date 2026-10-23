package com.mawena.fluluks

import com.mawena.fluluks.files.Entry
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.ProtectedDirException
import com.mawena.fluluks.files.SpaceAnalysis
import java.io.File
import java.nio.file.Files as NioFiles
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Analyse de l'espace sur une source locale (dossier temporaire), lue à travers une source qui compte ses listes. */
class SpaceAnalysisTest {
    @get:Rule val tmp = TemporaryFolder()

    private val root: File get() = File(tmp.root, "racine").apply { mkdirs() }

    /** Source locale dont chaque `list` est comptée et peut déclencher une action. */
    private class Counting(val s: FileSource) : FileSource by s {
        var lists = 0
        var onList: (String, Int) -> Unit = { _, _ -> }
        override fun list(path: String): List<Entry> {
            onList(path, ++lists)
            return s.list(path)
        }
    }

    private lateinit var src: Counting

    private fun use(): Counting {
        src = Counting(LocalSource("phone", "phone", "Téléphone", root) { true })
        Files.setResolverForTests { if (it == "phone") src else null }
        return src
    }

    @After fun teardown() {
        SpaceAnalysis.clearForTests()
        Files.setResolverForTests(null)
    }

    private fun file(path: String, size: Int) = File(root, path).apply { parentFile!!.mkdirs(); writeBytes(ByteArray(size)) }

    @Test fun sizesSumUp() {
        file("a.bin", 100); file("D/b.bin", 200); file("D/E/c.bin", 300); file("D/E/d.bin", 50); file(".cache/x", 10)
        File(root, "vide").mkdir()
        NioFiles.createSymbolicLink(File(root, "lien").toPath(), File(tmp.root, "dehors").apply { mkdirs(); File(this, "gros").writeBytes(ByteArray(5000)) }.toPath())
        use()
        val r = SpaceAnalysis.run("phone", "/")
        assertEquals("/", r.path)
        assertEquals(660L, r.totalBytes) // fichiers cachés compris, lien non suivi
        assertEquals(5, r.fileCount)
        assertFalse(r.truncated)
        // fichiers et dossiers seulement, du plus gros au plus petit (liens et fichiers spéciaux : rien)
        assertEquals(listOf("D", "a.bin", ".cache", "vide"), r.children.map { it.name })
        assertEquals(listOf(550L, 100L, 10L, 0L), r.children.map { it.size })
        val d = r.children.first()
        assertTrue(d.isDir); assertEquals(3, d.fileCount); assertEquals("/D", d.path)
        assertEquals(0, r.otherCount)
        val sub = SpaceAnalysis.children("phone", "/D")
        assertEquals(550L, sub.totalBytes)
        assertEquals(listOf("E" to 350L, "b.bin" to 200L), sub.children.map { it.name to it.size })
        assertEquals(listOf("/D/E/c.bin", "/D/b.bin", "/D/E/d.bin"), sub.biggest.map { it.path }) // sous /D seulement
    }

    @Test fun biggestFilesTop100() {
        for (i in 1..150) file("d${i % 7}/f$i.bin", i)
        use()
        val r = SpaceAnalysis.run("phone", "/")
        assertEquals(100, r.biggest.size)
        assertEquals((150 downTo 51).map { it.toLong() }, r.biggest.map { it.size })
        assertEquals((1..150).sum().toLong(), r.totalBytes)
    }

    @Test fun manyChildrenSummarized() {
        for (i in 1..(SpaceAnalysis.MAX_CHILDREN + 5)) file("f$i.bin", i)
        use()
        val r = SpaceAnalysis.run("phone", "/")
        assertEquals(SpaceAnalysis.MAX_CHILDREN, r.children.size)
        assertEquals(5, r.otherCount)
        assertEquals((1..5).sum().toLong(), r.otherBytes)
        assertEquals(r.totalBytes, r.children.sumOf { it.size } + r.otherBytes)
    }

    @Test fun drillDownUsesCache() {
        file("D/E/c.bin", 300); file("D/b.bin", 200)
        val s = use()
        SpaceAnalysis.run("phone", "/")
        val before = s.lists
        file("D/nouveau.bin", 999) // invisible jusqu'à la prochaine analyse
        val sub = SpaceAnalysis.children("phone", "/D")
        assertEquals(before, s.lists)
        assertEquals(500L, sub.totalBytes)
        assertEquals(listOf("E", "b.bin"), sub.children.map { it.name })
        // tout dossier parcouru se relit de la mémoire ; une nouvelle analyse voit le nouveau fichier
        val again = SpaceAnalysis.children("phone", "/D/E")
        assertEquals(before, s.lists)
        assertEquals(300L, again.totalBytes)
        SpaceAnalysis.run("phone", "/D")
        assertEquals(1499L, SpaceAnalysis.children("phone", "/D").totalBytes)
    }

    @Test fun unreadableDirSkipped() {
        file("a.bin", 100); file("D/b.bin", 200); file("D/P/secret.bin", 5000)
        val s = use()
        s.onList = { path, _ -> if (path == "/D/P") throw ProtectedDirException() }
        val r = SpaceAnalysis.run("phone", "/")
        assertEquals(300L, r.totalBytes)
        assertEquals(listOf("D" to 200L, "a.bin" to 100L), r.children.map { it.name to it.size })
        // le dossier de départ illisible : erreur
        s.onList = { path, _ -> if (path == "/") throw ProtectedDirException() }
        try {
            SpaceAnalysis.run("phone", "/")
            throw AssertionError("accepté")
        } catch (e: Exception) {
            assertTrue(e.message, e !is AssertionError)
        }
    }

    @Test fun cancelStops() {
        for (i in 1..20) file("d$i/f.bin", 10)
        val s = use()
        s.onList = { _, n -> if (n == 5) SpaceAnalysis.cancel() }
        val r = SpaceAnalysis.run("phone", "/")
        assertTrue(r.truncated)
        assertTrue(r.totalBytes < 200L)
        // la suivante repart de zéro
        s.onList = { _, _ -> }
        val full = SpaceAnalysis.run("phone", "/")
        assertFalse(full.truncated)
        assertEquals(200L, full.totalBytes)
    }
}

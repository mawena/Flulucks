package com.mawena.fluluks

import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.Search
import com.mawena.fluluks.files.Trash
import java.io.File
import java.io.FileNotFoundException
import java.time.LocalDateTime
import java.time.ZoneId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Corbeille d'un stockage, sur une source locale (dossier temporaire). */
class TrashTest {
    @get:Rule val tmp = TemporaryFolder()

    private val root: File get() = tmp.root
    private fun src(access: Boolean = true) = LocalSource("phone", "phone", "Téléphone", root) { access }
    private fun local(path: String, text: String = "x") = File(root, path).apply { parentFile!!.mkdirs(); writeText(text) }
    private val day = 86_400_000L
    private val t0 = LocalDateTime.of(2026, 10, 23, 2, 5, 0).atZone(ZoneId.systemDefault()).toInstant().toEpochMilli()

    private inline fun <reified T : Throwable> refused(block: () -> Unit): T {
        try { block() } catch (t: Throwable) { if (t is T) return t; throw t }
        throw AssertionError("accepté")
    }

    @Test fun deleteMovesToTrash() {
        local("d/x.txt", "contenu"); local("D2/y.txt", "yy"); local("D2/z.txt", "zzz")
        val s = src()
        Trash.moveToTrash(s, listOf("/d/x.txt", "/D2"), t0)
        assertFalse(File(root, "d/x.txt").exists()); assertFalse(File(root, "D2").exists())
        assertTrue(File(root, "d").isDirectory) // le dossier parent reste
        assertEquals("contenu", File(root, ".Portail-corbeille/files/x.txt").readText())
        assertEquals("yy", File(root, ".Portail-corbeille/files/D2/y.txt").readText())
        val items = Trash.list(s).associateBy { it.name }
        assertEquals(setOf("x.txt", "D2"), items.keys)
        val x = items["x.txt"]!!
        assertEquals("x.txt", x.id); assertEquals("/d/x.txt", x.originalPath); assertEquals(7L, x.size); assertFalse(x.isDir)
        assertEquals(t0, x.deletedAtMs)
        val d2 = items["D2"]!!
        assertEquals("/D2", d2.originalPath); assertEquals(5L, d2.size); assertTrue(d2.isDir)
    }

    @Test fun trashInfoFormat() {
        local("dossier/café noir+1.txt")
        Trash.moveToTrash(src(), listOf("/dossier/café noir+1.txt"), t0)
        assertEquals(
            listOf("[Trash Info]", "Path=dossier/caf%C3%A9%20noir%2B1.txt", "DeletionDate=2026-10-23T02:05:00"),
            File(root, ".Portail-corbeille/info/café noir+1.txt.trashinfo").readText().trimEnd().lines(),
        )
        assertEquals("/dossier/café noir+1.txt", Trash.list(src()).single().originalPath)
    }

    @Test fun restoreRecreatesParent() {
        local("a/b/c.txt", "c")
        val s = src()
        Trash.moveToTrash(s, listOf("/a/b/c.txt"), t0)
        File(root, "a").deleteRecursively()
        Trash.restore(s, listOf("c.txt"))
        assertEquals("c", File(root, "a/b/c.txt").readText())
        assertTrue(Trash.list(s).isEmpty())
        assertFalse(File(root, ".Portail-corbeille/info/c.txt.trashinfo").exists())
    }

    @Test fun restoreUniqueName() {
        local("a.txt", "ancien")
        val s = src()
        Trash.moveToTrash(s, listOf("/a.txt"), t0)
        local("a.txt", "nouveau")
        Trash.restore(s, listOf("a.txt"))
        assertEquals("nouveau", File(root, "a.txt").readText())
        assertEquals("ancien", File(root, "a (1).txt").readText())
    }

    @Test fun sameNameTwiceKeepsBoth() {
        val s = src()
        local("a.txt", "1"); Trash.moveToTrash(s, listOf("/a.txt"), t0)
        local("a.txt", "2"); Trash.moveToTrash(s, listOf("/a.txt"), t0 + 1000)
        val items = Trash.list(s)
        assertEquals(listOf("a.txt", "a.txt"), items.map { it.name })
        assertEquals(listOf(t0 + 1000, t0), items.map { it.deletedAtMs }) // le plus récent d'abord
        assertEquals(2, items.map { it.id }.toSet().size)
        // restaurés tous les deux, sous un nom unique ; un nom de corbeille « a (1).txt » ne devient pas « a (2).txt »
        Trash.restore(s, items.map { it.id })
        assertEquals(setOf("1", "2"), setOf(File(root, "a.txt").readText(), File(root, "a (1).txt").readText()))
        assertFalse(File(root, "a (2).txt").exists())
    }

    @Test fun purgeExpiredAfter30Days() {
        val s = src()
        local("vieux.txt"); Trash.moveToTrash(s, listOf("/vieux.txt"), t0)
        local("recent.txt"); Trash.moveToTrash(s, listOf("/recent.txt"), t0 + 10 * day)
        Trash.purgeExpired(s, t0 + 31 * day)
        assertEquals(listOf("recent.txt"), Trash.list(s).map { it.name })
        assertFalse(File(root, ".Portail-corbeille/files/vieux.txt").exists())
        assertFalse(File(root, ".Portail-corbeille/info/vieux.txt.trashinfo").exists())
        // chaque suppression purge aussi
        local("autre.txt"); Trash.moveToTrash(s, listOf("/autre.txt"), t0 + 41 * day)
        assertEquals(listOf("autre.txt"), Trash.list(s).map { it.name })
    }

    @Test fun purgeAndEmpty() {
        val s = src()
        local("a.txt"); local("b.txt"); local("c.txt")
        Trash.moveToTrash(s, listOf("/a.txt", "/b.txt", "/c.txt"), t0)
        Trash.purge(s, listOf("b.txt"))
        assertEquals(setOf("a.txt", "c.txt"), Trash.list(s).map { it.id }.toSet())
        assertFalse(File(root, ".Portail-corbeille/info/b.txt.trashinfo").exists())
        File(root, ".Portail-corbeille/info/orphelin.trashinfo").writeText("[Trash Info]\n")
        Trash.empty(s)
        assertTrue(Trash.list(s).isEmpty())
        assertEquals(0, File(root, ".Portail-corbeille/files").list()!!.size)
        assertEquals(0, File(root, ".Portail-corbeille/info").list()!!.size)
    }

    @Test fun deleteInsideTrashIsPermanent() {
        val s = src()
        local("a.txt"); Trash.moveToTrash(s, listOf("/a.txt"), t0)
        Trash.moveToTrash(s, listOf("/.Portail-corbeille/files/a.txt"), t0)
        assertFalse(File(root, ".Portail-corbeille/files/a.txt").exists())
        assertFalse(File(root, ".Portail-corbeille/info/a.txt.trashinfo").exists())
        assertTrue(Trash.list(s).isEmpty())
        // la corbeille elle-même : supprimée définitivement
        local("b.txt"); Trash.moveToTrash(s, listOf("/b.txt"), t0)
        Trash.moveToTrash(s, listOf("/.Portail-corbeille"), t0)
        assertFalse(File(root, ".Portail-corbeille").exists())
        assertTrue(Trash.list(s).isEmpty())
    }

    @Test fun readOnlySourceRefused() {
        local("a.txt")
        refused<FileNotFoundException> { Trash.moveToTrash(src(access = false), listOf("/a.txt"), t0) }
        assertTrue(File(root, "a.txt").exists())
        assertFalse(File(root, ".Portail-corbeille").exists())
        // stockage sans droit d'écriture (sauf pour root, qui écrit partout)
        root.setWritable(false)
        try {
            assumeTrue(!root.canWrite())
            val e = refused<FileNotFoundException> { Trash.moveToTrash(src(), listOf("/a.txt"), t0) }
            assertTrue(e.message, e.message!!.contains("lecture seule"))
            assertTrue(File(root, "a.txt").exists())
        } finally {
            root.setWritable(true)
        }
    }

    @Test fun malformedInfoRestoresToRoot() {
        val s = src()
        local("d/a.txt"); Trash.moveToTrash(s, listOf("/d/a.txt"), t0)
        File(root, ".Portail-corbeille/info/a.txt.trashinfo").writeText("[Trash Info]\nPath=../../dehors/a.txt\n")
        val item = Trash.list(s).single()
        assertEquals("/a.txt", item.originalPath); assertEquals(0L, item.deletedAtMs)
        Trash.restore(s, listOf("a.txt"))
        assertTrue(File(root, "a.txt").exists())
        assertFalse(File(tmp.root.parentFile, "dehors").exists())
    }

    @Test fun trashHiddenFromSearch() {
        local("zut.txt"); local("garde/zut2.txt")
        val s = src()
        Files.setResolverForTests { if (it == "phone") s else null }
        try {
            Trash.moveToTrash(s, listOf("/zut.txt"), t0)
            val r = Search.run("phone", "/", "zut", showHidden = true)
            assertEquals(listOf("/garde/zut2.txt"), r.entries.map { it.path })
        } finally {
            Files.setResolverForTests(null)
        }
    }
}

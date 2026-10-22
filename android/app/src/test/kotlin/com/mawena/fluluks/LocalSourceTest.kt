package com.mawena.fluluks

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.ProtectedDirException
import com.mawena.fluluks.files.Sources
import java.io.File
import java.io.FileNotFoundException
import java.nio.file.Files as NioFiles
import com.mawena.fluluks.files.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Source locale (stockage du téléphone, carte SD) sur un dossier temporaire. */
class LocalSourceTest {
    @get:Rule val tmp = TemporaryFolder()
    @get:Rule val dehors = TemporaryFolder()

    private val root: File get() = tmp.root
    private fun src(access: Boolean = true) = LocalSource("phone", "phone", "Téléphone", root) { access }

    private inline fun <reified T : Throwable> refused(block: () -> Unit): T {
        try { block() } catch (t: Throwable) { if (t is T) return t; throw t }
        throw AssertionError("accepté")
    }

    @Test fun listsEntriesWithTypes() {
        File(root, "b.txt").writeText("bb")
        File(root, "A").mkdir()
        File(root, "a.txt").writeText("a")
        NioFiles.createSymbolicLink(File(root, "lien").toPath(), File(root, "a.txt").toPath())
        val l = src().list("/").associateBy { it.name }
        assertEquals(setOf("b.txt", "A", "a.txt", "lien"), l.keys)
        assertEquals(FileType.DIR, l["A"]!!.type)
        assertEquals(FileType.FILE, l["b.txt"]!!.type)
        assertEquals(2L, l["b.txt"]!!.size)
        assertEquals("/b.txt", l["b.txt"]!!.path)
        assertEquals(FileType.SYMLINK, l["lien"]!!.type)
        assertTrue(l["b.txt"]!!.mtime > 0)
    }

    @Test fun canonicalEscapeRefused() {
        NioFiles.createSymbolicLink(File(root, "dehors").toPath(), File("/").toPath())
        refused<FileNotFoundException> { src().list("/dehors") }
        refused<FileNotFoundException> { src().stat("/../x") }
        refused<FileNotFoundException> { src().list("/a//b") }
        refused<FileNotFoundException> { src().mkdir("/dehors", "x") }
    }

    @Test fun protectedDirReported() {
        val d = File(root, "prive").apply { mkdir() }
        File(d, "x").writeText("x")
        assumeTrue(d.setReadable(false) && !d.canRead()) // root lit tout : test sans objet
        try {
            refused<ProtectedDirException> { src().list("/prive") }
        } finally {
            d.setReadable(true)
        }
    }

    @Test fun noAccessReported() {
        val e = refused<FileNotFoundException> { src(access = false).list("/") }
        assertEquals("Accès aux fichiers du téléphone non autorisé", e.message)
    }

    @Test fun mkdirRenameDeleteRecursive() {
        val s = src()
        assertEquals("/P", s.mkdir("/", "P"))
        assertEquals("/P (1)", s.mkdir("/", "P"))
        File(root, "P/sous").mkdirs()
        File(root, "P/sous/f.txt").writeText("f")
        assertEquals("/Q", s.rename("/P", "Q"))
        assertEquals("Un élément porte déjà ce nom", refused<FileNotFoundException> { s.rename("/Q", "P (1)") }.message)
        refused<FileNotFoundException> { s.rename("/Q", "a/b") }
        s.delete("/Q")
        assertFalse(File(root, "Q").exists())
        refused<FileNotFoundException> { s.delete("/") }
    }

    @Test fun createFileAndReadBack() {
        val s = src()
        val data = ByteArray(3 shl 20) { (it * 31).toByte() }
        val (path, out) = s.createFile("/", "a.bin")
        out.use { it.write(data) }
        assertEquals("/a.bin", path)
        assertEquals("/a (1).bin", s.createFile("/", "a.bin").also { it.second.close() }.first)
        assertArrayEquals(data, s.openRead("/a.bin").use { it.readBytes() })
        assertEquals(data.size.toLong(), s.stat("/a.bin")!!.size)
        val (free, total) = s.space()
        assertTrue(free in 1..total)
        assertEquals(null, s.readOnlyReason)
    }

    /** Relecture finale : Android 11 et plus renvoie souvent une liste vide (et non null) pour ces dossiers. */
    @Test fun androidDataAndObbReportedProtected() {
        File(root, "Android/data/com.exemple").mkdirs()
        File(root, "Android/obb").mkdirs()
        File(root, "Android/media").mkdirs()
        for (p in listOf("/Android/data", "/Android/obb", "/android/DATA", "/Android/data/com.exemple")) {
            refused<ProtectedDirException> { src().list(p) }
        }
        assertEquals(setOf("data", "obb", "media"), src().list("/Android").map { it.name }.toSet())
        assertEquals(0, src().list("/Android/media").size)
    }

    /** Relecture finale : les erreurs de java.io (« /storage/…: open failed: EACCES ») ne sortent jamais. */
    @Test fun ioFailuresNeverLeakPaths() {
        val f = File(root, "secret.txt").apply { writeText("x") }
        val d = File(root, "fermé").apply { mkdir() }
        assumeTrue(f.setReadable(false) && !f.canRead() && d.setWritable(false) && !d.canWrite()) // pas en root
        try {
            for (block in listOf({ src().openRead("/secret.txt").close() }, { src().createFile("/fermé", "a.txt").second.close() })) {
                val m = Files.toFilesException(refused<Exception>(block), "copyDocument").message!!
                assertFalse(m, m.contains(root.path) || m.contains("EACCES") || m.contains("/"))
            }
        } finally {
            f.setReadable(true); d.setWritable(true)
        }
    }

    /** Relecture de la phase 6 : un lien qui pointe hors de la racine se renomme et se supprime (lui, jamais sa cible). */
    @Test fun deleteAndRenameLinkPointingOutside() {
        val cible = File(dehors.root, "cible.txt").apply { writeText("garde") }
        NioFiles.createSymbolicLink(File(root, "lien").toPath(), cible.toPath())
        assertEquals("/lien2", src().rename("/lien", "lien2"))
        assertEquals(FileType.SYMLINK, src().stat("/lien2")!!.type)
        src().delete("/lien2")
        assertFalse(NioFiles.exists(File(root, "lien2").toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS))
        assertEquals("garde", cible.readText())
        refused<FileNotFoundException> { src().openRead("/lien2") }
    }

    /** Relecture de la phase 6 : sans accès, la raison affichée n'est pas « lecture seule ». */
    @Test fun noAccessReason() {
        assertEquals(Files.MSG_NO_ACCESS, src(access = false).readOnlyReason)
        assertEquals(null, src().readOnlyReason)
    }

    /** Relecture de la phase 6 : Android ne dit pas si un stockage amovible est une carte ou une clé USB. */
    @Test fun removableKindForAndroidVolumes() {
        assertEquals("phone", Sources.kind(src()))
        assertEquals("removable", Sources.kind(LocalSource("sd:1234-ABCD", "sd:1234-ABCD", "Clé USB", root) { true }))
    }
}

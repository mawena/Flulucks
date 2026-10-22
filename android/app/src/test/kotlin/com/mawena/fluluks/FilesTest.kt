package com.mawena.fluluks

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Opérations du gestionnaire sur une source locale et une source disque (moteur en mémoire). */
class FilesTest {
    @get:Rule val tmp = TemporaryFolder()

    /** Comme FAT : « : » refusé dans un nom (FL_EINVAL) ; compte les appels qui touchent un chemin. */
    private class Engine : MemEngine() {
        var calls = 0
        override fun stat(vol: Long, path: ByteArray): ByteArray { calls++; return super.stat(vol, path) }
        override fun list(vol: Long, path: ByteArray): ByteArray { calls++; return super.list(vol, path) }
        override fun rename(vol: Long, from: ByteArray, to: ByteArray): Int =
            if (String(to, Charsets.UTF_8).substringAfterLast('/').contains(':')) EngineError.FL_EINVAL
            else super.rename(vol, from, to)
    }

    private lateinit var e: Engine
    private lateinit var delayer: FakeDelayer
    private lateinit var volId: String
    private lateinit var local: LocalSource

    @Before fun setup() {
        e = Engine()
        delayer = FakeDelayer()
        VolumeManager.resetForTests(e, delayer = delayer, clock = { delayer.now })
        val disk = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(disk, 1, true) }.id
        local = LocalSource("phone", "phone", "Téléphone", tmp.root) { true }
        val sources = mapOf<String, FileSource>("phone" to local, volId to DiskSource(volId, e))
        Files.setResolverForTests { sources[it] }
    }

    @After fun teardown() = Files.setResolverForTests(null)

    private inline fun refused(block: () -> Unit): FilesException {
        try { block() } catch (x: FilesException) { return x }
        throw AssertionError("accepté")
    }

    private fun fill(id: String) {
        Files.createFolder(id, "/", "A")
        Files.createFolder(id, "/", "c")
        val s = Files.source(id)
        for (n in listOf("b.txt", "a.txt", ".cache")) s.createFile("/", n).second.use { it.write(1) }
    }

    @Test fun listSortsDirsFirstThenNameCaseInsensitive() {
        for (id in listOf("phone", volId)) {
            fill(id)
            assertEquals(id, listOf("A", "c", "a.txt", "b.txt"), Files.list(id, "/", false).map { it.name })
        }
    }

    @Test fun hiddenFilesFiltered() {
        for (id in listOf("phone", volId)) {
            fill(id)
            assertFalse(Files.list(id, "/", false).any { it.name == ".cache" })
            assertTrue(Files.list(id, "/", true).any { it.name == ".cache" })
        }
    }

    @Test fun hostilePathRejectedBeforeEngine() {
        for (bad in listOf("/a/../b", "a", "/a/", "//a", "/a\u0000b", "/./a")) {
            e.calls = 0
            val x = refused { Files.list(volId, bad, false) }
            assertEquals(bad, "Document introuvable ou illisible", x.message)
            assertEquals(bad, 0, e.calls)
            refused { Files.list("phone", bad, false) }
            refused { Files.createFolder(volId, bad, "x") }
            refused { Files.delete(volId, listOf(bad)) }
        }
        assertFalse(File(tmp.root.parentFile, "b").exists())
    }

    @Test fun createFolderUnique() {
        for (id in listOf("phone", volId)) {
            assertEquals("/Photos", Files.createFolder(id, "/", "Photos"))
            assertEquals("/Photos (1)", Files.createFolder(id, "/", "Photos"))
        }
    }

    @Test fun renameInvalidOnDisk() {
        Files.createFolder(volId, "/", "D")
        assertEquals("Nom non autorisé sur ce disque", refused { Files.rename(volId, "/D", "a:b") }.message)
        assertEquals("/E", Files.rename(volId, "/D", "E"))
        Files.createFolder("phone", "/", "x")
        assertEquals("Nom invalide", refused { Files.rename("phone", "/x", "a/b") }.message)
    }

    @Test fun deleteManyNotesWriteOnce() {
        fill(volId)
        Files.createFolder(volId, "/A", "sous")
        val before = VolumeManager.writeGeneration(volId)
        Files.delete(volId, listOf("/A", "/a.txt", "/b.txt"))
        assertEquals(before + 1, VolumeManager.writeGeneration(volId))
        assertEquals(listOf("c"), Files.list(volId, "/", false).map { it.name })
        fill("phone")
        Files.delete("phone", listOf("/A", "/a.txt"))
        assertEquals(listOf("c", "b.txt"), Files.list("phone", "/", false).map { it.name })
    }

    @Test fun closedSourceReported() {
        assertEquals("Le disque a été fermé", refused { Files.list("inconnu", "/", false) }.message)
        VolumeManager.eject(volId)
        assertEquals("Volume fermé", refused { Files.list(volId, "/", false) }.message)
    }

    /** Récupéré de feature/portail-save : messages français, jamais le détail d'une exception inattendue. */
    @Test fun errorMessagesAreFrenchAndNeverLeakDetails() {
        assertEquals("Disque plein", Files.toFilesException(EngineError(EngineError.FL_ENOSPC), "createDocument").message)
        assertEquals("Nom non autorisé sur ce disque",
            Files.toFilesException(EngineError(EngineError.FL_EINVAL), "renameDocument").message)
        assertEquals("Dossier introuvable",
            Files.toFilesException(java.io.FileNotFoundException("Dossier introuvable"), "queryDocument").message)
        for (x in listOf(IllegalStateException("/storage/emulated/0/secret.jpg"), java.io.IOException("/secret"),
            RuntimeException("secret"))) {
            val m = Files.toFilesException(x, "queryDocument").message!!
            assertEquals(Files.MSG_GENERIC, m)
            assertFalse(m.contains("secret"))
        }
    }

    /** Relecture finale : un FileNotFoundException de java.io porte le chemin absolu en anglais. */
    @Test fun javaIoMessagesReplaced() {
        val m = Files.toFilesException(
            java.io.FileNotFoundException("/storage/emulated/0/DCIM/x.jpg: open failed: EACCES (Permission denied)"),
            "openDocument").message!!
        assertEquals(Files.MSG_BAD_PATH, m)
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.Search
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Recherche par nom sur une source locale et sur un disque (moteur en mémoire). */
class SearchTest {
    @get:Rule val tmp = TemporaryFolder()

    /** Le listage peut déclencher une annulation (recherche en cours). */
    private class Engine : MemEngine() {
        var onList: () -> Unit = {}
        var failPath: String? = null
        override fun list(vol: Long, path: ByteArray): ByteArray {
            onList()
            if (String(path, Charsets.UTF_8) == failPath) return """{"err":-5}""".toByteArray() // dossier illisible
            return super.list(vol, path)
        }
    }

    private lateinit var e: Engine
    private lateinit var volId: String
    private val root: File get() = tmp.root

    @Before fun setup() {
        e = Engine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        val disk = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        volId = runBlocking { VolumeManager.mountPlain(disk, 1, true) }.id
        val sources = mapOf<String, FileSource>(
            "phone" to LocalSource("phone", "phone", "Téléphone", root) { true },
            volId to DiskSource(volId, e),
        )
        Files.setResolverForTests { sources[it] }
    }

    @After fun teardown() = Files.setResolverForTests(null)

    private fun mk(id: String, path: String, dir: Boolean = false) {
        val s = Files.source(id)
        val parent = path.substringBeforeLast('/').ifEmpty { "/" }
        var cur = "/"
        for (part in parent.split('/').filter { it.isNotEmpty() }) {
            val next = if (cur == "/") "/$part" else "$cur/$part"
            if (s.stat(next) == null) s.mkdir(cur, part)
            cur = next
        }
        val name = path.substringAfterLast('/')
        if (dir) s.mkdir(parent, name) else s.createFile(parent, name).second.close()
    }

    @Test fun findsAccentAndCaseInsensitive() {
        for (id in listOf("phone", volId)) {
            mk(id, "/Photos/2026/Été.jpg"); mk(id, "/ETE-plage.png"); mk(id, "/hiver.jpg"); mk(id, "/.été-caché")
            val r = Search.run(id, "/", "ete", false)
            assertEquals(id, listOf("ETE-plage.png", "Été.jpg"), r.entries.map { it.name })
            assertEquals("/Photos/2026/Été.jpg", r.entries[1].path)
            assertFalse(r.truncated)
            assertEquals(3, Search.run(id, "/", "ÉTÉ", true).entries.size)
            assertEquals(listOf("Été.jpg"), Search.run(id, "/Photos", "été", false).entries.map { it.name })
        }
    }

    @Test fun truncatesAt500() {
        for (i in 0 until 501) File(root, "d${i / 100}").apply { mkdir() }.let { File(it, "f$i.txt").createNewFile() }
        val r = Search.run("phone", "/", "f", false)
        assertEquals(500, r.entries.size)
        assertTrue(r.truncated)
    }

    @Test fun skipsProtectedDirs() {
        mk("phone", "/A/cible.txt"); mk("phone", "/prive/cible.txt")
        val d = File(root, "prive")
        assumeTrue(d.setReadable(false) && !d.canRead()) // root lit tout : test sans objet
        try {
            assertEquals(listOf("/A/cible.txt"), Search.run("phone", "/", "cible", false).entries.map { it.path })
        } finally {
            d.setReadable(true)
        }
    }

    @Test fun cancelReturnsPartial() {
        for (i in 0 until 5) mk(volId, "/d$i/cible$i.txt")
        var lists = 0
        e.onList = { if (++lists == 3) Search.cancel() }
        val r = Search.run(volId, "/", "cible", false)
        assertTrue(r.entries.size in 1..4)
        assertFalse(r.truncated)
        e.onList = {}
        assertEquals(5, Search.run(volId, "/", "cible", false).entries.size) // la suivante repart de zéro
    }

    @Test fun emptyQueryReturnsNothing() {
        mk("phone", "/a.txt")
        for (q in listOf("", "   ")) assertEquals(0, Search.run("phone", "/", q, true).entries.size)
    }

    @Test fun depthBounded() {
        var p = ""
        for (i in 0 until 70) p += "/n$i"
        File(root, p).mkdirs()
        File(root, "$p/profond.txt").createNewFile()
        File(root, "n0/profond-haut.txt").createNewFile()
        assertEquals(listOf("profond-haut.txt"), Search.run("phone", "/", "profond", false).entries.map { it.name })
    }

    /** Relecture de la phase 6 : un dossier illisible d'un disque est sauté, la recherche continue. */
    @Test fun searchSkipsUnreadableDiskFolder() {
        mk(volId, "/A/cible.txt"); mk(volId, "/B/cible.txt"); mk(volId, "/C/cible.txt")
        e.failPath = "/B"
        assertEquals(listOf("/A/cible.txt", "/C/cible.txt"), Search.run(volId, "/", "cible", false).entries.map { it.path }.sorted())
    }
}

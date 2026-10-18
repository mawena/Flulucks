package com.mawena.fluluks

import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.OpenFlags
import com.mawena.fluluks.provider.WriteOps
import java.io.FileNotFoundException
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/** WriteOps sur un système de fichiers en mémoire (mêmes codes d'erreur que le moteur). */
class WriteOpsTest {
    /** Arbre en mémoire : chemin → contenu (null = dossier). */
    private class MemEngine : Engine {
        val nodes = sortedMapOf<String, ByteArray?>("/" to null)
        val files = HashMap<Long, String>()
        var next = 1L
        var ops = 0
        /** Disque hostile : liste les enfants d'un chemin (nom, type) sans rapport avec `nodes`. */
        var hostile: ((String) -> List<Pair<String, String>>)? = null

        private fun s(b: ByteArray) = String(b, Charsets.UTF_8)
        private fun parent(p: String) = p.substring(0, p.lastIndexOf('/')).ifEmpty { "/" }
        private fun children(p: String) = nodes.keys.filter { it != "/" && it != p && parent(it) == p }
        private fun err(c: Int) = """{"err":$c}""".toByteArray()
        private fun entry(p: String) = JSONObject()
            .put("name", p.substring(p.lastIndexOf('/') + 1))
            .put("type", if (nodes[p] == null) "d" else "f")
            .put("size", nodes[p]?.size ?: 0).put("mtime", 0)

        override fun version() = "mem"
        override fun strerror(code: Int) = ByteArray(0)
        override fun openImage(path: ByteArray, readOnly: Boolean) = 0L
        override fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int) = 0L
        override fun closeDisk(disk: Long) = 0
        override fun scan(disk: Long) = ByteArray(0)
        override fun mount(disk: Long, partIndex: Int, pass: ByteArray?, writable: Boolean) = 0L
        override fun unmount(vol: Long, force: Boolean) = 0
        override fun statfs(vol: Long) = ByteArray(0)
        override fun sync(vol: Long) = 0
        override fun truncate(file: Long, size: Long) = 0

        override fun stat(vol: Long, path: ByteArray): ByteArray {
            val p = s(path)
            return if (!nodes.containsKey(p)) err(EngineError.FL_ENOENT) else entry(p).toString().toByteArray()
        }

        override fun list(vol: Long, path: ByteArray): ByteArray {
            val p = s(path)
            hostile?.let { h ->
                val a = JSONArray()
                h(p).forEach { (n, t) -> a.put(JSONObject().put("name", n).put("type", t).put("size", 0).put("mtime", 0)) }
                return a.toString().toByteArray()
            }
            if (!nodes.containsKey(p)) return err(EngineError.FL_ENOENT)
            if (nodes[p] != null) return err(EngineError.FL_ENOTDIR)
            val a = JSONArray()
            children(p).forEach { a.put(entry(it)) }
            return a.toString().toByteArray()
        }

        override fun openFile(vol: Long, path: ByteArray, flags: Int): Long {
            val p = s(path)
            if (!nodes.containsKey(p)) {
                if (flags and OpenFlags.CREAT == 0) return EngineError.FL_ENOENT.toLong()
                if (!nodes.containsKey(parent(p))) return EngineError.FL_ENOENT.toLong()
                nodes[p] = ByteArray(0); ops++
            } else if (nodes[p] == null) {
                return EngineError.FL_EISDIR.toLong()
            } else if (flags and OpenFlags.TRUNC != 0) {
                nodes[p] = ByteArray(0)
            }
            files[next] = p
            return next++
        }

        override fun pread(file: Long, offset: Long, buf: ByteArray, len: Int): Int {
            val d = nodes[files[file] ?: return EngineError.FL_EINVAL] ?: return EngineError.FL_EISDIR
            if (offset >= d.size) return 0
            val n = minOf(len.toLong(), d.size - offset).toInt()
            System.arraycopy(d, offset.toInt(), buf, 0, n)
            return n
        }

        override fun pwrite(file: Long, offset: Long, buf: ByteArray, len: Int): Int {
            val p = files[file] ?: return EngineError.FL_EINVAL
            val d = nodes[p]!!
            val out = d.copyOf(maxOf(d.size, (offset + len).toInt()))
            System.arraycopy(buf, 0, out, offset.toInt(), len)
            nodes[p] = out
            return len
        }

        override fun closeFile(file: Long) = if (files.remove(file) != null) 0 else EngineError.FL_EINVAL

        override fun mkdir(vol: Long, path: ByteArray): Int {
            val p = s(path)
            if (nodes.containsKey(p)) return EngineError.FL_EEXIST
            if (!nodes.containsKey(parent(p))) return EngineError.FL_ENOENT
            nodes[p] = null; ops++
            return 0
        }

        override fun unlink(vol: Long, path: ByteArray): Int {
            val p = s(path)
            if (!nodes.containsKey(p)) return EngineError.FL_ENOENT
            if (nodes[p] == null) return EngineError.FL_EISDIR
            nodes.remove(p); ops++
            return 0
        }

        override fun rmdir(vol: Long, path: ByteArray): Int {
            val p = s(path)
            if (!nodes.containsKey(p)) return EngineError.FL_ENOENT
            if (children(p).isNotEmpty()) return EngineError.FL_ENOTEMPTY
            nodes.remove(p); ops++
            return 0
        }

        override fun rename(vol: Long, from: ByteArray, to: ByteArray): Int {
            val f = s(from); val t = s(to)
            if (!nodes.containsKey(f)) return EngineError.FL_ENOENT
            if (nodes.containsKey(t)) return EngineError.FL_EEXIST
            if (t.startsWith("$f/")) return EngineError.FL_EINVAL
            val moved = nodes.keys.filter { it == f || it.startsWith("$f/") }
            for (k in moved) nodes[t + k.substring(f.length)] = nodes.remove(k)
            ops++
            return 0
        }
    }

    private val e = MemEngine()
    private val w = WriteOps(e, 1)

    private fun file(p: String, data: ByteArray = ByteArray(0)) { e.nodes[p] = data }
    private fun dir(p: String) { e.nodes[p] = null }

    private inline fun refused(block: () -> Unit) {
        try { block(); fail("accepté") } catch (_: FileNotFoundException) { }
    }

    @Test fun createUniqueNames() {
        assertEquals("/a.txt", w.create("/", "a.txt", false))
        assertEquals("/a (1).txt", w.create("/", "a.txt", false))
        assertEquals("/d", w.create("/", "d", true))
        assertEquals("/d (1)", w.create("/", "d", true))
        assertEquals("/d/a.txt", w.create("/d", "a.txt", false))
        assertTrue(e.nodes["/d (1)"] == null && e.nodes.containsKey("/d (1)"))
    }

    @Test fun createRefusals() {
        for (bad in listOf(null, "", ".", "..", "a/b", "a\u0000", "x".repeat(256))) refused { w.create("/", bad, false) }
        file("/f")
        refused { w.create("/f", "x", false) } // parent qui n'est pas un dossier
        refused { w.create("/absent", "x", false) }
        file("/z")
        for (n in 1..999) file("/z ($n)")
        refused { w.create("/", "z", false) } // 999 essais épuisés
    }

    @Test fun deleteIsRecursiveAndIterative() {
        var p = "/haut"
        dir(p)
        repeat(200) { p = "$p/n"; dir(p); file("$p/f") } // profond : pas de récursion de pile
        w.delete("/haut")
        assertEquals(setOf("/"), e.nodes.keys)
        refused { w.delete("/") }
        refused { w.delete("/absent") }
    }

    @Test fun renameRules() {
        file("/a"); file("/b"); dir("/d"); file("/d/x")
        assertEquals("/c", w.rename("/a", "c"))
        refused { w.rename("/c", "b") }
        refused { w.rename("/c", "a/b") }
        refused { w.rename("/", "r") }
        assertEquals("/e", w.rename("/d", "e"))
        assertTrue(e.nodes.containsKey("/e/x"))
        assertEquals("/b", w.rename("/b", "b")) // même nom : rien à faire
    }

    @Test fun moveRules() {
        dir("/s"); file("/s/f", byteArrayOf(1)); dir("/t"); file("/t/f")
        assertEquals("/t/f (1)", w.move("/s/f", "/t"))
        assertArrayEquals(byteArrayOf(1), e.nodes["/t/f (1)"])
        refused { w.move("/s", "/s") }
        dir("/s/sub")
        refused { w.move("/s", "/s/sub") }
        assertEquals("/t/s", w.move("/s", "/t"))
        assertTrue(e.nodes.containsKey("/t/s/sub"))
        val before = e.ops
        assertEquals("/t/s", w.move("/t/s", "/t")) // même dossier : inchangé
        assertEquals(before, e.ops)
    }

    @Test fun copyFileAndTree() {
        val big = ByteArray((WriteOps.COPY_CHUNK * 2) + 17) { (it * 7).toByte() }
        dir("/s"); file("/s/gros", big); dir("/s/d"); file("/s/d/p", "p".toByteArray()); dir("/s/d/vide")
        assertEquals("/s/gros (1)", w.copy("/s/gros", "/s"))
        assertArrayEquals(big, e.nodes["/s/gros (1)"])
        dir("/t")
        assertEquals("/t/s", w.copy("/s", "/t"))
        assertArrayEquals(big, e.nodes["/t/s/gros"])
        assertArrayEquals("p".toByteArray(), e.nodes["/t/s/d/p"])
        assertTrue(e.nodes.containsKey("/t/s/d/vide"))
        assertFalse(e.files.isNotEmpty()) // tout fichier ouvert a été refermé
        refused { w.copy("/s", "/s") }
        refused { w.copy("/s", "/s/d") }
        refused { w.copy("/", "/t") }
    }

    private fun tooDeep(block: () -> Unit) {
        try { block(); fail("accepté") } catch (ex: FileNotFoundException) {
            assertEquals(WriteOps.TOO_DEEP, ex.message)
        }
    }

    @Test fun hostileLoopRefusedWithoutWriting() {
        dir("/b"); dir("/t")
        e.hostile = { listOf("d" to "d") } // chaque dossier contient un dossier : boucle sans fin
        val before = e.ops
        tooDeep { w.delete("/b") }
        tooDeep { w.copy("/b", "/t") }
        assertEquals(before, e.ops) // rien de supprimé, rien de créé
        assertEquals(setOf("/", "/b", "/t"), e.nodes.keys)
    }

    @Test fun hostileWideTreeRefused() {
        dir("/b"); dir("/t")
        val small = WriteOps(e, 1, maxEntries = 100)
        e.hostile = { p -> if (p == "/b") (0 until 101).map { "n$it" to "f" } else emptyList() }
        tooDeep { small.delete("/b") }
        tooDeep { small.copy("/b", "/t") }
        assertEquals(0, e.ops)
        e.hostile = { p -> if (p == "/b") (0 until 99).map { "n$it" to "f" } else emptyList() }
        // 100 éléments, bord inclus : la borne laisse passer (l'échec vient ensuite de l'arbre factice)
        try { small.delete("/b") } catch (_: EngineError) { }
    }

    @Test fun depthLimitIsExact() {
        // un nœud de profondeur k sous /b a k + 1 « / » : jusqu'à la profondeur MAX_DEPTH exactement
        e.hostile = { p -> if (p.count { it == '/' } <= WriteOps.MAX_DEPTH) listOf("d" to "d") else emptyList() }
        dir("/b")
        // profondeur MAX_DEPTH atteinte : acceptée par le parcours (l'arbre factice échoue ensuite, pas la borne)
        try { w.delete("/b") } catch (ex: Exception) {
            assertFalse(ex.message, ex.message == WriteOps.TOO_DEEP)
        }
        // un niveau de plus : refusé par la borne, avant toute écriture
        e.hostile = { p -> if (p.count { it == '/' } <= WriteOps.MAX_DEPTH + 1) listOf("d" to "d") else emptyList() }
        val before = e.ops
        try { w.delete("/b"); fail("profondeur MAX_DEPTH + 1 acceptée") } catch (ex: FileNotFoundException) {
            assertEquals(WriteOps.TOO_DEEP, ex.message)
        }
        assertEquals(before, e.ops)
    }

    @Test fun copyWithSymlinkSaysSo() {
        dir("/s"); file("/s/a"); dir("/t")
        e.hostile = { p -> if (p == "/s") listOf("a" to "f", "lien" to "l", "sock" to "o") else emptyList() }
        try { w.copy("/s", "/t"); fail("copie incomplète silencieuse") } catch (ex: FileNotFoundException) {
            assertTrue(ex.message, ex.message!!.contains("2 lien(s) symbolique(s) ou fichier(s) spécial(aux)"))
        }
        assertFalse(e.nodes.containsKey("/t/s")) // rien d'écrit
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.OpenFlags
import org.json.JSONArray
import org.json.JSONObject

/**
 * Système de fichiers en mémoire, avec les codes d'erreur du moteur (tests JVM de WriteOps, des sources
 * de fichiers et des copies). Un disque à une partition ext4 se monte par VolumeManager : scan, mount et
 * statfs rendent des réponses valides ; [readOnly], [roReason] et [free] règlent le statfs.
 */
/** Arbre en mémoire : chemin → contenu (null = dossier). */
open class MemEngine : Engine {
    val nodes = sortedMapOf<String, ByteArray?>("/" to null)
    val files = HashMap<Long, String>()
    var next = 1L
    var ops = 0
    /** Disque hostile : liste les enfants d'un chemin (nom, type) sans rapport avec `nodes`. */
    var hostile: ((String) -> List<Pair<String, String>>)? = null
    /** Comme FAT : noms insensibles à la casse (le nom garde la casse donnée à la création ou au renommage). */
    var caseInsensitive = false

    /** Clé existante désignant p (à la casse près si caseInsensitive), ou p. */
    private fun key(p: String) = if (caseInsensitive) nodes.keys.firstOrNull { it.equals(p, ignoreCase = true) } ?: p else p

    private fun s(b: ByteArray) = String(b, Charsets.UTF_8)
    private fun parent(p: String) = p.substring(0, p.lastIndexOf('/')).ifEmpty { "/" }
    private fun children(p: String) = nodes.keys.filter { it != "/" && it != p && parent(it) == p }
    private fun err(c: Int) = """{"err":$c}""".toByteArray()
    private fun entry(p: String) = JSONObject()
        .put("name", p.substring(p.lastIndexOf('/') + 1))
        .put("type", if (nodes[p] == null) "d" else "f")
        .put("size", nodes[p]?.size ?: 0).put("mtime", 0)
        .also { o -> owner?.let { (mode, uid, gid) -> o.put("mode", mode).put("uid", uid).put("gid", gid) } }

    /** (mode, uid, gid) rendus par stat et list ; null : clés absentes. */
    var owner: Triple<Int, Long, Long>? = null
    /** Système de fichiers rendu par statfs. */
    var fs = "ext4"

    override fun version() = "mem"
    override fun strerror(code: Int) = ByteArray(0)
    var readOnly = false
    var roReason = ""
    var free = 1L shl 30
    var total = 2L shl 30
    var partUuid = "uuid-mem"
    /** Code rendu par le n-ième pwrite à venir (1 = le prochain) ; 0 : pas de panne. */
    var failPwriteAt = 0
    var failPwriteCode = EngineError.FL_EIO
    /** Nombre de FL_EBUSY rendus par unlink/rename sur ce chemin avant d'aboutir (fichier encore ouvert). */
    val busy = mutableMapOf<String, Int>()
    private fun busyNow(p: String): Boolean {
        val n = busy[p] ?: return false
        if (n <= 0) return false
        busy[p] = n - 1
        return true
    }

    override fun openImage(path: ByteArray, readOnly: Boolean) = 1L
    override fun openUsb(fd: Int, iface: Int, epIn: Int, epOut: Int) = 0L
    override fun closeDisk(disk: Long) = 0
    override fun scan(disk: Long) = """{"sector_size":512,"size":$total,"table":"gpt","parts":[
        {"index":1,"first_lba":2048,"size":$total,"content":"ext4","label":"Mem","uuid":"$partUuid","supported":true}]}""".toByteArray()
    override fun mount(disk: Long, partIndex: Int, pass: ByteArray?, writable: Boolean) = 7L
    override fun unmount(vol: Long, force: Boolean) = 0
    override fun statfs(vol: Long) =
        """{"total":$total,"free":$free,"label":"Mem","read_only":$readOnly,"ro_reason":"$roReason","needs_recovery":false,"fs":"$fs"}""".toByteArray()
    override fun sync(vol: Long) = 0
    override fun truncate(file: Long, size: Long) = 0

    override fun stat(vol: Long, path: ByteArray): ByteArray {
        val p = key(s(path))
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
        if (failPwriteAt > 0 && --failPwriteAt == 0) return failPwriteCode
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
        if (busyNow(p)) return EngineError.FL_EBUSY
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
        val f = key(s(from)); val t = s(to)
        if (!nodes.containsKey(f)) return EngineError.FL_ENOENT
        if (nodes.containsKey(key(t)) && key(t) != f) return EngineError.FL_EEXIST
        if (t.startsWith("$f/")) return EngineError.FL_EINVAL
        if (busyNow(f)) return EngineError.FL_EBUSY
        val moved = nodes.keys.filter { it == f || it.startsWith("$f/") }
        for (k in moved) nodes[t + k.substring(f.length)] = nodes.remove(k)
        ops++
        return 0
    }
}

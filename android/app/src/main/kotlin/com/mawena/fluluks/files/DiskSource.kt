package com.mawena.fluluks.files

import android.net.Uri
import android.provider.DocumentsContract
import com.mawena.fluluks.engine.DirEntry
import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.engine.NativeEngine
import com.mawena.fluluks.engine.OpenFlags
import com.mawena.fluluks.engine.VolumeStat
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.ProviderErrors
import com.mawena.fluluks.provider.WriteOps
import com.mawena.fluluks.volumes.VolumeManager
import java.io.FileNotFoundException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

/** Erreur du moteur pendant la lecture ou l'écriture d'un flux de disque (message français). */
class DiskIOException(val code: Int, op: String) :
    IOException(ProviderErrors.message(code, op) ?: ProviderErrors.READ_ONLY)

/**
 * Volume ouvert d'un disque USB. Toute écriture passe par [WriteOps] sous [VolumeManager.writeLock], puis
 * [VolumeManager.noteWrite] (synchronisation différée) et [VolumeManager.notifyChanged] ; chaque appel
 * compte comme une activité du volume (verrouillage automatique).
 */
class DiskSource(override val id: String, private val engine: Engine = NativeEngine) : FileSource {
    private fun b(path: String) = path.toByteArray(Charsets.UTF_8)

    private fun state() = VolumeManager.volume(id) ?: throw FileNotFoundException(ProviderErrors.VOLUME_CLOSED)

    /** Poignée native du volume ; l'appel compte comme une activité. */
    private fun handle(): Long {
        val h = VolumeManager.nativeVolume(id)
        if (h <= 0) throw FileNotFoundException(ProviderErrors.VOLUME_CLOSED)
        VolumeManager.noteActivity(id)
        return h
    }

    /** Activité d'un flux ; un volume fermé entre-temps arrête la lecture ou l'écriture. */
    private fun touch() {
        if (VolumeManager.volume(id) == null) throw DiskIOException(EngineError.FL_ENODEV, "openDocument")
        VolumeManager.noteActivity(id)
    }

    override val stableKey: String
        get() = state().partUuid.let { if (it.isBlank()) "disk:${state().diskId}#${state().partIndex}" else "disk:$it" }
    override val label: String get() = state().title

    /** Mode lu au moment de l'appel : le moteur peut passer en lecture seule après une erreur. */
    override val readOnlyReason: String?
        get() {
            val st = VolumeStat.parse(engine.statfs(handle()))
            return if (st.readOnly) st.roReason else null
        }

    /** Les noms uniques passent par stat, que le moteur résout selon le système de fichiers. */
    override val caseInsensitive = false

    /** Assainir vers ext4 ne coûte rien et évite un échec sur FAT et NTFS. */
    override val restrictedNames = true

    private fun requireWritable() {
        readOnlyReason?.let { throw FileNotFoundException(ProviderErrors.readOnly(it)) }
    }

    /** Écriture structurelle sous le verrou partagé, puis synchronisation programmée et vues prévenues. */
    private inline fun <T> write(vararg parents: String, block: (WriteOps) -> T): T {
        val h = handle()
        try {
            return synchronized(VolumeManager.writeLock) {
                requireWritable()
                block(WriteOps(engine, h))
            }
        } finally {
            VolumeManager.noteWrite(id) // même partielle, l'écriture a pu changer le disque
            VolumeManager.notifyChanged(id, *parents)
        }
    }

    override fun list(path: String): List<Entry> {
        val p = PathRules.require(path)
        return DirEntry.parseList(engine.list(handle(), b(p))).map {
            Entry(it.name, DocumentIds.child(p, it.name), it.type, it.size, it.mtime)
        }
    }

    override fun stat(path: String): Entry? {
        val p = PathRules.require(path)
        val st = WriteOps(engine, handle()).statOrNull(p) ?: return null
        return Entry(if (p == "/") "" else WriteOps.nameOf(p), p, st.type, st.size, st.mtime)
    }

    override fun space(): Pair<Long, Long> {
        val st = VolumeStat.parse(engine.statfs(handle()))
        return st.freeBytes to st.totalBytes
    }

    override fun mkdir(parent: String, name: String): String {
        val p = PathRules.require(parent)
        return write(p) { it.create(p, name, dir = true) }
    }

    override fun rename(path: String, newName: String): String {
        val p = PathRules.require(path)
        return write(WriteOps.parentOf(p)) { it.rename(p, newName) }
    }

    override fun delete(path: String) {
        val p = PathRules.require(path)
        write(WriteOps.parentOf(p)) { it.delete(p) }
    }

    override fun deleteAll(paths: List<String>) {
        val ps = paths.map(PathRules::require)
        write(*ps.map(WriteOps::parentOf).distinct().toTypedArray()) { w -> ps.forEach(w::delete) }
    }

    override fun move(path: String, targetParent: String): String {
        val p = PathRules.require(path)
        val t = PathRules.require(targetParent)
        if (t == p || DocumentIds.isDescendant(p, t)) throw FileNotFoundException(Files.MSG_INTO_ITSELF)
        return write(WriteOps.parentOf(p), t) { it.move(p, t) }
    }

    override fun openRead(path: String): InputStream {
        val h = handle()
        val f = EngineError.check(engine.openFile(h, b(PathRules.require(path)), OpenFlags.RDONLY))
        return EngineInputStream(engine, f) { touch() }
    }

    override fun createFile(parent: String, name: String): Pair<String, OutputStream> {
        val p = PathRules.require(parent)
        val h = handle()
        val (path, f) = write(p) { w ->
            val created = w.create(p, name, dir = false)
            created to EngineError.check(engine.openFile(h, b(created), OpenFlags.RDWR or OpenFlags.TRUNC))
        }
        return path to EngineOutputStream(engine, f, onChunk = ::touch) {
            VolumeManager.noteWrite(id)
            VolumeManager.notifyChanged(id, p)
        }
    }

    override fun openOverwrite(path: String): OutputStream {
        val p = PathRules.require(path)
        val h = handle()
        val parent = WriteOps.parentOf(p)
        val f = write(parent) { EngineError.check(engine.openFile(h, b(p), OpenFlags.RDWR or OpenFlags.TRUNC)) }
        return EngineOutputStream(engine, f, onChunk = ::touch) {
            VolumeManager.noteWrite(id)
            VolumeManager.notifyChanged(id, parent)
        }
    }

    override fun details(path: String): Details? {
        val p = PathRules.require(path)
        val h = handle()
        val st = WriteOps(engine, h).statOrNull(p) ?: return null
        val fs = VolumeStat.parse(engine.statfs(h)).fs
        val ext4 = fs == "ext4"
        return Details(
            if (p == "/") "" else WriteOps.nameOf(p), p, st.type, st.size, st.mtime,
            st.mode.takeIf { ext4 }, st.uid.takeIf { ext4 }, st.gid.takeIf { ext4 }, fs,
        )
    }

    override fun openNative(path: String): NativeIn {
        val p = PathRules.require(path)
        val h = handle()
        val st = WriteOps(engine, h).statOrNull(p) ?: throw FileNotFoundException("Document introuvable")
        if (st.type != FileType.FILE) throw FileNotFoundException("Ce n'est pas un fichier ordinaire")
        val f = EngineError.check(engine.openFile(h, b(p), OpenFlags.RDONLY))
        return NativeIn.EngineFile(f, st.size) { touch() }
    }

    override fun createNative(parent: String, name: String): Pair<String, NativeOut> {
        val p = PathRules.require(parent)
        val h = handle()
        val (path, f) = write(p) { w ->
            val created = w.create(p, name, dir = false)
            created to EngineError.check(engine.openFile(h, b(created), OpenFlags.RDWR or OpenFlags.TRUNC))
        }
        return path to NativeOut.EngineFile(f) {
            VolumeManager.noteWrite(id)
            VolumeManager.notifyChanged(id, p)
        }
    }

    override fun contentUri(path: String): Uri =
        DocumentsContract.buildDocumentUri(VolumeManager.AUTHORITY, DocumentIds.encode(id, PathRules.require(path)))
}

/** Lecture séquentielle d'un fichier du moteur ; [onRead] à chaque lecture (activité du volume). */
internal class EngineInputStream(
    private val engine: Engine, private val file: Long, private val onRead: () -> Unit,
) : InputStream() {
    private var pos = 0L
    private var closed = false
    private var tmp: ByteArray? = null

    override fun read(): Int {
        val one = ByteArray(1)
        return if (read(one, 0, 1) <= 0) -1 else one[0].toInt() and 0xFF
    }

    override fun read(b: ByteArray, off: Int, len: Int): Int {
        if (closed) throw IOException("flux fermé")
        if (len == 0) return 0
        onRead()
        val n = minOf(len, WriteOps.COPY_CHUNK)
        val buf = if (off == 0) b else (tmp?.takeIf { it.size >= n } ?: ByteArray(n).also { tmp = it })
        val r = engine.pread(file, pos, buf, n)
        if (r < 0) throw DiskIOException(r, "openDocument")
        if (r == 0) return -1
        if (buf !== b) System.arraycopy(buf, 0, b, off, r)
        pos += r
        return r
    }

    override fun close() {
        if (closed) return
        closed = true
        engine.closeFile(file)
    }
}

/**
 * Écriture séquentielle d'un fichier du moteur. [close] ferme le fichier (le moteur synchronise) puis
 * appelle [onClosed] ; une erreur de fermeture est rendue (données peut-être non écrites).
 */
internal class EngineOutputStream(
    private val engine: Engine, private val file: Long, private val onChunk: () -> Unit, private val onClosed: () -> Unit,
) : OutputStream() {
    private var pos = 0L
    private var closed = false

    override fun write(b: Int) = write(byteArrayOf(b.toByte()), 0, 1)

    override fun write(b: ByteArray, off: Int, len: Int) {
        if (closed) throw IOException("flux fermé")
        onChunk()
        var done = 0
        while (done < len) {
            val n = minOf(len - done, WriteOps.COPY_CHUNK)
            val buf = if (off + done == 0 && n == b.size) b else b.copyOfRange(off + done, off + done + n)
            val w = engine.pwrite(file, pos, buf, n)
            if (w < 0) throw DiskIOException(w, "copyDocument")
            pos += w
            done += w
        }
    }

    override fun close() {
        if (closed) return
        closed = true
        val rc = engine.closeFile(file)
        onClosed()
        if (rc < 0) throw DiskIOException(rc, "copyDocument")
    }
}

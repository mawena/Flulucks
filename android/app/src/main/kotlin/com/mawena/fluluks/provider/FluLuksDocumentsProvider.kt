package com.mawena.fluluks.provider

import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.CancellationSignal
import android.os.Handler
import android.os.HandlerThread
import android.os.ParcelFileDescriptor
import android.os.ProxyFileDescriptorCallback
import android.os.storage.StorageManager
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import android.provider.DocumentsContract.Root
import android.provider.DocumentsProvider
import android.system.ErrnoException
import android.system.OsConstants
import android.util.Log
import com.mawena.fluluks.R
import com.mawena.fluluks.engine.DirEntry
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.engine.FsStat
import com.mawena.fluluks.engine.NativeEngine
import com.mawena.fluluks.engine.OpenFlags
import com.mawena.fluluks.engine.VolumeStat
import com.mawena.fluluks.volumes.VolumeManager
import com.mawena.fluluks.volumes.VolumeState
import java.io.FileNotFoundException
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Expose chaque volume ouvert comme une racine, en écriture si le volume est monté en écriture. Tout
 * argument vient d'une autre application (hostile) ; seules FileNotFoundException et SecurityException
 * sortent des méthodes (plus IllegalArgumentException pour un mode inconnu et
 * UnsupportedOperationException pour un déplacement ou une copie entre volumes, comme le veut le contrat).
 */
class FluLuksDocumentsProvider : DocumentsProvider() {

    override fun onCreate(): Boolean {
        // Léger : n'enregistre qu'un récepteur ; le cycle de vie natif reste sur l'exécuteur du gestionnaire.
        VolumeManager.init(context!!)
        return true
    }

    // ------------------------------------------------------------------ racines

    override fun queryRoots(projection: Array<out String>?): Cursor {
        val cols = rootColumns(projection)
        val cursor = MatrixCursor(cols)
        val disks = VolumeManager.disks.value
        for (v in VolumeManager.volumes.value) {
            val part = disks.firstOrNull { it.id == v.diskId }?.scan?.parts?.firstOrNull { it.index == v.partIndex }
            val mode = liveMode(v, VolumeManager.nativeVolume(v.id))
            val flags = Root.FLAG_LOCAL_ONLY or Root.FLAG_SUPPORTS_IS_CHILD or
                (if (mode.readOnly) 0 else Root.FLAG_SUPPORTS_CREATE)
            val row = cursor.newRow()
            for (c in cols) {
                when (c) {
                    Root.COLUMN_ROOT_ID -> row.add(c, v.id)
                    Root.COLUMN_FLAGS -> row.add(c, flags)
                    Root.COLUMN_ICON -> row.add(c, R.drawable.ic_disk)
                    Root.COLUMN_TITLE -> row.add(c, v.title)
                    Root.COLUMN_SUMMARY -> row.add(c, RootSummary.of(part, mode.readOnly, mode.reason))
                    Root.COLUMN_DOCUMENT_ID -> row.add(c, DocumentIds.encode(v.id, "/"))
                    Root.COLUMN_AVAILABLE_BYTES -> row.add(c, v.freeBytes)
                    Root.COLUMN_CAPACITY_BYTES -> row.add(c, v.totalBytes)
                }
            }
        }
        return cursor
    }

    // ------------------------------------------------------------------ documents

    override fun queryDocument(documentId: String?, projection: Array<out String>?): Cursor = guard("queryDocument") {
        val (vol, path) = resolve(documentId)
        val st = FsStat.parse(NativeEngine.stat(vol.handle, path.toByteArray(Charsets.UTF_8)))
        val cols = docColumns(projection)
        val writable = !liveMode(vol.state, vol.handle).readOnly
        MatrixCursor(cols).also {
            addRow(it, cols, documentId!!, if (path == "/") vol.state.title else st.name, st.type, st.size, st.mtime,
                docFlags(st.type, path == "/", writable))
            observe(it, vol.state.id)
        }
    }

    override fun queryChildDocuments(
        parentDocumentId: String?, projection: Array<out String>?, sortOrder: String?,
    ): Cursor = guard("queryChildDocuments") {
        val (vol, path) = resolve(parentDocumentId)
        val entries = DirEntry.parseList(NativeEngine.list(vol.handle, path.toByteArray(Charsets.UTF_8)))
        val cols = docColumns(projection)
        val cursor = MatrixCursor(cols, entries.size)
        observe(cursor, vol.state.id)
        val writable = !liveMode(vol.state, vol.handle).readOnly
        var skipped = 0
        for (e in entries) {
            // un nom du disque qui ne forme pas un identifiant valide n'est pas adressable : on l'omet
            if (e.name.isEmpty() || e.name == "." || e.name == ".." || e.name.indexOf('/') >= 0 || e.name.indexOf('\u0000') >= 0) continue
            // TODO : identifiants en octets bruts pour les noms non UTF-8 (phase ultérieure). En phase 2 ils
            // arrivent avec U+FFFD et deux noms distincts pourraient s'aliaser : on les omet.
            if (e.name.indexOf('\uFFFD') >= 0) { skipped++; continue }
            val id = DocumentIds.encode(vol.state.id, DocumentIds.child(path, e.name))
            if (DocumentIds.decode(id) == null) continue
            addRow(cursor, cols, id, e.name, e.type, e.size, e.mtime, docFlags(e.type, false, writable))
        }
        if (skipped > 0) Log.i(TAG, "$skipped nom(s) non UTF-8 omis")
        cursor
    }

    /** Tous les curseurs d'un volume observent l'URI des enfants de sa racine : l'éjection les rafraîchit. */
    private fun observe(c: MatrixCursor, volumeId: String) {
        c.setNotificationUri(
            context!!.contentResolver,
            DocumentsContract.buildChildDocumentsUri(VolumeManager.AUTHORITY, DocumentIds.encode(volumeId, "/")),
        )
    }

    override fun isChildDocument(parentDocumentId: String?, documentId: String?): Boolean {
        val p = parentDocumentId?.let { DocumentIds.decode(it) } ?: return false
        val d = documentId?.let { DocumentIds.decode(it) } ?: return false
        return p.first == d.first && DocumentIds.isDescendant(p.second, d.second)
    }

    override fun openDocument(
        documentId: String?, mode: String?, signal: CancellationSignal?,
    ): ParcelFileDescriptor {
        val m = OpenMode.parse(mode) // IllegalArgumentException pour un mode inconnu, avant guard
        return guard("openDocument", documentId) {
            val (vol, path) = resolve(documentId)
            val bytes = path.toByteArray(Charsets.UTF_8)
            val st = FsStat.parse(NativeEngine.stat(vol.handle, bytes))
            if (st.type != FileType.FILE) throw FileNotFoundException("Ce n'est pas un fichier ordinaire")
            if (m.write) requireWritable(vol)
            val flags = if (!m.write) OpenFlags.RDONLY else OpenFlags.RDWR or (if (m.truncate) OpenFlags.TRUNC else 0)
            val file = EngineError.check(NativeEngine.openFile(vol.handle, bytes, flags))
            if (m.truncate) VolumeManager.noteWrite(vol.state.id) // la troncature est une écriture
            try {
                val size = if (m.truncate) 0L else st.size
                val target = if (!m.write) null else WriteTarget(vol.state.id, vol.handle, if (m.append) size else 0L) {
                    VolumeManager.notifyChanged(vol.state.id, WriteOps.parentOf(path))
                }
                val sm = context!!.getSystemService(StorageManager::class.java)
                sm.openProxyFileDescriptor(m.pfdMode, FileCallback(vol.state.id, file, size, target), ioHandler)
            } catch (t: Throwable) {
                NativeEngine.closeFile(file) // le descripteur n'a pas été créé : le rappel ne sera jamais libéré
                throw t
            }
        }
    }

    // ------------------------------------------------------------------ écriture

    override fun createDocument(parentDocumentId: String?, mimeType: String?, displayName: String?): String =
        guard("createDocument", parentDocumentId) {
            val (vol, parent) = resolve(parentDocumentId)
            requireWritable(vol)
            val path = synchronized(VolumeManager.writeLock) {
                WriteOps(NativeEngine, vol.handle).create(parent, displayName, mimeType == Document.MIME_TYPE_DIR)
            }
            written(vol, parent)
            DocumentIds.encode(vol.state.id, path)
        }

    override fun deleteDocument(documentId: String?) = guard("deleteDocument", documentId) {
        val (vol, path) = resolve(documentId)
        requireWritable(vol)
        try {
            synchronized(VolumeManager.writeLock) { WriteOps(NativeEngine, vol.handle).delete(path) }
        } finally {
            written(vol, WriteOps.parentOf(path)) // même partiel, le dossier a changé
        }
    }

    override fun renameDocument(documentId: String?, displayName: String?): String = guard("renameDocument", documentId) {
        val (vol, path) = resolve(documentId)
        requireWritable(vol)
        val target = synchronized(VolumeManager.writeLock) { WriteOps(NativeEngine, vol.handle).rename(path, displayName) }
        written(vol, WriteOps.parentOf(path))
        DocumentIds.encode(vol.state.id, target)
    }

    override fun moveDocument(
        sourceDocumentId: String?, sourceParentDocumentId: String?, targetParentDocumentId: String?,
    ): String {
        sameVolume(sourceDocumentId, targetParentDocumentId)
        return guard("moveDocument", sourceDocumentId) {
            val (vol, path) = resolve(sourceDocumentId)
            val (_, target) = resolve(targetParentDocumentId)
            requireWritable(vol)
            val moved = synchronized(VolumeManager.writeLock) { WriteOps(NativeEngine, vol.handle).move(path, target) }
            written(vol, WriteOps.parentOf(path), target)
            DocumentIds.encode(vol.state.id, moved)
        }
    }

    override fun copyDocument(sourceDocumentId: String?, targetParentDocumentId: String?): String {
        sameVolume(sourceDocumentId, targetParentDocumentId)
        return guard("copyDocument", sourceDocumentId) {
            val (vol, path) = resolve(sourceDocumentId)
            val (_, target) = resolve(targetParentDocumentId)
            requireWritable(vol)
            try {
                val copy = synchronized(VolumeManager.writeLock) { WriteOps(NativeEngine, vol.handle).copy(path, target) }
                DocumentIds.encode(vol.state.id, copy)
            } finally {
                written(vol, target)
            }
        }
    }

    /** Déplacement ou copie entre deux volumes : non pris en charge (le client se rabat sur une copie). */
    private fun sameVolume(a: String?, b: String?) {
        val va = a?.let { DocumentIds.decode(it) }?.first
        val vb = b?.let { DocumentIds.decode(it) }?.first
        if (va != null && vb != null && va != vb) throw UnsupportedOperationException("Volumes différents")
    }

    /** Après une modification : synchronisation programmée et vues des dossiers concernés rafraîchies. */
    private fun written(vol: Resolved, vararg parents: String) {
        VolumeManager.noteWrite(vol.state.id)
        VolumeManager.notifyChanged(vol.state.id, *parents)
    }


    /** Mode d'un volume lu au moment de l'appel (le moteur peut passer en lecture seule après une erreur). */
    private class Mode(val readOnly: Boolean, val reason: String)

    private fun liveMode(state: VolumeState, handle: Long): Mode = try {
        val st = VolumeStat.parse(NativeEngine.statfs(handle))
        Mode(st.readOnly, st.roReason)
    } catch (e: Exception) {
        Mode(state.readOnly, state.roReason)
    }

    private fun requireWritable(vol: Resolved) {
        val m = liveMode(vol.state, vol.handle)
        if (m.readOnly) throw FileNotFoundException(readOnlyText(m.reason))
    }

    private fun readOnlyText(reason: String) = ProviderErrors.readOnly(reason)

    /** « Volume en lecture seule : <raison> » pour un FL_EROFS du moteur (raison relue via statfs). */
    private fun readOnlyMessage(documentId: String?): String {
        val volId = documentId?.let { DocumentIds.decode(it) }?.first ?: return READ_ONLY
        val state = VolumeManager.volume(volId) ?: return READ_ONLY
        val handle = VolumeManager.nativeVolume(volId)
        if (handle <= 0) return READ_ONLY
        return readOnlyText(liveMode(state, handle).reason)
    }

    private fun docFlags(type: FileType, isRoot: Boolean, writable: Boolean): Int {
        if (!writable) return 0
        if (isRoot) return Document.FLAG_DIR_SUPPORTS_CREATE
        val common = Document.FLAG_SUPPORTS_DELETE or Document.FLAG_SUPPORTS_RENAME or
            Document.FLAG_SUPPORTS_MOVE or Document.FLAG_SUPPORTS_COPY
        return when (type) {
            FileType.DIR -> common or Document.FLAG_DIR_SUPPORTS_CREATE
            FileType.FILE -> common or Document.FLAG_SUPPORTS_WRITE
            else -> Document.FLAG_SUPPORTS_DELETE or Document.FLAG_SUPPORTS_RENAME or Document.FLAG_SUPPORTS_MOVE
        }
    }

    // ------------------------------------------------------------------ utilitaires

    private class Resolved(val state: VolumeState, val handle: Long)

    private fun resolve(documentId: String?): Pair<Resolved, String> {
        val (volId, path) = documentId?.let { DocumentIds.decode(it) } ?: throw FileNotFoundException("Identifiant invalide")
        val state = VolumeManager.volume(volId)
        val handle = VolumeManager.nativeVolume(volId)
        if (state == null || handle <= 0) throw FileNotFoundException(VOLUME_CLOSED)
        VolumeManager.noteActivity(volId)
        return Resolved(state, handle) to path
    }

    private fun addRow(
        c: MatrixCursor, cols: Array<String>, id: String, name: String, type: FileType, size: Long, mtime: Long, flags: Int,
    ) {
        val row = c.newRow()
        for (col in cols) {
            when (col) {
                Document.COLUMN_DOCUMENT_ID -> row.add(col, id)
                Document.COLUMN_DISPLAY_NAME -> row.add(col, name)
                Document.COLUMN_MIME_TYPE -> row.add(col, when (type) {
                    FileType.DIR -> Document.MIME_TYPE_DIR
                    FileType.FILE -> MimeTypes.forName(name)
                    else -> MimeTypes.DEFAULT // lien ou autre : non ouvrable, ne doit pas ressembler à un média
                })
                Document.COLUMN_SIZE -> row.add(col, size)
                Document.COLUMN_LAST_MODIFIED -> row.add(col, mtime * 1000)
                Document.COLUMN_FLAGS -> row.add(col, flags)
            }
        }
    }

    /** Seules FileNotFoundException et SecurityException sortent ; le reste devient un message français. */
    private inline fun <T> guard(op: String, documentId: String? = null, block: () -> T): T = try {
        block()
    } catch (e: FileNotFoundException) {
        throw e
    } catch (e: SecurityException) {
        throw e
    } catch (e: EngineError) {
        Log.w(TAG, "$op : code ${e.code}")
        throw FileNotFoundException(ProviderErrors.message(e.code, op) ?: readOnlyMessage(documentId))
    } catch (e: Exception) {
        Log.w(TAG, "$op : ${e.javaClass.simpleName}")
        throw FileNotFoundException("Erreur interne du fournisseur")
    } catch (e: OutOfMemoryError) { // dossier énorme : l'erreur va au client, le processus survit
        Log.w(TAG, "$op : mémoire insuffisante")
        throw FileNotFoundException(OUT_OF_MEMORY)
    }

    /**
     * Écriture d'un fichier ouvert en « w », « wa », « rw »… : volume, et décalage ajouté à chaque position
     * en mode ajout (« wa ») puisque le descripteur du client commence à 0. [onClosed] : fichier écrit fermé.
     */
    private class WriteTarget(val volumeId: String, val volume: Long, val appendBase: Long, val onClosed: () -> Unit)

    /** Rappel d'un fichier ; exécuté sur le fil partagé "fluluks-io", donc sérialisé. */
    private class FileCallback(
        private val volumeId: String, private val file: Long, private var size: Long, private val write: WriteTarget?,
    ) : ProxyFileDescriptorCallback() {
        private val released = AtomicBoolean(false)
        private var bytesRead = 0L
        private var bytesWritten = 0L
        private var readNanos = 0L
        private var chunk: ByteArray? = null
        private var warned = false
        private val openedNanos = System.nanoTime()
        private val base = write?.appendBase ?: 0L
        private val chunkSize = if (write != null) CHUNK else maxOf(1L, minOf(size, CHUNK.toLong())).toInt()

        override fun onGetSize(): Long = size - base

        override fun onRead(offset: Long, size: Int, data: ByteArray): Int {
            if (offset < 0 || size < 0 || size > data.size) throw ErrnoException("pread", OsConstants.EINVAL)
            VolumeManager.noteActivity(volumeId) // une lecture continue (vidéo) garde le volume actif
            if (size == 0) return 0
            val t0 = System.nanoTime()
            var done = 0
            try {
                while (done < size) {
                    val want = minOf(size - done, chunkSize)
                    val buf = chunk ?: ByteArray(chunkSize).also { chunk = it }
                    val n = NativeEngine.pread(file, base + offset + done, buf, want)
                    if (n < 0) {
                        warnOnce("pread", n)
                        throw ErrnoException("pread", if (n == EngineError.FL_ENODEV) OsConstants.ENXIO else OsConstants.EIO)
                    }
                    if (n == 0) break // fin de fichier
                    System.arraycopy(buf, 0, data, done, n)
                    done += n
                    if (n < want) break // lecture courte : fin de fichier
                }
            } finally {
                readNanos += System.nanoTime() - t0
            }
            bytesRead += done
            return done
        }

        override fun onWrite(offset: Long, size: Int, data: ByteArray): Int {
            val w = write ?: throw ErrnoException("pwrite", OsConstants.EBADF)
            if (offset < 0 || size < 0 || size > data.size) throw ErrnoException("pwrite", OsConstants.EINVAL)
            VolumeManager.noteActivity(volumeId)
            if (size == 0) return 0
            var done = 0
            while (done < size) {
                val len = minOf(size - done, MAX_WRITE)
                val buf = if (done == 0) data else data.copyOfRange(done, done + len)
                val n = NativeEngine.pwrite(file, base + offset + done, buf, len)
                if (n < 0) {
                    warnOnce("pwrite", n)
                    VolumeManager.refresh(w.volumeId) // le volume a pu passer en lecture seule
                    throw ErrnoException("pwrite", errno(n))
                }
                done += n
            }
            this.size = maxOf(this.size, base + offset + done)
            bytesWritten += done
            VolumeManager.noteWrite(w.volumeId)
            return done
        }

        override fun onFsync() {
            val w = write ?: return
            val gen = VolumeManager.writeGeneration(w.volumeId)
            val rc = NativeEngine.sync(w.volume)
            if (rc >= 0) VolumeManager.markSynced(w.volumeId, gen)
            if (rc < 0) {
                warnOnce("sync", rc)
                VolumeManager.refresh(w.volumeId)
                throw ErrnoException("fsync", errno(rc))
            }
        }

        override fun onRelease() {
            if (!released.compareAndSet(false, true)) return
            chunk = null
            val gen = write?.let { VolumeManager.writeGeneration(it.volumeId) } ?: 0L
            val rc = NativeEngine.closeFile(file) // FL_EINVAL si le volume a été éjecté entre-temps
            if (write != null) {
                // la fermeture d'un fichier écrit par ce descripteur synchronise le volume
                if (rc >= 0 && bytesWritten > 0) VolumeManager.markSynced(write.volumeId, gen)
                if (rc < 0 && rc != EngineError.FL_EINVAL) {
                    Log.w(TAG, "closeFile : code $rc")
                    VolumeManager.refresh(write.volumeId)
                }
                write.onClosed()
            }
            val readMs = maxOf(1L, readNanos / 1_000_000)
            val totalMs = maxOf(1L, (System.nanoTime() - openedNanos) / 1_000_000)
            Log.i("FluLuksPerf", "$bytesRead o lus en $readMs ms, $bytesWritten o écrits / $totalMs ms (total)")
        }

        private fun warnOnce(op: String, code: Int) {
            if (!warned) { warned = true; Log.w(TAG, "$op : code $code") } // une fois par fichier
        }

        private fun errno(code: Int) = errnoFor(code)
    }

    /** Modes de [openDocument], ceux de ParcelFileDescriptor.parseMode. */
    internal class OpenMode(val write: Boolean, val truncate: Boolean, val append: Boolean, val pfdMode: Int) {
        companion object {
            fun parse(mode: String?): OpenMode = when (mode) {
                "r" -> OpenMode(false, false, false, ParcelFileDescriptor.MODE_READ_ONLY)
                "w", "wt" -> OpenMode(true, true, false, ParcelFileDescriptor.MODE_WRITE_ONLY)
                "wa" -> OpenMode(true, false, true, ParcelFileDescriptor.MODE_WRITE_ONLY)
                "rw" -> OpenMode(true, false, false, ParcelFileDescriptor.MODE_READ_WRITE)
                "rwt" -> OpenMode(true, true, false, ParcelFileDescriptor.MODE_READ_WRITE)
                else -> throw IllegalArgumentException("Mode inconnu : $mode")
            }
        }
    }

    companion object {
        private const val TAG = "FluLuksProvider"
        private const val VOLUME_CLOSED = ProviderErrors.VOLUME_CLOSED
        private const val OUT_OF_MEMORY = "Mémoire insuffisante"
        private const val CHUNK = 128 * 1024
        private const val MAX_WRITE = 4 shl 20 // FJ_PREAD_MAX
        private const val READ_ONLY = "Volume en lecture seule"

        /** Code du moteur → errno rendu au client par onWrite / onFsync. */
        internal fun errnoFor(code: Int): Int = when (ProviderErrors.errno(code)) {
            ProviderErrors.Errno.ENOSPC -> OsConstants.ENOSPC
            ProviderErrors.Errno.EROFS -> OsConstants.EROFS
            ProviderErrors.Errno.ENXIO -> OsConstants.ENXIO
            ProviderErrors.Errno.EFBIG -> OsConstants.EFBIG
            ProviderErrors.Errno.EIO -> OsConstants.EIO
        }

        private val ioHandler: Handler by lazy { Handler(HandlerThread("fluluks-io").also { it.start() }.looper) }

        private val DOC_COLUMNS = arrayOf(
            Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
            Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS,
        )

        private val ROOT_COLUMNS: Array<String> = buildList {
            addAll(listOf(
                Root.COLUMN_ROOT_ID, Root.COLUMN_FLAGS, Root.COLUMN_ICON, Root.COLUMN_TITLE,
                Root.COLUMN_SUMMARY, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_AVAILABLE_BYTES,
            ))
            add(Root.COLUMN_CAPACITY_BYTES) // depuis l'API 24
        }.toTypedArray()

        private fun docColumns(projection: Array<out String>?) = pick(projection, DOC_COLUMNS)
        private fun rootColumns(projection: Array<out String>?) = pick(projection, ROOT_COLUMNS)

        /** Schéma standard : la projection demandée telle quelle (colonnes inconnues = null), sinon tout. */
        private fun pick(projection: Array<out String>?, supported: Array<String>): Array<String> =
            if (projection == null || projection.isEmpty()) supported else Array(projection.size) { projection[it] }
    }
}

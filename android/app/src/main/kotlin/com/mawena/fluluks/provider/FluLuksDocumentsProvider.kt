package com.mawena.fluluks.provider

import android.database.Cursor
import android.database.MatrixCursor
import android.os.Build
import android.os.CancellationSignal
import android.os.Handler
import android.os.HandlerThread
import android.os.ParcelFileDescriptor
import android.os.ProxyFileDescriptorCallback
import android.os.storage.StorageManager
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
import com.mawena.fluluks.volumes.VolumeManager
import com.mawena.fluluks.volumes.VolumeState
import java.io.FileNotFoundException
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Expose chaque volume ouvert comme une racine en lecture seule. Tout argument vient d'une autre
 * application (hostile) ; seules FileNotFoundException et SecurityException sortent des méthodes.
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
            val encrypted = disks.firstOrNull { it.id == v.diskId }
                ?.scan?.parts?.firstOrNull { it.index == v.partIndex }?.encrypted == true
            val row = cursor.newRow()
            for (c in cols) {
                when (c) {
                    Root.COLUMN_ROOT_ID -> row.add(c, v.id)
                    Root.COLUMN_FLAGS -> row.add(c, Root.FLAG_LOCAL_ONLY or Root.FLAG_SUPPORTS_IS_CHILD)
                    Root.COLUMN_ICON -> row.add(c, R.drawable.ic_disk)
                    Root.COLUMN_TITLE -> row.add(c, v.title)
                    Root.COLUMN_SUMMARY -> row.add(c, if (encrypted) "Chiffré (LUKS2) — lecture seule" else "Lecture seule")
                    Root.COLUMN_DOCUMENT_ID -> row.add(c, DocumentIds.encode(v.id, "/"))
                    Root.COLUMN_AVAILABLE_BYTES -> row.add(c, v.freeBytes)
                    CAPACITY_COLUMN -> row.add(c, v.totalBytes)
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
        MatrixCursor(cols).also { addRow(it, cols, documentId!!, if (path == "/") vol.state.title else st.name, st.type, st.size, st.mtime) }
    }

    override fun queryChildDocuments(
        parentDocumentId: String?, projection: Array<out String>?, sortOrder: String?,
    ): Cursor = guard("queryChildDocuments") {
        val (vol, path) = resolve(parentDocumentId)
        val entries = DirEntry.parseList(NativeEngine.list(vol.handle, path.toByteArray(Charsets.UTF_8)))
        val cols = docColumns(projection)
        val cursor = MatrixCursor(cols, entries.size)
        for (e in entries) {
            // un nom du disque qui ne forme pas un identifiant valide n'est pas adressable : on l'omet
            if (e.name.isEmpty() || e.name == "." || e.name == ".." || e.name.indexOf('/') >= 0 || e.name.indexOf('\u0000') >= 0) continue
            val id = DocumentIds.encode(vol.state.id, DocumentIds.child(path, e.name))
            if (DocumentIds.decode(id) == null) continue
            addRow(cursor, cols, id, e.name, e.type, e.size, e.mtime)
        }
        cursor
    }

    override fun isChildDocument(parentDocumentId: String?, documentId: String?): Boolean {
        val p = parentDocumentId?.let { DocumentIds.decode(it) } ?: return false
        val d = documentId?.let { DocumentIds.decode(it) } ?: return false
        return p.first == d.first && DocumentIds.isDescendant(p.second, d.second)
    }

    override fun openDocument(
        documentId: String?, mode: String?, signal: CancellationSignal?,
    ): ParcelFileDescriptor = guard("openDocument") {
        if (mode != "r") throw FileNotFoundException("Lecture seule pour l'instant")
        val (vol, path) = resolve(documentId)
        val bytes = path.toByteArray(Charsets.UTF_8)
        val st = FsStat.parse(NativeEngine.stat(vol.handle, bytes))
        if (st.type != FileType.FILE) throw FileNotFoundException("Ce n'est pas un fichier ordinaire")
        val file = EngineError.check(NativeEngine.openFile(vol.handle, bytes))
        try {
            val sm = context!!.getSystemService(StorageManager::class.java)
            sm.openProxyFileDescriptor(ParcelFileDescriptor.MODE_READ_ONLY, FileCallback(file, st.size), ioHandler)
        } catch (t: Throwable) {
            NativeEngine.closeFile(file) // le descripteur n'a pas été créé : le rappel ne sera jamais libéré
            throw t
        }
    }

    // ------------------------------------------------------------------ utilitaires

    private class Resolved(val state: VolumeState, val handle: Long)

    private fun resolve(documentId: String?): Pair<Resolved, String> {
        val (volId, path) = documentId?.let { DocumentIds.decode(it) } ?: throw FileNotFoundException("Identifiant invalide")
        val state = VolumeManager.volume(volId)
        val handle = VolumeManager.nativeVolume(volId)
        if (state == null || handle <= 0) throw FileNotFoundException(VOLUME_CLOSED)
        return Resolved(state, handle) to path
    }

    private fun addRow(c: MatrixCursor, cols: Array<String>, id: String, name: String, type: FileType, size: Long, mtime: Long) {
        val row = c.newRow()
        for (col in cols) {
            when (col) {
                Document.COLUMN_DOCUMENT_ID -> row.add(col, id)
                Document.COLUMN_DISPLAY_NAME -> row.add(col, name)
                Document.COLUMN_MIME_TYPE -> row.add(col, if (type == FileType.DIR) Document.MIME_TYPE_DIR else MimeTypes.forName(name))
                Document.COLUMN_SIZE -> row.add(col, size)
                Document.COLUMN_LAST_MODIFIED -> row.add(col, mtime * 1000)
                Document.COLUMN_FLAGS -> row.add(col, 0)
            }
        }
    }

    /** Seules FileNotFoundException et SecurityException sortent ; le reste devient un message français. */
    private inline fun <T> guard(op: String, block: () -> T): T = try {
        block()
    } catch (e: FileNotFoundException) {
        throw e
    } catch (e: SecurityException) {
        throw e
    } catch (e: EngineError) {
        Log.w(TAG, "$op : code ${e.code}")
        throw FileNotFoundException(if (e.code == EngineError.FL_ENODEV) VOLUME_CLOSED else "Document introuvable ou illisible")
    } catch (e: Exception) {
        Log.w(TAG, "$op : ${e.javaClass.simpleName}")
        throw FileNotFoundException("Erreur interne du fournisseur")
    }

    /** Rappel de lecture d'un fichier ; exécuté sur le fil partagé "fluluks-io", donc sérialisé. */
    private class FileCallback(private val file: Long, private val size: Long) : ProxyFileDescriptorCallback() {
        private val released = AtomicBoolean(false)
        private var bytesRead = 0L
        private var readNanos = 0L
        private var chunk: ByteArray? = null

        override fun onGetSize(): Long = size

        override fun onRead(offset: Long, size: Int, data: ByteArray): Int {
            if (offset < 0 || size < 0 || size > data.size) throw ErrnoException("pread", OsConstants.EINVAL)
            if (size == 0) return 0
            val t0 = System.nanoTime()
            var done = 0
            try {
                while (done < size) {
                    val want = minOf(size - done, CHUNK)
                    val buf = chunk ?: ByteArray(CHUNK).also { chunk = it }
                    val n = NativeEngine.pread(file, offset + done, buf, want)
                    if (n < 0) {
                        Log.w(TAG, "pread : code $n")
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

        override fun onRelease() {
            if (!released.compareAndSet(false, true)) return
            chunk = null
            NativeEngine.closeFile(file) // FL_EINVAL si le volume a été éjecté entre-temps : ignoré
            Log.i("FluLuksPerf", "$bytesRead o en ${readNanos / 1_000_000} ms")
        }
    }

    companion object {
        private const val TAG = "FluLuksProvider"
        private const val VOLUME_CLOSED = "Volume fermé"
        private const val CHUNK = 1 shl 20
        private const val CAPACITY_COLUMN = "capacity_bytes" // Root.COLUMN_CAPACITY_BYTES, API 28

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
            if (Build.VERSION.SDK_INT >= 28) add(CAPACITY_COLUMN)
        }.toTypedArray()

        private fun docColumns(projection: Array<out String>?) = pick(projection, DOC_COLUMNS)
        private fun rootColumns(projection: Array<out String>?) = pick(projection, ROOT_COLUMNS)

        /** Sous-ensemble demandé des colonnes gérées (hostile : inconnues et doublons ignorés) ; défaut = tout. */
        private fun pick(projection: Array<out String>?, supported: Array<String>): Array<String> {
            if (projection == null) return supported
            val chosen = projection.filter { it in supported }.distinct()
            return if (chosen.isEmpty()) supported else chosen.toTypedArray()
        }
    }
}

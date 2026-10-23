package com.mawena.fluluks.provider

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.Handler
import android.os.HandlerThread
import android.os.ParcelFileDescriptor
import android.os.ProxyFileDescriptorCallback
import android.os.storage.StorageManager
import android.provider.OpenableColumns
import android.system.ErrnoException
import android.system.OsConstants
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.SeekableReader
import java.io.FileNotFoundException
import java.security.SecureRandom

/** Fichier servi par le fournisseur de flux : source, chemin, nom et taille au moment où le lien a été fait. */
data class StreamFile(val sourceId: String, val path: String, val name: String, val size: Long)

/**
 * Jetons du fournisseur de flux : un jeton aléatoire de 128 bits par fichier servi, gardé en mémoire seulement (un
 * lien ne survit pas au processus) et oublié quand sa source se ferme.
 */
object StreamTokens {
    private val byToken = HashMap<String, StreamFile>()
    private val rnd = SecureRandom()

    /** Jeton de [path] de [sourceId] ; le même fichier (même taille) garde son jeton. */
    @Synchronized
    fun register(sourceId: String, path: String, size: Long): String {
        val f = StreamFile(sourceId, path, WriteOps.nameOf(path), size)
        byToken.entries.firstOrNull { it.value == f }?.let { return it.key }
        val b = ByteArray(16).also(rnd::nextBytes)
        val t = b.joinToString("") { "%02x".format(it) }
        byToken[t] = f
        return t
    }

    @Synchronized
    fun get(token: String): StreamFile? = byToken[token]

    @Synchronized
    fun forget(sourceId: String) {
        byToken.entries.removeAll { it.value.sourceId == sourceId }
    }

    @Synchronized
    fun clearForTests() = byToken.clear()
}

/**
 * Fournisseur de flux (phase 10), non exporté : sert en lecture un fichier de n'importe quelle source sans
 * descripteur propre (dossier distant) par un descripteur mandataire, lu à n'importe quel décalage par
 * [SeekableReader]. C'est l'URI de contenu des sources distantes : lecteurs Media3, « Ouvrir avec », « Partager »
 * (permission accordée par l'intention) ; le lecteur d'archives natif passe par [openProxy].
 */
class StreamProvider : ContentProvider() {
    override fun onCreate() = true

    private fun file(uri: Uri): StreamFile =
        uri.pathSegments.firstOrNull()?.let(StreamTokens::get) ?: throw FileNotFoundException("Lien expiré")

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        if (mode != "r") throw FileNotFoundException("Lecture seule")
        val f = file(uri)
        val src = Files.sourceOrNull(f.sourceId) ?: throw FileNotFoundException(Files.MSG_SOURCE_CLOSED)
        return openProxy(src, f.path, f.size)
    }

    override fun getType(uri: Uri): String? = StreamTokens.get(uri.pathSegments.firstOrNull() ?: "")?.let { MimeTypes.forName(it.name) }

    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, args: Array<out String>?, sort: String?): Cursor {
        val f = file(uri)
        val cols = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        return MatrixCursor(cols, 1).apply {
            addRow(cols.map { c -> when (c) { OpenableColumns.DISPLAY_NAME -> f.name; OpenableColumns.SIZE -> f.size; else -> null } })
        }
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, args: Array<out String>?) = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, args: Array<out String>?) = 0

    companion object {
        const val AUTHORITY = "com.mawena.fluluks.flux"

        private val handler: Handler by lazy { Handler(HandlerThread("portail-flux").also { it.start() }.looper) }

        /** URI de contenu de [path] de [src] (fichier ordinaire). */
        fun uriFor(src: FileSource, path: String): Uri {
            val e = src.stat(path) ?: throw FileNotFoundException("Document introuvable")
            val t = StreamTokens.register(src.id, e.path, e.size)
            return Uri.Builder().scheme("content").authority(AUTHORITY).appendPath(t).appendPath(e.name).build()
        }

        /** Descripteur mandataire en lecture sur [path] de [src] ([size] octets). */
        fun openProxy(src: FileSource, path: String, size: Long): ParcelFileDescriptor {
            val ctx = Files.context ?: throw FileNotFoundException(Files.MSG_SOURCE_CLOSED)
            val reader = SeekableReader(size) { off -> src.openReadAt(path, off) }
            val sm = ctx.getSystemService(StorageManager::class.java)
            return sm.openProxyFileDescriptor(ParcelFileDescriptor.MODE_READ_ONLY, object : ProxyFileDescriptorCallback() {
                override fun onGetSize() = size

                override fun onRead(offset: Long, size: Int, data: ByteArray): Int = try {
                    maxOf(0, reader.read(offset, data, size))
                } catch (e: Exception) {
                    reader.close()
                    throw ErrnoException("read", OsConstants.EIO)
                }

                override fun onRelease() = reader.close()
            }, handler)
        }
    }
}

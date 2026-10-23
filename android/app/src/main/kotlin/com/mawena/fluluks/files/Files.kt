package com.mawena.fluluks.files

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.MimeTypes
import com.mawena.fluluks.provider.ProviderErrors
import java.io.FileNotFoundException
import java.io.IOException

/** Échec d'une opération du gestionnaire de fichiers : `code` est un fl_err, le message est en français. */
class FilesException(val code: Int, message: String) : Exception(message)

/**
 * Opérations du gestionnaire de fichiers sur toutes les sources. Les chemins viennent de Dart : chaque
 * source les valide par [PathRules] avant tout appel au moteur. Toute erreur sort en [FilesException].
 */
object Files {
    const val FILE_AUTHORITY = "com.mawena.fluluks.fichiers"
    const val MSG_BAD_PATH = "Document introuvable ou illisible"
    const val MSG_PROTECTED = "Dossier protégé par Android"
    const val MSG_NO_ACCESS = "Accès aux fichiers du téléphone non autorisé"
    const val MSG_INTO_ITSELF = "Un dossier ne peut pas être copié dans lui-même"
    const val MSG_NO_VIEWER = "Aucune application ne sait ouvrir ce fichier"
    const val MSG_SHARE_DIR = "Un dossier ne peut pas être partagé"
    const val MSG_SOURCE_CLOSED = "Le disque a été fermé"
    const val MSG_GENERIC = "L'opération a échoué"
    const val OPEN_WITH = "Ouvrir avec…"
    const val SHARE = "Partager"

    /** Contexte de l'application (URI du FileProvider) ; posé par [Sources.init]. */
    @Volatile var context: Context? = null

    /** Résolution des sources ; remplacée dans les tests JVM. */
    @Volatile private var resolver: (String) -> FileSource? = Sources::get

    @VisibleForTesting
    fun setResolverForTests(r: ((String) -> FileSource?)?) {
        resolver = r ?: Sources::get
    }

    fun source(id: String): FileSource = resolver(id) ?: throw FilesException(EngineError.FL_ENODEV, MSG_SOURCE_CLOSED)

    fun sourceOrNull(id: String): FileSource? = resolver(id)

    /** « Volume en lecture seule : <raison> » pour un disque, la raison seule pour le téléphone. */
    fun readOnlyMessage(s: FileSource, reason: String): String =
        if (s is DiskSource) ProviderErrors.readOnly(reason) else reason

    /**
     * Exécute [block] ; les erreurs deviennent des [FilesException] françaises. [op] est l'opération du
     * provider dont le message d'erreur du moteur est repris (`renameDocument` → « Nom non autorisé… »).
     */
    inline fun <T> guard(op: String, block: () -> T): T = try {
        block()
    } catch (e: FilesException) {
        throw e
    } catch (e: Throwable) {
        throw toFilesException(e, op)
    }

    fun toFilesException(e: Throwable, op: String): FilesException = when (e) {
        is FilesException -> e
        is EngineError -> FilesException(e.code, ProviderErrors.message(e.code, op) ?: ProviderErrors.READ_ONLY)
        is DiskIOException -> FilesException(e.code, e.message ?: MSG_GENERIC)
        is ProtectedDirException -> FilesException(EngineError.FL_EIO, MSG_PROTECTED)
        // nos messages sont en français ; ceux de java.io commencent par le chemin absolu
        is FileNotFoundException -> FilesException(EngineError.FL_ENOENT, e.message?.takeUnless { it.startsWith("/") } ?: MSG_BAD_PATH)
        is SecurityException -> FilesException(EngineError.FL_EIO, MSG_NO_ACCESS)
        is com.mawena.fluluks.remote.RemoteIOException -> FilesException(EngineError.FL_EIO, e.message ?: MSG_GENERIC)
        is IOException -> FilesException(EngineError.FL_EIO, MSG_GENERIC)
        is OutOfMemoryError -> FilesException(EngineError.FL_ENOMEM, MSG_GENERIC)
        else -> FilesException(EngineError.FL_EIO, MSG_GENERIC)
    }

    /** Éléments du dossier, dossiers d'abord puis par nom sans casse ; cachés = nom commençant par « . ». */
    fun list(sourceId: String, path: String, showHidden: Boolean): List<Entry> = guard("queryChildDocuments") {
        source(sourceId).list(path)
            .filter { showHidden || !it.name.startsWith(".") }
            .sortedWith(compareBy<Entry>({ !it.type.isDir() }, { it.name.lowercase() }, { it.name }))
    }

    fun createFolder(sourceId: String, parent: String, name: String): String = guard("createDocument") {
        source(sourceId).mkdir(parent, name)
    }

    fun rename(sourceId: String, path: String, newName: String): String = guard("renameDocument") {
        source(sourceId).rename(path, newName)
    }

    /** Supprime chaque élément (récursif) ; un disque n'est synchronisé qu'une fois pour tout le lot. */
    /**
     * [permanent] : supprime ; sinon met à la corbeille du stockage ([Trash], définitif pour ce qui y est déjà). Sans
     * corbeille (dossier distant), toujours définitif.
     */
    fun delete(sourceId: String, paths: List<String>, permanent: Boolean) = guard("deleteDocument") {
        val src = source(sourceId)
        if (permanent || !src.hasTrash) src.deleteAll(paths) else Trash.moveToTrash(src, paths)
    }

    /** ACTION_VIEW sur l'URI de contenu, dans un sélecteur « Ouvrir avec… ». */
    fun viewIntent(sourceId: String, path: String): Intent = guard("openDocument") {
        val src = source(sourceId)
        val e = src.stat(path) ?: throw FileNotFoundException("Document introuvable")
        if (e.type != FileType.FILE) throw FileNotFoundException("Ce n'est pas un fichier ordinaire")
        val uri = src.contentUri(e.path)
        var flags = Intent.FLAG_GRANT_READ_URI_PERMISSION
        if (src.readOnlyReason == null) flags = flags or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        val view = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, MimeTypes.forName(e.name))
            clipData = ClipData.newRawUri(e.name, uri)
            addFlags(flags)
        }
        Intent.createChooser(view, OPEN_WITH).addFlags(flags or Intent.FLAG_ACTIVITY_NEW_TASK)
    }

    /** ACTION_SEND ou ACTION_SEND_MULTIPLE, toutes les URI dans le ClipData ; un dossier est refusé. */
    fun shareIntent(sourceId: String, paths: List<String>): Intent = guard("openDocument") {
        if (paths.isEmpty()) throw FileNotFoundException("Rien à partager")
        val src = source(sourceId)
        val entries = paths.map { src.stat(it) ?: throw FileNotFoundException("Document introuvable") }
        if (entries.any { it.type != FileType.FILE }) {
            throw FilesException(EngineError.FL_EISDIR, MSG_SHARE_DIR)
        }
        val uris = entries.map { src.contentUri(it.path) }
        val types = entries.map { MimeTypes.forName(it.name) }.distinct()
        val send = Intent(if (uris.size == 1) Intent.ACTION_SEND else Intent.ACTION_SEND_MULTIPLE).apply {
            type = types.singleOrNull() ?: "*/*"
            if (uris.size == 1) putExtra(Intent.EXTRA_STREAM, uris[0])
            else putParcelableArrayListExtra(Intent.EXTRA_STREAM, ArrayList<Uri>(uris))
            clipData = ClipData.newRawUri(entries[0].name, uris[0]).also { c ->
                for (u in uris.drop(1)) c.addItem(ClipData.Item(u))
            }
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        Intent.createChooser(send, SHARE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
    }

    /** Vrai si une application sait ouvrir l'intention enveloppée par le sélecteur. */
    fun hasViewer(ctx: Context, chooser: Intent): Boolean {
        @Suppress("DEPRECATION")
        val inner = chooser.getParcelableExtra<Intent>(Intent.EXTRA_INTENT) ?: return false
        return ctx.packageManager.queryIntentActivities(inner, 0).isNotEmpty()
    }
}

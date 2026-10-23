package com.mawena.fluluks.files

import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import com.mawena.fluluks.engine.EngineError
import java.io.File

/** Fichier trouvé par un filtre rapide : stockage ([sourceId]) et chemin dans ce stockage ; [mtime] en secondes. */
data class FilterItem(val sourceId: String, val path: String, val name: String, val size: Long, val mtime: Long)

/**
 * Filtres rapides de l'accueil (phase 9) : photos, vidéos, musique, documents, archives, récents (modifiés depuis
 * 30 jours), sur le téléphone et les cartes SD par l'index d'Android (MediaStore), du plus récent au plus ancien,
 * [MAX_RESULTS] au plus. Les disques USB n'y sont pas (non indexés) ; fichiers cachés et corbeille exclus, comme
 * dans l'explorateur. Chaque chemin de l'index est rapporté à un stockage ([locate]) et vérifié sur place.
 */
object QuickFilters {
    const val MAX_RESULTS = 500
    const val RECENT_DAYS = 30L

    private val PHOTOS = setOf("jpg", "jpeg", "png", "gif", "webp", "heic", "heif", "dng", "bmp")
    private val VIDEOS = setOf("mp4", "mkv", "mov", "avi", "webm", "3gp", "m4v")
    private val MUSIC = setOf("mp3", "m4a", "ogg", "opus", "flac", "wav", "aac", "mid")
    private val DOCUMENTS = listOf("pdf", "doc", "docx", "odt", "xls", "xlsx", "ods", "ppt", "pptx", "odp", "txt", "md", "epub")
    private val ARCHIVES = listOf("zip", "7z", "rar", "tar", "tgz", "txz", "gz", "xz")

    /** Filtres auxquels appartient un fichier (vide : aucun) ; « récents » dépend de la date, pas du type. */
    fun kind(name: String, mime: String?): Set<String> {
        if (Archives.isArchive(name)) return setOf("archives")
        val ext = name.substringAfterLast('.', "").lowercase().takeIf { name.lastIndexOf('.') > 0 } ?: ""
        val m = mime.orEmpty()
        return when {
            ext in PHOTOS || m.startsWith("image/") -> setOf("photos")
            ext in VIDEOS || m.startsWith("video/") -> setOf("videos")
            ext in MUSIC || m.startsWith("audio/") -> setOf("music")
            ext in DOCUMENTS -> setOf("documents")
            else -> emptySet()
        }
    }

    /** Début de la fenêtre des récents, en secondes. */
    fun recentSince(nowMs: Long): Long = nowMs / 1000 - RECENT_DAYS * 86_400

    fun isRecent(mtimeSec: Long, nowMs: Long): Boolean = mtimeSec >= recentSince(nowMs)

    /**
     * (stockage, chemin) du fichier [abs] d'après les racines (identifiant, chemin absolu), ou null : hors des
     * stockages, racine elle-même, chemin non normalisé, élément caché ou dans la corbeille.
     */
    fun locate(abs: String, roots: List<Pair<String, String>>): Pair<String, String>? {
        for ((id, root) in roots) {
            val r = root.trimEnd('/')
            if (!abs.startsWith("$r/")) continue
            val p = PathRules.check(abs.substring(r.length)) ?: return null
            if (p == "/" || Trash.isInTrash(p) || p.split('/').any { it.startsWith(".") }) return null
            return id to p
        }
        return null
    }

    @Suppress("DEPRECATION") // colonne DATA : le chemin absolu reste lisible avec l'accès à tous les fichiers
    fun query(context: Context, filter: String, nowMs: Long = System.currentTimeMillis()): List<FilterItem> {
        val c = MediaStore.Files.FileColumns.DATA
        val name = MediaStore.Files.FileColumns.DISPLAY_NAME
        val type = MediaStore.Files.FileColumns.MEDIA_TYPE
        val date = MediaStore.Files.FileColumns.DATE_MODIFIED
        fun likeAny(exts: List<String>) = exts.joinToString(" OR ", "(", ")") { "LOWER($name) LIKE ?" } to exts.map { "%.$it" }
        val (selection, args) = when (filter) {
            "photos" -> "$type = ?" to listOf(MediaStore.Files.FileColumns.MEDIA_TYPE_IMAGE.toString())
            "videos" -> "$type = ?" to listOf(MediaStore.Files.FileColumns.MEDIA_TYPE_VIDEO.toString())
            "music" -> "$type = ?" to listOf(MediaStore.Files.FileColumns.MEDIA_TYPE_AUDIO.toString())
            "documents" -> likeAny(DOCUMENTS)
            "archives" -> likeAny(ARCHIVES)
            "recent" -> "$date >= ?" to listOf(recentSince(nowMs).toString())
            else -> throw FilesException(EngineError.FL_EINVAL, "Filtre inconnu")
        }
        val roots = Sources.list().filterIsInstance<LocalSource>().flatMap { s ->
            listOf(s.id to s.root.absolutePath, s.id to s.root.canonicalPath).distinct()
        }
        val volumes = if (Build.VERSION.SDK_INT >= 29) MediaStore.getExternalVolumeNames(context).toList() else listOf("external")
        val out = ArrayList<FilterItem>()
        for (v in volumes) {
            val uri: Uri = MediaStore.Files.getContentUri(v)
            context.contentResolver.query(uri, arrayOf(c, name, MediaStore.Files.FileColumns.SIZE, date), selection, args.toTypedArray(), "$date DESC")
                ?.use { cur ->
                    var kept = 0
                    while (kept < MAX_RESULTS && cur.moveToNext()) {
                        val abs = cur.getString(0) ?: continue
                        val (id, p) = locate(abs, roots) ?: continue
                        val f = File(abs)
                        if (!f.isFile) continue // index en retard, dossier
                        out += FilterItem(id, p, f.name, f.length(), f.lastModified() / 1000)
                        kept++
                    }
                }
        }
        return out.sortedByDescending { it.mtime }.take(MAX_RESULTS)
    }
}

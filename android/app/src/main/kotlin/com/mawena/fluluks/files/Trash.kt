package com.mawena.fluluks.files

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.DocNames
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import java.io.ByteArrayOutputStream
import java.io.FileNotFoundException
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/** Élément de la corbeille : [id] est son nom dans `files/` ; [name] et [originalPath] disent d'où il vient. */
data class TrashItem(
    val id: String, val name: String, val originalPath: String, val deletedAtMs: Long, val size: Long, val isDir: Boolean,
)

/**
 * Corbeille d'un stockage (phase 9) : dossier caché `.Portail-corbeille` à sa racine, disposition freedesktop
 * (`files/` et `info/<nom>.trashinfo`, avec `Path=` relatif à la racine et encodé comme une URL, et `DeletionDate=`
 * en heure locale). Mettre à la corbeille est un déplacement dans le même stockage, sans copie ; sur un disque
 * chiffré, la corbeille reste donc dans le volume chiffré. Les éléments de plus de 30 jours sont purgés à chaque
 * suppression et à l'ouverture de la corbeille. Une suppression dans la corbeille est définitive.
 *
 * Une fiche absente ou invalide (chemin hors du stockage, dans la corbeille, mal formé) n'empêche rien : l'élément
 * est rendu à la racine.
 */
object Trash {
    const val DIR = "/.Portail-corbeille"
    private const val FILES = "$DIR/files"
    private const val INFO = "$DIR/info"
    private const val SUFFIX = ".trashinfo"
    const val KEEP_MS = 30L * 24 * 3600 * 1000
    private val DATE = DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss")

    /** RFC 2396 : caractères non réservés, gardés tels quels (avec « / »). */
    private const val PLAIN = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'()/"

    fun isInTrash(path: String) = path == DIR || path.startsWith("$DIR/")

    private fun requireWritable(src: FileSource) {
        src.readOnlyReason?.let { throw FileNotFoundException(Files.readOnlyMessage(src, it)) }
    }

    /** Met [paths] à la corbeille de [src] (définitif pour ce qui est déjà dedans), après avoir purgé l'expiré. */
    fun moveToTrash(src: FileSource, paths: List<String>, nowMs: Long = System.currentTimeMillis()) {
        requireWritable(src)
        val ps = paths.map(PathRules::require)
        if (ps.any { it == "/" }) throw FileNotFoundException("La racine ne peut pas être supprimée")
        purgeExpired(src, nowMs)
        for (p in ps) {
            if (src.stat(p) == null) throw FileNotFoundException("Document introuvable")
            if (isInTrash(p)) {
                src.delete(p)
                if (WriteOps.parentOf(p) == FILES) deleteInfo(src, WriteOps.nameOf(p))
                continue
            }
            ensureDirs(src)
            val id = WriteOps.nameOf(src.move(p, FILES))
            writeInfo(src, id, p, nowMs)
        }
    }

    /** Éléments de la corbeille, du plus récemment supprimé au plus ancien. */
    fun list(src: FileSource): List<TrashItem> = items(src, sizes = true)

    /** [sizes] : taille des dossiers calculée (parcours complet de chacun) ; la purge s'en passe (taille 0). */
    private fun items(src: FileSource, sizes: Boolean): List<TrashItem> {
        if (src.stat(FILES)?.type?.isDir() != true) return emptyList()
        return src.list(FILES).map { e ->
            val (original, date) = readInfo(src, e.name) ?: ("/${e.name}" to 0L)
            val size = if (!e.type.isDir()) e.size else if (sizes) dirSize(src, e.path) else 0L
            TrashItem(e.name, WriteOps.nameOf(original), original, date, size, e.type.isDir())
        }.sortedByDescending { it.deletedAtMs }
    }

    /** Remet chaque élément à son emplacement d'origine (dossiers recréés au besoin, nom unique). */
    fun restore(src: FileSource, ids: List<String>) {
        requireWritable(src)
        for (id in ids) {
            val path = itemPath(id)
            if (src.stat(path) == null) throw FileNotFoundException("Élément introuvable dans la corbeille")
            val original = readInfo(src, id)?.first ?: "/$id"
            val parent = ensureDir(src, WriteOps.parentOf(original))
            val moved = src.move(path, parent)
            val movedName = WriteOps.nameOf(moved)
            val want = WriteOps.nameOf(original)
            val name = DocNames.unique(want) { it != movedName && src.stat(DocumentIds.child(parent, it)) != null }
                ?: throw FileNotFoundException("Trop d'éléments portent déjà ce nom")
            if (name != movedName) src.rename(moved, name)
            deleteInfo(src, id)
        }
    }

    /** Supprime définitivement les éléments [ids] et leurs fiches. */
    fun purge(src: FileSource, ids: List<String>) {
        requireWritable(src)
        for (id in ids) {
            src.stat(itemPath(id))?.let { src.delete(it.path) }
            deleteInfo(src, id)
        }
    }

    /** Vide la corbeille, fiches orphelines comprises. */
    fun empty(src: FileSource) {
        requireWritable(src)
        for (dir in listOf(FILES, INFO)) {
            if (src.stat(dir)?.type?.isDir() != true) continue
            src.deleteAll(src.list(dir).map { it.path })
        }
    }

    /** Supprime les éléments mis à la corbeille il y a plus de 30 jours ; rien sur un stockage en lecture seule. */
    fun purgeExpired(src: FileSource, nowMs: Long = System.currentTimeMillis()) {
        if (src.readOnlyReason != null || src.stat(FILES)?.type?.isDir() != true) return
        val old = items(src, sizes = false).filter { it.deletedAtMs > 0 && nowMs - it.deletedAtMs > KEEP_MS }.map { it.id }
        if (old.isNotEmpty()) purge(src, old)
    }

    private fun itemPath(id: String): String {
        if (!DocNames.isValid(id)) throw FileNotFoundException("Élément introuvable dans la corbeille")
        return DocumentIds.child(FILES, id)
    }

    /** Crée la corbeille et ses deux dossiers ; refuse un lien ou un fichier à leur place. */
    private fun ensureDirs(src: FileSource) {
        for ((parent, name) in listOf("/" to DIR.substring(1), DIR to "files", DIR to "info")) {
            val st = src.stat(DocumentIds.child(parent, name))
            if (st == null) {
                src.mkdir(parent, name)
            } else if (!st.type.isDir()) {
                throw FileNotFoundException("Corbeille inaccessible : « $name » n'est pas un dossier")
            }
        }
    }

    /** Dossier [dir], recréé au besoin (un fichier ou un lien à sa place : dossier au nom unique à côté). */
    private fun ensureDir(src: FileSource, dir: String): String {
        if (dir == "/") return "/"
        if (src.stat(dir)?.type == FileType.DIR) return dir
        return src.mkdir(ensureDir(src, WriteOps.parentOf(dir)), WriteOps.nameOf(dir))
    }

    private fun infoName(id: String) = "$id$SUFFIX"

    private fun writeInfo(src: FileSource, id: String, original: String, nowMs: Long) {
        val name = infoName(id)
        if (!DocNames.isValid(name)) return // nom trop long pour une fiche : l'élément sera rendu à la racine
        deleteInfo(src, id) // reste d'une suppression interrompue
        val date = LocalDateTime.ofInstant(Instant.ofEpochMilli(nowMs), ZoneId.systemDefault()).format(DATE)
        val text = "[Trash Info]\nPath=${encode(original.removePrefix("/"))}\nDeletionDate=$date\n"
        src.createFile(INFO, name).second.use { it.write(text.toByteArray(Charsets.UTF_8)) }
    }

    private fun deleteInfo(src: FileSource, id: String) {
        val name = infoName(id)
        if (!DocNames.isValid(name)) return
        src.stat(DocumentIds.child(INFO, name))?.let { if (it.type == FileType.FILE) src.delete(it.path) }
    }

    /** (chemin d'origine, date de suppression en ms ou 0) lus dans la fiche de [id] ; null si absente ou invalide. */
    private fun readInfo(src: FileSource, id: String): Pair<String, Long>? {
        val name = infoName(id)
        if (!DocNames.isValid(name)) return null
        val st = src.stat(DocumentIds.child(INFO, name)) ?: return null
        if (st.type != FileType.FILE || st.size > 64 * 1024) return null
        val text = try {
            src.openRead(st.path).use { String(it.readBytes(), Charsets.UTF_8) }
        } catch (e: Exception) {
            return null
        }
        var path: String? = null
        var date = 0L
        for (line in text.lines()) {
            if (line.startsWith("Path=")) path = decode(line.substring(5))
            else if (line.startsWith("DeletionDate=")) date = parseDate(line.substring(13))
        }
        val p = path?.let { PathRules.check(if (it.startsWith("/")) it else "/$it") } ?: return null
        if (p == "/" || isInTrash(p)) return null
        return p to date
    }

    private fun dirSize(src: FileSource, path: String): Long = try {
        Transfers.scanTree(src, path) {}.filter { it.type == FileType.FILE }.sumOf { it.size }
    } catch (e: Exception) {
        0L
    }

    private fun parseDate(s: String): Long = try {
        LocalDateTime.parse(s.trim(), DATE).atZone(ZoneId.systemDefault()).toInstant().toEpochMilli()
    } catch (e: Exception) {
        0L
    }

    private fun encode(path: String): String {
        val sb = StringBuilder()
        for (b in path.toByteArray(Charsets.UTF_8)) {
            val c = b.toInt() and 0xFF
            if (c < 0x80 && PLAIN.indexOf(c.toChar()) >= 0) sb.append(c.toChar()) else sb.append("%%%02X".format(c))
        }
        return sb.toString()
    }

    /** Inverse de [encode] ; null si une séquence « % » est invalide. */
    private fun decode(s: String): String? {
        val out = ByteArrayOutputStream()
        var i = 0
        while (i < s.length) {
            val c = s[i]
            if (c == '%') {
                if (i + 3 > s.length) return null
                val hi = Character.digit(s[i + 1], 16)
                val lo = Character.digit(s[i + 2], 16)
                if (hi < 0 || lo < 0) return null
                out.write(hi * 16 + lo)
                i += 3
            } else {
                out.write(c.toString().toByteArray(Charsets.UTF_8))
                i++
            }
        }
        return String(out.toByteArray(), Charsets.UTF_8)
    }
}

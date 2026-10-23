package com.mawena.fluluks.files

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.WriteOps
import java.io.FileNotFoundException
import java.security.MessageDigest

/**
 * Propriétés d'un élément. [mode], [uid], [gid] et [permissions] : sur ext4 seulement (ailleurs, FAT, exFAT,
 * NTFS et le stockage du téléphone, ils sont inventés par le pilote et n'apprendraient rien). [fs] : système de
 * fichiers d'un volume de disque, vide pour le téléphone et la carte SD.
 */
data class Details(
    val name: String, val path: String, val type: FileType, val size: Long, val mtime: Long, val mode: Int?, val uid: Long?,
    val gid: Long?, val fs: String,
) {
    val permissions: String? get() = mode?.let(FileDetails::modeString)
}

/** Taille d'un dossier : octets et nombre de fichiers et de dossiers sous lui ; [truncated] si arrêtée avant la fin. */
data class FolderSize(val size: Long, val files: Int, val dirs: Int, val truncated: Boolean)

/**
 * Calculs à la demande de la feuille des propriétés : empreinte SHA-256 d'un fichier (progression, annulable) et
 * taille d'un dossier (mêmes bornes que la recherche, annulable). [cancel] arrête les deux.
 */
object FileDetails {
    @Volatile private var hashFlag: BooleanArray? = null
    @Volatile private var sizeFlag: BooleanArray? = null

    fun cancel() {
        hashFlag?.let { it[0] = true }
        sizeFlag?.let { it[0] = true }
    }

    /** « rwxr-xr-x » des 12 bits bas de [mode] (setuid, setgid, sticky compris : s/S, t/T). */
    fun modeString(mode: Int): String {
        val sb = StringBuilder(9)
        val special = charArrayOf('s', 's', 't')
        for (who in 0..2) {
            val bits = mode shr (6 - 3 * who) and 7
            sb.append(if (bits and 4 != 0) 'r' else '-')
            sb.append(if (bits and 2 != 0) 'w' else '-')
            val x = bits and 1 != 0
            val sp = mode and (0x800 shr who) != 0 // 04000, 02000, 01000
            sb.append(
                when {
                    sp && x -> special[who]
                    sp -> special[who].uppercaseChar()
                    x -> 'x'
                    else -> '-'
                },
            )
        }
        return sb.toString()
    }

    /** SHA-256 en hexadécimal ; [onProgress] reçoit un pourcentage croissant par morceau lu ; null si annulé. */
    fun sha256(src: FileSource, path: String, onProgress: (Int) -> Unit): String? {
        val flag = BooleanArray(1)
        hashFlag = flag
        try {
            val st = src.stat(PathRules.require(path)) ?: throw FileNotFoundException("Document introuvable")
            if (st.type != FileType.FILE) throw FilesException(EngineError.FL_EISDIR, "Ce n'est pas un fichier ordinaire")
            val md = MessageDigest.getInstance("SHA-256")
            val buf = ByteArray(WriteOps.COPY_CHUNK)
            var done = 0L
            src.openRead(st.path).use { input ->
                while (true) {
                    if (flag[0]) return null
                    val k = input.read(buf)
                    if (k < 0) break
                    if (k == 0) continue
                    md.update(buf, 0, k)
                    done += k
                    onProgress(if (st.size > 0) minOf(100L, done * 100 / st.size).toInt() else 100)
                }
            }
            return md.digest().joinToString("") { "%02x".format(it) }
        } finally {
            if (hashFlag === flag) hashFlag = null
        }
    }

    /** Taille de [path] et de tout son contenu (liens non suivis, dossiers illisibles sautés). */
    fun folderSize(src: FileSource, path: String): FolderSize {
        val flag = BooleanArray(1)
        sizeFlag = flag
        try {
            val start = PathRules.require(path)
            var size = 0L
            var files = 0
            var dirs = 0
            var truncated = false
            val queue = ArrayDeque<Pair<String, Int>>().apply { add(start to 0) }
            var visited = 0
            while (queue.isNotEmpty()) {
                if (flag[0] || ++visited > WriteOps.MAX_ENTRIES) {
                    truncated = true
                    break
                }
                val (dir, depth) = queue.removeFirst()
                val children = try {
                    src.list(dir)
                } catch (e: FileNotFoundException) {
                    if (dir == start) throw e
                    continue
                } catch (e: EngineError) {
                    if (dir == start) throw e
                    continue
                }
                for (e in children) {
                    when (e.type) {
                        FileType.FILE -> {
                            size += e.size
                            files++
                        }
                        FileType.DIR -> {
                            dirs++
                            if (depth + 1 < Search.MAX_DEPTH) queue.addLast(e.path to depth + 1) else truncated = true
                        }
                        else -> {}
                    }
                }
            }
            return FolderSize(size, files, dirs, truncated)
        } finally {
            if (sizeFlag === flag) sizeFlag = null
        }
    }
}

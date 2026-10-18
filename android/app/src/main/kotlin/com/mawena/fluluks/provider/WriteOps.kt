package com.mawena.fluluks.provider

import com.mawena.fluluks.engine.DirEntry
import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.engine.FsStat
import com.mawena.fluluks.engine.OpenFlags
import java.io.FileNotFoundException

/**
 * Opérations d'écriture du provider sur un volume monté (chemins absolus déjà validés par
 * [DocumentIds.decode]). Les erreurs du moteur sortent en [EngineError], les refus en
 * [FileNotFoundException] avec un message français. L'appelant sérialise les appels (vérification puis
 * création d'un nom unique).
 */
class WriteOps(private val engine: Engine, private val vol: Long) {
    private fun b(path: String) = path.toByteArray(Charsets.UTF_8)

    /** null si le chemin n'existe pas. */
    fun statOrNull(path: String): FsStat? = try {
        FsStat.parse(engine.stat(vol, b(path)))
    } catch (e: EngineError) {
        if (e.code == EngineError.FL_ENOENT) null else throw e
    }

    private fun list(path: String): List<DirEntry> = DirEntry.parseList(engine.list(vol, b(path)))

    private fun check(rc: Int) { if (rc < 0) throw EngineError(rc) }

    private fun requireDir(path: String) {
        val st = statOrNull(path) ?: throw FileNotFoundException("Dossier introuvable")
        if (st.type != FileType.DIR) throw FileNotFoundException("Ce n'est pas un dossier")
    }

    private fun requireName(name: String?): String {
        if (name == null || !DocNames.isValid(name)) throw FileNotFoundException(INVALID_NAME)
        return name
    }

    /** Premier nom libre dans [parent] : « nom », puis « nom (1) »… */
    private fun uniqueChild(parent: String, name: String): String {
        val free = DocNames.unique(name) { statOrNull(DocumentIds.child(parent, it)) != null }
            ?: throw FileNotFoundException("Trop d'éléments portent déjà ce nom")
        return DocumentIds.child(parent, free)
    }

    /** Crée un dossier ou un fichier vide sous [parent] ; rend son chemin. */
    fun create(parent: String, name: String?, dir: Boolean): String {
        requireDir(parent)
        val path = uniqueChild(parent, requireName(name))
        if (dir) {
            check(engine.mkdir(vol, b(path)))
        } else {
            val f = EngineError.check(engine.openFile(vol, b(path), OpenFlags.RDWR or OpenFlags.CREAT))
            check(engine.closeFile(f))
        }
        return path
    }

    /** Supprime un fichier, ou un dossier et tout son contenu (parcours itératif, enfants d'abord). */
    fun delete(path: String) {
        if (path == "/") throw FileNotFoundException("La racine du volume ne peut pas être supprimée")
        val root = statOrNull(path) ?: throw FileNotFoundException("Document introuvable")
        // pile de (chemin, est un dossier, déjà développé)
        val stack = ArrayDeque<Triple<String, Boolean, Boolean>>()
        stack.addLast(Triple(path, root.type == FileType.DIR, false))
        while (stack.isNotEmpty()) {
            val (p, isDir, expanded) = stack.removeLast()
            if (isDir && !expanded) {
                stack.addLast(Triple(p, true, true))
                for (e in list(p)) {
                    if (e.name == "." || e.name == "..") continue
                    stack.addLast(Triple(DocumentIds.child(p, e.name), e.type == FileType.DIR, false))
                }
            } else {
                check(if (isDir) engine.rmdir(vol, b(p)) else engine.unlink(vol, b(p)))
            }
        }
    }

    /** Renomme dans le même dossier ; rend le nouveau chemin. */
    fun rename(path: String, name: String?): String {
        if (path == "/") throw FileNotFoundException("La racine du volume ne peut pas être renommée")
        val target = DocumentIds.child(parentOf(path), requireName(name))
        if (target == path) return path
        if (statOrNull(target) != null) throw FileNotFoundException("Un élément porte déjà ce nom")
        check(engine.rename(vol, b(path), b(target)))
        return target
    }

    /** Déplace sous [targetParent] (nom unique en cas de conflit) ; rend le nouveau chemin. */
    fun move(path: String, targetParent: String): String {
        if (path == "/") throw FileNotFoundException("La racine du volume ne peut pas être déplacée")
        if (statOrNull(path) == null) throw FileNotFoundException("Document introuvable")
        requireDir(targetParent)
        if (targetParent == path || DocumentIds.isDescendant(path, targetParent)) {
            throw FileNotFoundException("Un dossier ne peut pas être déplacé dans lui-même")
        }
        if (parentOf(path) == targetParent) return path
        val target = uniqueChild(targetParent, nameOf(path))
        check(engine.rename(vol, b(path), b(target)))
        return target
    }

    /** Copie un fichier ou un dossier (récursivement) sous [targetParent] ; rend le chemin de la copie. */
    fun copy(path: String, targetParent: String): String {
        if (path == "/") throw FileNotFoundException("La racine du volume ne peut pas être copiée")
        val st = statOrNull(path) ?: throw FileNotFoundException("Document introuvable")
        requireDir(targetParent)
        if (st.type == FileType.DIR && (targetParent == path || DocumentIds.isDescendant(path, targetParent))) {
            throw FileNotFoundException("Un dossier ne peut pas être copié dans lui-même")
        }
        val top = uniqueChild(targetParent, nameOf(path))
        // file de (source, destination, type)
        val queue = ArrayDeque<Triple<String, String, FileType>>()
        queue.addLast(Triple(path, top, st.type))
        while (queue.isNotEmpty()) {
            val (src, dst, type) = queue.removeFirst()
            when (type) {
                FileType.DIR -> {
                    check(engine.mkdir(vol, b(dst)))
                    for (e in list(src)) {
                        if (e.name == "." || e.name == "..") continue
                        queue.addLast(Triple(DocumentIds.child(src, e.name), DocumentIds.child(dst, e.name), e.type))
                    }
                }
                FileType.FILE -> copyFile(src, dst)
                else -> {} // lien ou fichier spécial : le moteur ne sait pas les créer, ignorés
            }
        }
        return top
    }

    private fun copyFile(src: String, dst: String) {
        val inp = EngineError.check(engine.openFile(vol, b(src)))
        try {
            val out = EngineError.check(engine.openFile(vol, b(dst), OpenFlags.RDWR or OpenFlags.CREAT or OpenFlags.TRUNC))
            var closed = false
            try {
                val buf = ByteArray(COPY_CHUNK)
                var off = 0L
                while (true) {
                    val n = engine.pread(inp, off, buf, buf.size)
                    if (n < 0) throw EngineError(n)
                    if (n == 0) break
                    val w = engine.pwrite(out, off, buf, n)
                    if (w < 0) throw EngineError(w)
                    off += n
                }
                closed = true
                check(engine.closeFile(out))
            } finally {
                if (!closed) engine.closeFile(out)
            }
        } finally {
            engine.closeFile(inp)
        }
    }

    companion object {
        const val INVALID_NAME = "Nom invalide"
        const val COPY_CHUNK = 1 shl 20

        fun parentOf(path: String): String {
            val i = path.lastIndexOf('/')
            return if (i <= 0) "/" else path.substring(0, i)
        }

        fun nameOf(path: String): String = path.substring(path.lastIndexOf('/') + 1)
    }
}

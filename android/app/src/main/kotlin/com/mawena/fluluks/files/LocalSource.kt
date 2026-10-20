package com.mawena.fluluks.files

import android.net.Uri
import androidx.core.content.FileProvider
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.DocNames
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import java.io.File
import java.io.FileInputStream
import java.io.FileNotFoundException
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.nio.file.Files as NioFiles
import java.nio.file.LinkOption

/**
 * Stockage du téléphone ou carte SD, par java.io.File sous [root]. Un chemin dont le chemin canonique sort
 * de [root] (lien symbolique vers l'extérieur) est refusé. [hasAccess] : faux tant que l'accès à tous les
 * fichiers n'est pas accordé.
 */
class LocalSource(
    override val id: String,
    override val stableKey: String,
    override val label: String,
    private val root: File,
    private val hasAccess: () -> Boolean,
) : FileSource {
    override val readOnlyReason: String?
        get() = if (root.canWrite()) null else "stockage en lecture seule"
    override val caseInsensitive = true

    private val canonRoot: File by lazy { root.canonicalFile }

    private fun file(path: String): File {
        if (!hasAccess()) throw FileNotFoundException(Files.MSG_NO_ACCESS)
        val p = PathRules.require(path)
        val f = if (p == "/") root else File(root, p.substring(1))
        val c = f.canonicalFile
        if (c != canonRoot && !c.path.startsWith(canonRoot.path + File.separator)) throw FileNotFoundException(Files.MSG_BAD_PATH)
        return f
    }

    private fun entry(f: File, path: String): Entry {
        val p = f.toPath()
        val type = when {
            NioFiles.isSymbolicLink(p) -> FileType.SYMLINK
            NioFiles.isDirectory(p, LinkOption.NOFOLLOW_LINKS) -> FileType.DIR
            NioFiles.isRegularFile(p, LinkOption.NOFOLLOW_LINKS) -> FileType.FILE
            else -> FileType.OTHER
        }
        return Entry(f.name, path, type, if (type == FileType.FILE) f.length() else 0L, f.lastModified() / 1000)
    }

    override fun list(path: String): List<Entry> {
        val d = file(path)
        if (!d.exists()) throw FileNotFoundException("Dossier introuvable")
        if (!d.isDirectory) throw FileNotFoundException("Ce n'est pas un dossier")
        val children = d.listFiles() ?: throw ProtectedDirException()
        val p = PathRules.require(path)
        return children.map { entry(it, DocumentIds.child(p, it.name)) }
    }

    override fun stat(path: String): Entry? {
        val f = file(path)
        if (!NioFiles.exists(f.toPath(), LinkOption.NOFOLLOW_LINKS)) return null
        return entry(f, PathRules.require(path))
    }

    override fun space(): Pair<Long, Long> = root.usableSpace to root.totalSpace

    private fun requireWritable() {
        readOnlyReason?.let { throw FileNotFoundException(it) }
    }

    private fun requireDir(parent: String): File {
        val d = file(parent)
        if (!d.isDirectory) throw FileNotFoundException("Dossier introuvable")
        return d
    }

    private fun requireName(name: String): String {
        if (!DocNames.isValid(name)) throw FileNotFoundException(WriteOps.INVALID_NAME)
        return name
    }

    private fun unique(dir: File, name: String): String =
        DocNames.unique(name) { File(dir, it).exists() } ?: throw FileNotFoundException("Trop d'éléments portent déjà ce nom")

    override fun mkdir(parent: String, name: String): String {
        requireWritable()
        val d = requireDir(parent)
        val n = unique(d, requireName(name))
        if (!File(d, n).mkdir()) throw FileNotFoundException("Création impossible")
        return DocumentIds.child(PathRules.require(parent), n)
    }

    override fun rename(path: String, newName: String): String {
        requireWritable()
        if (PathRules.require(path) == "/") throw FileNotFoundException("La racine ne peut pas être renommée")
        val f = file(path)
        if (!f.exists()) throw FileNotFoundException("Document introuvable")
        val n = requireName(newName)
        val target = File(f.parentFile, n)
        val targetPath = DocumentIds.child(WriteOps.parentOf(path), n)
        if (target.path == f.path) return path
        // « a.txt » → « A.txt » sur un stockage insensible à la casse : même fichier, renommage permis
        if (target.exists() && !target.name.equals(f.name, ignoreCase = true)) throw FileNotFoundException("Un élément porte déjà ce nom")
        if (!f.renameTo(target)) throw FileNotFoundException("Renommage impossible")
        return targetPath
    }

    override fun delete(path: String) {
        requireWritable()
        if (PathRules.require(path) == "/") throw FileNotFoundException("La racine ne peut pas être supprimée")
        val f = file(path)
        if (!NioFiles.exists(f.toPath(), LinkOption.NOFOLLOW_LINKS)) throw FileNotFoundException("Document introuvable")
        // les liens sont supprimés eux-mêmes, jamais suivis
        val stack = ArrayDeque<File>().apply { add(f) }
        val order = ArrayList<File>()
        while (stack.isNotEmpty()) {
            val c = stack.removeLast()
            order.add(c)
            if (!NioFiles.isSymbolicLink(c.toPath()) && c.isDirectory) {
                if (order.size > WriteOps.MAX_ENTRIES) throw FileNotFoundException(WriteOps.TOO_DEEP)
                (c.listFiles() ?: throw ProtectedDirException()).forEach { stack.addLast(it) }
            }
        }
        for (c in order.asReversed()) if (!c.delete()) throw FileNotFoundException("Suppression impossible : ${c.name}")
    }

    override fun move(path: String, targetParent: String): String {
        requireWritable()
        val p = PathRules.require(path)
        val tp = PathRules.require(targetParent)
        if (p == "/") throw FileNotFoundException("La racine ne peut pas être déplacée")
        val f = file(p)
        if (!f.exists()) throw FileNotFoundException("Document introuvable")
        val d = requireDir(tp)
        if (tp == p || DocumentIds.isDescendant(p, tp)) throw FileNotFoundException(Files.MSG_INTO_ITSELF)
        if (WriteOps.parentOf(p) == tp) return p
        val n = unique(d, f.name)
        if (!f.renameTo(File(d, n))) throw FileNotFoundException("Déplacement impossible")
        return DocumentIds.child(tp, n)
    }

    override fun openRead(path: String): InputStream {
        val f = file(path)
        if (!f.isFile) throw FileNotFoundException("Ce n'est pas un fichier ordinaire")
        return FileInputStream(f)
    }

    override fun createFile(parent: String, name: String): Pair<String, OutputStream> {
        requireWritable()
        val d = requireDir(parent)
        val n = unique(d, requireName(name))
        return DocumentIds.child(PathRules.require(parent), n) to FileOutputStream(File(d, n))
    }

    override fun contentUri(path: String): Uri {
        val ctx = Files.context ?: throw FileNotFoundException("Application non initialisée")
        return FileProvider.getUriForFile(ctx, Files.FILE_AUTHORITY, file(path))
    }
}

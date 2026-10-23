package com.mawena.fluluks.remote

import android.net.Uri
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Entry
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.NativeIn
import com.mawena.fluluks.files.NativeOut
import com.mawena.fluluks.files.PathRules
import com.mawena.fluluks.provider.DocNames
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.StreamProvider
import com.mawena.fluluks.provider.WriteOps
import java.io.FileNotFoundException
import java.io.InputStream
import java.io.OutputStream

/**
 * Dossier distant (FTP, SFTP) vu comme un stockage du gestionnaire (phase 10). Les noms venus du serveur sont
 * hostiles : vides, `.`, `..`, avec `/` ou NUL, ils sont écartés, et les chemins sont toujours refaits ici. Place
 * libre inconnue (−1, −1) ; pas de corbeille ; liens listés mais non suivis. Les lecteurs, « Ouvrir avec » et le
 * lecteur d'archives passent par le fournisseur de flux.
 */
class RemoteSource(val server: ServerConfig, val client: RemoteClient) : FileSource {
    override val id = "$PREFIX${server.id}"
    override val stableKey = id
    override val label = server.name
    override val readOnlyReason: String? = null
    override val caseInsensitive = false
    override val restrictedNames = false
    override val hasTrash = false

    override fun list(path: String): List<Entry> {
        val p = PathRules.require(path)
        return net { client.list(p) }.filter { DocNames.isValid(it.name) }
            .map { it.copy(path = DocumentIds.child(p, it.name)) }
    }

    override fun stat(path: String): Entry? {
        val p = PathRules.require(path)
        if (p == "/") return Entry("/", "/", FileType.DIR, 0, 0)
        return net { client.stat(p) }?.copy(name = WriteOps.nameOf(p), path = p)
    }

    override fun space(): Pair<Long, Long> = -1L to -1L

    private fun requireName(name: String): String {
        if (!DocNames.isValid(name)) throw FileNotFoundException(WriteOps.INVALID_NAME)
        return name
    }

    /** Nom libre sous [parent] : [name], sinon « name (1) »… */
    private fun free(parent: String, name: String): String =
        DocNames.unique(requireName(name)) { stat(DocumentIds.child(parent, it)) != null }
            ?: throw FileNotFoundException("Trop d'éléments portent déjà ce nom")

    override fun mkdir(parent: String, name: String): String {
        val p = PathRules.require(parent)
        val n = free(p, name)
        net { client.mkdir(p, n) }
        return DocumentIds.child(p, n)
    }

    override fun rename(path: String, newName: String): String {
        val p = PathRules.require(path)
        if (p == "/") throw FileNotFoundException("La racine ne peut pas être renommée")
        val n = requireName(newName)
        val target = DocumentIds.child(WriteOps.parentOf(p), n)
        if (target == p) return p
        if (stat(target) != null) throw FileNotFoundException("Un élément porte déjà ce nom")
        net { client.rename(p, target) }
        return target
    }

    override fun delete(path: String) {
        val p = PathRules.require(path)
        if (p == "/") throw FileNotFoundException("La racine ne peut pas être supprimée")
        val e = stat(p) ?: throw FileNotFoundException("Document introuvable")
        if (e.type == FileType.DIR) {
            for (c in list(p)) delete(c.path)
            net { client.delete(p, dir = true) }
        } else {
            net { client.delete(p, dir = false) } // un lien : le lien seul
        }
    }

    override fun move(path: String, targetParent: String): String {
        val p = PathRules.require(path)
        val t = PathRules.require(targetParent)
        if (t == p || t.startsWith("$p/")) throw FileNotFoundException("Un dossier ne peut pas être déplacé dans lui-même")
        val target = DocumentIds.child(t, free(t, WriteOps.nameOf(p)))
        net { client.rename(p, target) }
        return target
    }

    override fun openRead(path: String): InputStream = openReadAt(path, 0)

    override fun openReadAt(path: String, offset: Long): InputStream {
        val input = net { client.openReadAt(PathRules.require(path), offset) }
        return object : java.io.FilterInputStream(input) {
            override fun read(): Int = net { super.read() }
            override fun read(b: ByteArray, off: Int, len: Int): Int = net { super.read(b, off, len) }
            override fun skip(n: Long): Long = net { super.skip(n) }
            override fun close() = net { super.close() }
        }
    }

    override fun createFile(parent: String, name: String): Pair<String, OutputStream> {
        val p = PathRules.require(parent)
        val n = free(p, name)
        val out = net { client.create(p, n) }
        return DocumentIds.child(p, n) to object : java.io.FilterOutputStream(out) {
            override fun write(b: Int) = net { out.write(b) }
            override fun write(b: ByteArray, off: Int, len: Int) = net { out.write(b, off, len) }
            override fun flush() = net { out.flush() }
            override fun close() = net { out.close() }
        }
    }

    /** `STOR` (FTP) ou `put` en remplacement (SFTP) sur le fichier existant : le serveur garde ses droits. */
    override fun openOverwrite(path: String): OutputStream {
        val p = PathRules.require(path)
        val out = net { client.create(WriteOps.parentOf(p), WriteOps.nameOf(p)) }
        return object : java.io.FilterOutputStream(out) {
            override fun write(b: Int) = net { out.write(b) }
            override fun write(b: ByteArray, off: Int, len: Int) = net { out.write(b, off, len) }
            override fun flush() = net { out.flush() }
            override fun close() = net { out.close() }
        }
    }

    override fun contentUri(path: String): Uri = StreamProvider.uriFor(this, path)

    override fun openNative(path: String): NativeIn {
        val e = stat(path) ?: throw FileNotFoundException("Document introuvable")
        if (e.type != FileType.FILE) throw FileNotFoundException("Ce n'est pas un fichier ordinaire")
        return NativeIn.Proxy(e.size) { StreamProvider.openProxy(this, e.path, e.size).detachFd() }
    }

    override fun createNative(parent: String, name: String): Pair<String, NativeOut> {
        val (p, out) = createFile(parent, name)
        return p to NativeOut.Pipe(out) {}
    }

    companion object {
        const val PREFIX = "remote:"
    }
}

/**
 * Appel d'un client distant : ses échecs d'E/S portent un message écrit pour l'utilisateur, gardé tel quel
 * ([RemoteIOException]) ; « introuvable » reste une [FileNotFoundException].
 */
internal inline fun <T> net(block: () -> T): T = try {
    block()
} catch (e: FileNotFoundException) {
    throw e
} catch (e: RemoteIOException) {
    throw e
} catch (e: java.io.InterruptedIOException) {
    throw e
} catch (e: java.io.IOException) {
    throw RemoteIOException(e.message ?: "Connexion perdue", e)
}

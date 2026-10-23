package com.mawena.fluluks

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Entry
import com.mawena.fluluks.remote.RemoteClient
import java.io.File
import java.io.FileInputStream
import java.io.FileNotFoundException
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream

/**
 * Faux serveur distant sur un dossier local ([root]). [extra] : noms renvoyés en plus par le « serveur » pour un
 * dossier (noms hostiles), sans fichier derrière. Les chemins reçus sont notés dans [calls].
 */
class FakeRemoteClient(private val root: File) : RemoteClient {
    val extra = HashMap<String, List<String>>()
    val calls = ArrayList<String>()
    var closed = false

    /** Erreur levée par [list] (serveur occupé, connexion perdue…), et par la lecture des flux de [openReadAt]. */
    var listError: java.io.IOException? = null
    var readError: java.io.IOException? = null

    /** Appelé à chaque écriture d'un envoi, avec son rang (annulation pendant une copie vers le serveur). */
    var onWrite: (Int) -> Unit = {}
    private var writes = 0

    private fun f(path: String) = if (path == "/") root else File(root, path.removePrefix("/"))

    private fun entry(path: String, x: File) =
        Entry(x.name, path, if (x.isDirectory) FileType.DIR else FileType.FILE, if (x.isFile) x.length() else 0, x.lastModified() / 1000)

    override fun list(path: String): List<Entry> {
        calls += "list $path"
        listError?.let { throw it }
        val d = f(path)
        if (!d.isDirectory) throw FileNotFoundException("Dossier introuvable")
        val real = d.listFiles()!!.map { entry("$path/${it.name}".replace("//", "/"), it) }
        val fake = extra[path].orEmpty().map { Entry(it, "$path/$it", FileType.FILE, 1, 0) }
        return real + fake
    }

    override fun stat(path: String): Entry? {
        calls += "stat $path"
        val x = f(path)
        return if (x.exists()) entry(path, x) else null
    }

    override fun openReadAt(path: String, offset: Long): InputStream {
        calls += "read $path $offset"
        val err = readError
        if (err != null) return object : InputStream() { override fun read(): Int = throw err }
        return FileInputStream(f(path)).also { it.skip(offset) }
    }

    override fun create(parent: String, name: String): OutputStream {
        calls += "create $parent $name"
        return object : java.io.FilterOutputStream(FileOutputStream(File(f(parent), name))) {
            override fun write(b: ByteArray, off: Int, len: Int) {
                onWrite(++writes)
                out.write(b, off, len)
            }
        }
    }

    override fun mkdir(parent: String, name: String) {
        calls += "mkdir $parent $name"
        if (!File(f(parent), name).mkdir()) throw FileNotFoundException("Création impossible")
    }

    override fun rename(path: String, newPath: String) {
        calls += "rename $path $newPath"
        if (!f(path).renameTo(f(newPath))) throw FileNotFoundException("Renommage impossible")
    }

    override fun delete(path: String, dir: Boolean) {
        calls += "delete $path $dir"
        val x = f(path)
        if (dir && (x.list()?.isNotEmpty() == true)) throw FileNotFoundException("Dossier non vide")
        if (!x.delete()) throw FileNotFoundException("Suppression impossible")
    }

    override fun close() {
        closed = true
    }
}

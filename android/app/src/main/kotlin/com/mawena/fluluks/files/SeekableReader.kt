package com.mawena.fluluks.files

import java.io.Closeable
import java.io.InputStream

/**
 * Lecture à accès aléatoire sur un fichier de [size] octets qu'on ne sait lire qu'en flux ouvert à un décalage
 * ([open] : FTP `REST`, SFTP). Le flux courant est gardé : une lecture qui le suit le prolonge, un saut en avant de
 * [NEAR] octets au plus est lu et jeté, au-delà (ou en arrière) le flux est fermé puis rouvert au bon décalage.
 * Un seul flux ouvert à la fois ; pas sûr entre fils (le fournisseur de flux l'appelle sur un seul fil).
 */
class SeekableReader(val size: Long, private val open: (offset: Long) -> InputStream) : Closeable {
    private var stream: InputStream? = null
    private var pos = 0L

    /** Flux ouverts depuis le début (tests). */
    var opens = 0
        private set

    /** Lit jusqu'à [len] octets à [offset] dans [buf] ; rend le nombre lu, -1 à la fin du fichier. */
    fun read(offset: Long, buf: ByteArray, len: Int): Int {
        if (offset >= size) return -1
        val want = minOf(len.toLong(), size - offset).toInt()
        if (want <= 0) return 0
        position(offset)
        val s = stream!!
        var done = 0
        while (done < want) {
            val n = s.read(buf, done, want - done)
            if (n < 0) break
            done += n
            pos += n
        }
        return if (done == 0) -1 else done
    }

    /** Met le flux courant à [offset] : saut court lu et jeté, sinon réouverture. */
    private fun position(offset: Long) {
        val s = stream
        if (s != null && offset >= pos && offset - pos <= NEAR) {
            var left = offset - pos
            val skipBuf = ByteArray(minOf(left, 65536L).toInt().coerceAtLeast(1))
            while (left > 0) {
                val n = s.read(skipBuf, 0, minOf(left, skipBuf.size.toLong()).toInt())
                if (n < 0) break
                left -= n
                pos += n
            }
            if (left == 0L) return
        }
        close()
        stream = open(offset)
        opens++
        pos = offset
    }

    override fun close() {
        val s = stream ?: return
        stream = null
        runCatching { s.close() }
    }

    companion object {
        const val NEAR = 256L * 1024
    }
}

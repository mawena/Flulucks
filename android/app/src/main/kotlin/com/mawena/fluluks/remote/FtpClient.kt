package com.mawena.fluluks.remote

import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Entry
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import java.io.BufferedOutputStream
import java.io.BufferedWriter
import java.io.FileNotFoundException
import java.io.FilterInputStream
import java.io.FilterOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.InputStreamReader
import java.io.InterruptedIOException
import java.io.OutputStream
import java.io.OutputStreamWriter
import java.net.UnknownHostException
import java.time.Duration
import java.util.ArrayDeque
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import org.apache.commons.net.ftp.FTP
import org.apache.commons.net.ftp.FTPClient
import org.apache.commons.net.ftp.FTPFile
import org.apache.commons.net.ftp.FTPReply
import org.apache.commons.net.io.CRLFLineReader

/**
 * Dossier distant FTP (Commons Net) : mode passif, binaire, UTF-8 si le serveur annonce `UTF8` dans `FEAT` (`OPTS UTF8
 * ON`), sinon ISO-8859-1 ; `MLSD`/`MLST` si annoncés, sinon `CWD` + `LIST`. Anonyme si l'utilisateur est vide.
 *
 * Petite réserve d'au plus [MAX] connexions : chaque opération en emprunte une et la rend ; un flux de lecture
 * (`REST` + `RETR`) ou d'envoi (`STOR`) la garde jusqu'à sa fermeture. Une lecture finie est confirmée par le
 * serveur (`completePendingCommand`, sinon « Connexion perdue ») et sa connexion revient à la réserve ; un flux
 * abandonné en cours ou une connexion en erreur est jeté. Le constructeur ouvre la première connexion : il lève
 * [AuthFailed] (mot de passe manquant ou refusé) ou une [IOException] (« Serveur injoignable »…).
 *
 * Le mot de passe ([Secrets.password], tenu et remis à zéro par [Remotes]) n'est converti en texte que le temps du
 * `login` ; aucun message ni journal ne le contient. [timeoutMs] : données et réponses (30 s ; réduit par les tests).
 */
class FtpClient(
    private val server: ServerConfig,
    private val secrets: Secrets,
    private val timeoutMs: Int = DATA_TIMEOUT,
) : RemoteClient {
    /** Une connexion de la réserve ; [useUtf8] bascule le canal de commande après `OPTS UTF8 ON`. */
    private class Conn : FTPClient() {
        var since = 0L

        fun useUtf8() {
            setControlEncoding("UTF-8")
            _controlInput_ = CRLFLineReader(InputStreamReader(_input_, Charsets.UTF_8))
            _controlOutput_ = BufferedWriter(OutputStreamWriter(_output_, Charsets.UTF_8))
        }
    }

    /** Échec décrit par un message à montrer tel quel (les autres erreurs réseau deviennent « Connexion perdue »). */
    private class Failure(message: String) : IOException(message)

    private val lock = Any()
    private val permits = Semaphore(MAX, true)
    private val idle = ArrayDeque<Conn>()
    private val all = HashSet<Conn>()
    private var closed = false

    /** Extensions du serveur, lues à la première connexion. */
    @Volatile private var utf8: Boolean? = null
    @Volatile private var mlsd = false

    /** Connexions empruntées (tests). */
    @get:VisibleForTesting
    internal val busy get() = MAX - permits.availablePermits()

    /** Connexions ouvertes, empruntées ou en réserve (tests). */
    @get:VisibleForTesting
    internal val live get() = synchronized(lock) { all.size }

    /** Plus grand nombre de connexions ouvertes ensemble (tests). */
    @VisibleForTesting
    internal var peak = 0
        private set

    init {
        if (server.user.isNotEmpty() && secrets.password?.isNotEmpty() != true) throw AuthFailed(missing = true)
        release(borrow(), keep = true)
    }

    override fun list(path: String): List<Entry> = withConn { c ->
        val files = if (mlsd) {
            c.mlistDir(path)
        } else {
            if (!c.changeWorkingDirectory(path)) throw FileNotFoundException(NO_DIR)
            c.listFiles()
        }
        if (!FTPReply.isPositiveCompletion(c.replyCode)) throw FileNotFoundException(NO_DIR)
        files.mapNotNull { f ->
            if (f == null || f.name.isNullOrEmpty()) return@mapNotNull null
            val raw = f.rawListing.orEmpty().lowercase()
            if ("type=cdir" in raw || "type=pdir" in raw) return@mapNotNull null // le dossier lui-même, son parent
            entry(f.name, DocumentIds.child(path, f.name), f) // nom brut : filtré par RemoteSource
        }
    }

    override fun stat(path: String): Entry? = withConn { c ->
        val name = WriteOps.nameOf(path)
        if (mlsd) {
            c.mlistFile(path)?.let { entry(name, path, it) }
        } else {
            // sans MLST : la ligne de [path] dans la liste de son dossier
            if (!c.changeWorkingDirectory(WriteOps.parentOf(path))) return@withConn null
            c.listFiles().firstOrNull { it?.name == name }?.let { entry(name, path, it) }
        }
    }

    override fun openReadAt(path: String, offset: Long): InputStream {
        val c = borrow()
        val s = try {
            c.setRestartOffset(offset) // REST envoyé avant RETR si > 0
            c.retrieveFileStream(path)
        } catch (e: IOException) {
            release(c, keep = false)
            throw IOException(LOST, e)
        }
        if (s == null) {
            release(c, keep = true)
            throw FileNotFoundException(NO_FILE)
        }
        return Download(c, s)
    }

    override fun create(parent: String, name: String): OutputStream {
        val c = borrow()
        val s = try {
            c.storeFileStream(DocumentIds.child(parent, name))
        } catch (e: IOException) {
            release(c, keep = false)
            throw IOException(LOST, e)
        }
        if (s == null) {
            release(c, keep = true)
            throw FileNotFoundException("Écriture refusée par le serveur")
        }
        return Upload(c, BufferedOutputStream(s, 64 * 1024))
    }

    override fun mkdir(parent: String, name: String) = withConn { c ->
        if (!c.makeDirectory(DocumentIds.child(parent, name))) throw FileNotFoundException("Création impossible")
    }

    override fun rename(path: String, newPath: String) = withConn { c ->
        if (!c.rename(path, newPath)) throw FileNotFoundException("Renommage impossible")
    }

    override fun delete(path: String, dir: Boolean) = withConn { c ->
        val ok = if (dir) c.removeDirectory(path) else c.deleteFile(path)
        if (!ok) throw FileNotFoundException("Suppression impossible")
    }

    /** Ferme toutes les connexions, même empruntées (leurs flux échouent en « Connexion perdue »). */
    override fun close() {
        val l = synchronized(lock) {
            closed = true
            idle.clear()
            all.toList().also { all.clear() }
        }
        for (c in l) runCatching { c.disconnect() }
    }

    private fun entry(name: String, path: String, f: FTPFile): Entry {
        val type = when {
            f.isSymbolicLink -> FileType.SYMLINK
            f.isDirectory -> FileType.DIR
            f.isFile -> FileType.FILE
            else -> FileType.OTHER
        }
        val size = if (type == FileType.FILE) f.size.coerceAtLeast(0) else 0
        return Entry(name, path, type, size, (f.timestamp?.timeInMillis ?: 0) / 1000)
    }

    /** Emprunte une connexion pour [block] ; une erreur réseau la jette (« Connexion perdue »), un refus la garde. */
    private fun <T> withConn(block: (Conn) -> T): T {
        val c = borrow()
        var keep = false
        try {
            val r = block(c)
            keep = true
            return r
        } catch (e: FileNotFoundException) {
            keep = true
            throw e
        } catch (e: IOException) {
            throw IOException(LOST, e)
        } finally {
            release(c, keep)
        }
    }

    /** Une connexion de la réserve (vérifiée par `NOOP` après un repos), sinon une nouvelle ; attend s'il y en a [MAX]. */
    private fun borrow(): Conn {
        synchronized(lock) { if (closed) throw IOException(CLOSED) }
        try {
            if (!permits.tryAcquire(timeoutMs.toLong(), TimeUnit.MILLISECONDS)) {
                throw IOException("Serveur occupé : trop de transferts en cours")
            }
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            throw InterruptedIOException("Opération interrompue")
        }
        try {
            while (true) {
                val c = synchronized(lock) { if (closed) throw IOException(CLOSED); idle.pollFirst() } ?: break
                val fresh = System.currentTimeMillis() - c.since < IDLE_CHECK
                if (c.isConnected && (fresh || runCatching { c.sendNoOp() }.getOrDefault(false))) return c
                discard(c)
            }
            return open()
        } catch (e: Throwable) {
            permits.release()
            throw e
        }
    }

    /** Rend [c] : à la réserve si [keep], sinon (ou client fermé) déconnectée. */
    private fun release(c: Conn, keep: Boolean) {
        val drop = synchronized(lock) {
            if (keep && !closed && c.isConnected) {
                c.restartOffset = 0
                c.since = System.currentTimeMillis()
                idle.addFirst(c)
                false
            } else {
                true
            }
        }
        if (drop) discard(c)
        permits.release()
    }

    private fun discard(c: Conn) {
        synchronized(lock) {
            all.remove(c)
            idle.remove(c)
        }
        runCatching { c.disconnect() }
    }

    /**
     * Nouvelle connexion : connexion, identification, passif, binaire, encodage. L'identifiant et le mot de passe
     * partent en UTF-8 si le serveur l'annonce avant l'identification (FEAT, RFC 2389) ou l'a déjà annoncé ; sinon en
     * Latin-1, puis, s'ils sont refusés et contiennent des accents, une seconde fois en UTF-8 ([forceUtf8]).
     */
    private fun open(forceUtf8: Boolean = false): Conn {
        val c = Conn()
        c.connectTimeout = CONNECT_TIMEOUT
        c.defaultTimeout = timeoutMs
        c.setDataTimeout(Duration.ofMillis(timeoutMs.toLong()))
        c.listHiddenFiles = true // LIST -a : fichiers cachés listés comme par MLSD
        synchronized(lock) {
            all += c
            peak = maxOf(peak, all.size)
        }
        try {
            try {
                c.connect(server.host, server.port.takeIf { it in 1..65535 } ?: PORT)
            } catch (e: UnknownHostException) {
                throw Failure("Adresse du serveur introuvable")
            } catch (e: IOException) {
                throw Failure("Serveur injoignable")
            }
            if (!FTPReply.isPositiveCompletion(c.replyCode)) throw Failure("Connexion refusée par le serveur")
            val early = forceUtf8 || utf8 == true || (utf8 == null && utf8BeforeLogin(c))
            if (early) {
                c.sendCommand("OPTS UTF8 ON")
                c.useUtf8()
            }
            if (!login(c)) {
                if (early || !accentedCredentials()) throw AuthFailed(missing = false)
                discard(c)
                return open(forceUtf8 = true)
            }
            c.enterLocalPassiveMode()
            if (!c.setFileType(FTP.BINARY_FILE_TYPE)) throw Failure("Mode binaire refusé par le serveur")
            val u = utf8 ?: c.hasFeature("UTF8").also {
                // RFC 3659 : MLST annoncé vaut aussi pour MLSD
                mlsd = c.hasFeature("MLST") || c.hasFeature("MLSD")
                utf8 = it
            }
            if (u && !early) {
                c.sendCommand("OPTS UTF8 ON") // certains serveurs répondent 5xx tout en parlant UTF-8
                c.useUtf8()
            }
            return c
        } catch (e: AuthFailed) {
            discard(c)
            throw e
        } catch (e: Failure) {
            discard(c)
            throw e
        } catch (e: IOException) {
            discard(c)
            throw IOException(LOST) // sans la cause : échange d'identification
        } catch (e: RuntimeException) {
            discard(c)
            throw e
        }
    }

    /** `USER` + `PASS` ; faux si refusés. Le mot de passe n'existe en texte que pendant l'appel. */
    private fun login(c: Conn): Boolean = if (server.user.isEmpty()) {
        c.login(ANON_USER, ANON_PASSWORD)
    } else {
        val pw = secrets.password?.takeIf { it.isNotEmpty() } ?: throw AuthFailed(missing = true)
        c.login(server.user, String(pw, Charsets.UTF_8))
    }

    /** `FEAT` avant l'identification : vrai si le serveur y annonce UTF8 (beaucoup refusent avant `USER`). */
    private fun utf8BeforeLogin(c: Conn): Boolean =
        runCatching { FTPReply.isPositiveCompletion(c.feat()) && c.hasFeature("UTF8") }.getOrDefault(false)

    private fun accentedCredentials(): Boolean =
        server.user.any { it.code > 0x7F } || secrets.password?.any { it < 0 } == true

    /** Flux d'un `RETR` : la fin des données doit être confirmée par le serveur, sinon le fichier est tronqué. */
    private inner class Download(private val c: Conn, s: InputStream) : FilterInputStream(s) {
        private var done = false
        private var closed = false

        override fun read(): Int {
            val b = ByteArray(1)
            while (true) {
                val n = read(b, 0, 1)
                if (n < 0) return -1
                if (n == 1) return b[0].toInt() and 0xff
            }
        }

        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (closed) throw IOException("Flux fermé")
            if (done) return -1
            if (len == 0) return 0
            val n = try {
                `in`.read(b, off, len)
            } catch (e: IOException) {
                throw IOException(LOST, e)
            }
            if (n < 0) finish()
            return n
        }

        /** Lu et jeté : la fin des données passe toujours par [read]. */
        override fun skip(n: Long): Long {
            if (n <= 0) return 0
            val buf = ByteArray(minOf(n, 65536L).toInt())
            var left = n
            while (left > 0) {
                val r = read(buf, 0, minOf(left, buf.size.toLong()).toInt())
                if (r < 0) break
                left -= r
            }
            return n - left
        }

        override fun available(): Int = if (done || closed) 0 else runCatching { `in`.available() }.getOrDefault(0)

        override fun markSupported() = false

        private fun finish() {
            val ok = try {
                `in`.close()
                c.completePendingCommand()
            } catch (e: IOException) {
                false
            }
            if (!ok) throw IOException(LOST)
            done = true
        }

        override fun close() {
            if (closed) return
            closed = true
            if (!done) runCatching { `in`.close() } // abandonné en cours : connexion jetée
            release(c, keep = done)
        }
    }

    /** Flux d'un `STOR` : l'envoi se termine à la fermeture, confirmé par le serveur. */
    private inner class Upload(private val c: Conn, s: OutputStream) : FilterOutputStream(s) {
        private var failed = false
        private var closed = false

        override fun write(b: Int) = guard { out.write(b) }

        override fun write(b: ByteArray, off: Int, len: Int) = guard { out.write(b, off, len) }

        override fun flush() = guard { out.flush() }

        private inline fun guard(op: () -> Unit) {
            if (closed) throw IOException("Flux fermé")
            if (failed) throw IOException(LOST)
            try {
                op()
            } catch (e: IOException) {
                failed = true
                throw IOException(LOST, e)
            }
        }

        override fun close() {
            if (closed) return
            closed = true
            var keep = false
            try {
                if (failed) throw IOException(LOST)
                val ok = try {
                    out.close()
                    c.completePendingCommand()
                } catch (e: IOException) {
                    throw IOException(LOST, e)
                }
                keep = true
                if (!ok) throw IOException("Envoi refusé par le serveur")
            } finally {
                if (!keep) runCatching { out.close() }
                release(c, keep)
            }
        }
    }

    companion object {
        const val PORT = 21
        const val MAX = 3
        const val CONNECT_TIMEOUT = 10_000
        const val DATA_TIMEOUT = 30_000
        const val ANON_USER = "anonymous"
        const val ANON_PASSWORD = "portail@"

        /** Repos après lequel une connexion de la réserve est vérifiée (`NOOP`) avant d'être reprise. */
        private const val IDLE_CHECK = 15_000L
        private const val LOST = "Connexion perdue"
        private const val CLOSED = "Serveur déconnecté"
        private const val NO_DIR = "Dossier introuvable"
        private const val NO_FILE = "Fichier introuvable"
    }
}

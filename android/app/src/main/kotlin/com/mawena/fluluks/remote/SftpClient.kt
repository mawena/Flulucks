package com.mawena.fluluks.remote

import com.jcraft.jsch.ChannelSftp
import com.jcraft.jsch.HostKey
import com.jcraft.jsch.HostKeyRepository
import com.jcraft.jsch.JSch
import com.jcraft.jsch.JSchException
import com.jcraft.jsch.KeyPair
import com.jcraft.jsch.Session
import com.jcraft.jsch.SftpATTRS
import com.jcraft.jsch.SftpException
import com.jcraft.jsch.SftpProgressMonitor
import com.jcraft.jsch.UserInfo
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Entry
import com.mawena.fluluks.provider.DocumentIds
import java.io.FileNotFoundException
import java.io.FilterInputStream
import java.io.FilterOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.SocketTimeoutException
import java.net.UnknownHostException
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit

/**
 * Client SFTP (JSch, phase 10) : une session SSH par serveur et au plus [MAX_CHANNELS] canaux SFTP, gardés en réserve
 * entre deux opérations ; un flux garde son canal jusqu'à sa fermeture. La clé du serveur est vérifiée contre
 * [ServerConfig.hostKey] (jamais acceptée en silence) ; authentification par la clé privée importée si elle existe,
 * sinon par le mot de passe. Les chemins sont protégés des motifs de JSch (`*`, `?`, `\` pris à la lettre) ; liens
 * symboliques listés (`lstat`) mais jamais suivis. [secrets] appartient à [Remotes], qui les remet à zéro.
 */
class SftpClient private constructor(
    private val server: ServerConfig,
    private val secrets: Secrets,
    private val waitMs: Long,
) : RemoteClient {
    private val jsch: JSch = PrivateKeys.jsch()
    private val permits = Semaphore(MAX_CHANNELS, true)
    private val idle = ArrayDeque<ChannelSftp>()
    private val sessionLock = Any()
    @Volatile private var session: Session? = null
    @Volatile private var closed = false

    /** Identifiants inutilisables, connus avant d'appeler le serveur (rien à envoyer, ou phrase refusée). */
    private var authProblem: AuthFailed? = null
    private val useKey = secrets.privateKey != null

    private fun prepareIdentity() {
        val key = secrets.privateKey
        if (key == null) {
            if (secrets.password == null) authProblem = AuthFailed(missing = true)
            return
        }
        val copy = key.copyOf()
        var phrase: ByteArray? = null
        try {
            val encrypted = try {
                KeyPair.load(jsch, copy, null).let { kp -> kp.isEncrypted.also { kp.dispose() } }
            } catch (e: Exception) {
                throw IOException("Clé privée illisible")
            }
            if (encrypted) {
                // la phrase mémorisée, sinon le mot de passe saisi à la connexion
                phrase = (secrets.keyPassphrase ?: secrets.password)?.copyOf()
                if (phrase == null) {
                    authProblem = AuthFailed(missing = true)
                    return
                }
            }
            try {
                jsch.addIdentity("portail", copy, null, phrase)
            } catch (e: JSchException) {
                authProblem = AuthFailed(missing = false)
            }
        } finally {
            copy.fill(0)
            phrase?.fill(0)
        }
    }

    /** Clé du serveur : celle acceptée ([accepted]) ou rien ; la clé vue est notée pour le message. */
    private class TrustedKey(private val accepted: String?) : HostKeyRepository {
        @Volatile var seen: String? = null
        @Volatile var verdict = HostKeyRepository.OK

        override fun check(host: String?, key: ByteArray?): Int {
            val k = PrivateKeys.hostKey(key ?: ByteArray(0))
            seen = k
            verdict = when (accepted) {
                null -> HostKeyRepository.NOT_INCLUDED
                k -> HostKeyRepository.OK
                else -> HostKeyRepository.CHANGED
            }
            return verdict
        }

        override fun add(hostkey: HostKey?, ui: UserInfo?) {} // la confiance passe par Remotes.trustHostKey
        override fun remove(host: String?, type: String?) {}
        override fun remove(host: String?, type: String?, key: ByteArray?) {}
        override fun getKnownHostsRepositoryID() = "portail"
        override fun getHostKey(): Array<HostKey> = emptyArray()
        override fun getHostKey(host: String?, type: String?): Array<HostKey> = emptyArray()
    }

    private fun openSession(): Session {
        val trusted = TrustedKey(server.hostKey)
        jsch.hostKeyRepository = trusted
        val s = try {
            jsch.getSession(server.user, server.host, server.port)
        } catch (e: JSchException) {
            throw IOException("Adresse du serveur invalide")
        }
        s.setConfig("StrictHostKeyChecking", "yes")
        s.setConfig("PreferredAuthentications", if (useKey) "publickey" else "password,keyboard-interactive")
        hostKeyOrder()?.let { s.setConfig("server_host_key", it) }
        if (!useKey) secrets.password?.let { s.setPassword(it) } // copié par JSch, remis à zéro après la connexion
        s.timeout = DATA_TIMEOUT_MS
        s.serverAliveInterval = ALIVE_MS
        try {
            s.connect(CONNECT_TIMEOUT_MS)
        } catch (e: JSchException) {
            s.disconnect()
            throw when {
                trusted.verdict == HostKeyRepository.NOT_INCLUDED -> HostKeyUnknown(trusted.seen!!)
                trusted.verdict == HostKeyRepository.CHANGED -> HostKeyChanged(server.hostKey!!, trusted.seen!!)
                isAuth(e) -> authProblem ?: AuthFailed(missing = false)
                else -> connectError(e)
            }
        }
        return s
    }

    /** Algorithmes de clé du serveur, celui de la clé acceptée en tête (sinon une autre clé du serveur paraîtrait changée). */
    private fun hostKeyOrder(): String? {
        val type = server.hostKey?.substringBefore(' ')?.takeIf { it.isNotEmpty() } ?: return null
        val first = if (type == "ssh-rsa") listOf("rsa-sha2-512", "rsa-sha2-256") else listOf(type)
        val all = JSch.getConfig("server_host_key").split(',').filter { it.isNotEmpty() }
        return (first + all.filter { it !in first }).joinToString(",")
    }

    /** JSch range ses refus d'authentification en « Auth fail… » ou « Auth cancel… ». */
    private fun isAuth(e: JSchException) = e.message.orEmpty().let { it.startsWith("Auth fail") || it.startsWith("Auth cancel") }

    private fun connectError(e: JSchException): IOException {
        var c: Throwable? = e.cause
        while (c != null) {
            if (c is UnknownHostException) return IOException("Serveur introuvable", e)
            if (c is SocketTimeoutException) return IOException("Le serveur ne répond pas", e)
            c = c.cause
        }
        return IOException("Connexion impossible au serveur", e)
    }

    private fun session(): Session = synchronized(sessionLock) {
        if (closed) throw IOException("Déconnecté du serveur")
        session?.takeIf { it.isConnected }?.let { return it }
        session?.disconnect()
        synchronized(idle) {
            idle.forEach { it.disconnect() }
            idle.clear()
        }
        openSession().also { session = it }
    }

    /** Un canal libre (attend [waitMs] au plus qu'un des [MAX_CHANNELS] soit rendu). */
    private fun borrow(): ChannelSftp {
        if (closed) throw IOException("Déconnecté du serveur")
        if (!permits.tryAcquire(waitMs, TimeUnit.MILLISECONDS)) {
            throw IOException("Serveur occupé : trop de transferts en cours, réessayez")
        }
        try {
            while (true) {
                val c = synchronized(idle) { idle.removeFirstOrNull() } ?: break
                if (c.isConnected && session?.isConnected == true) return c
                c.disconnect()
            }
            val ch = session().openChannel("sftp") as ChannelSftp
            try {
                ch.connect(CONNECT_TIMEOUT_MS)
            } catch (e: JSchException) {
                ch.disconnect()
                throw IOException("Connexion perdue", e)
            }
            return ch
        } catch (e: Throwable) {
            permits.release()
            throw e
        }
    }

    private fun giveBack(ch: ChannelSftp, broken: Boolean) {
        if (broken || closed || !ch.isConnected) {
            ch.disconnect()
        } else {
            synchronized(idle) { idle.addLast(ch) }
        }
        permits.release()
    }

    /** Erreur d'une opération ; [broken] : le canal n'est plus sûr, on le ferme. */
    private class Failure(val error: IOException, val broken: Boolean)

    private fun failure(e: Exception): Failure = when (e) {
        is SftpException -> when (e.id) {
            ChannelSftp.SSH_FX_NO_SUCH_FILE -> Failure(FileNotFoundException("Document introuvable"), false)
            ChannelSftp.SSH_FX_PERMISSION_DENIED -> Failure(IOException("Accès refusé par le serveur"), false)
            ChannelSftp.SSH_FX_NO_CONNECTION, ChannelSftp.SSH_FX_CONNECTION_LOST ->
                Failure(IOException("Connexion perdue", e), true)
            // JSch range ses propres erreurs d'entrée-sortie en SSH_FX_FAILURE avec une cause
            else -> if (e.cause != null) Failure(IOException("Connexion perdue", e), true)
            else Failure(IOException("Le serveur a refusé l'opération"), false)
        }
        is IOException -> Failure(IOException("Connexion perdue", e), true)
        else -> Failure(IOException("Connexion perdue", e), true)
    }

    private fun <T> op(block: (ChannelSftp) -> T): T {
        val ch = borrow()
        var broken = false
        try {
            return block(ch)
        } catch (e: Exception) {
            val f = failure(e)
            broken = f.broken
            throw f.error
        } finally {
            giveBack(ch, broken)
        }
    }

    private fun type(a: SftpATTRS): FileType = when {
        a.flags and SftpATTRS.SSH_FILEXFER_ATTR_PERMISSIONS == 0 -> FileType.FILE
        a.isLink -> FileType.SYMLINK
        a.isDir -> FileType.DIR
        a.isReg -> FileType.FILE
        else -> FileType.OTHER
    }

    private fun entry(name: String, path: String, a: SftpATTRS): Entry {
        val t = type(a)
        return Entry(name, path, t, if (t == FileType.FILE) a.size else 0, a.mTime.toLong() and 0xffffffffL)
    }

    override fun list(path: String): List<Entry> = op { ch ->
        val out = ArrayList<Entry>()
        ch.ls(quote(path)) { e ->
            if (e.filename != "." && e.filename != "..") out += entry(e.filename, DocumentIds.child(path, e.filename), e.attrs)
            ChannelSftp.LsEntrySelector.CONTINUE
        }
        out
    }

    override fun stat(path: String): Entry? = op { ch ->
        try {
            entry(path.substringAfterLast('/'), path, ch.lstat(quote(path)))
        } catch (e: SftpException) {
            if (e.id == ChannelSftp.SSH_FX_NO_SUCH_FILE) null else throw e
        }
    }

    override fun openReadAt(path: String, offset: Long): InputStream {
        val ch = borrow()
        val input = try {
            ch.get(quote(path), null as SftpProgressMonitor?, offset)
        } catch (e: Exception) {
            val f = failure(e)
            giveBack(ch, f.broken)
            throw f.error
        }
        return object : FilterInputStream(input) {
            private var done = false
            private var broken = false

            private inline fun <T> guard(block: () -> T): T = try {
                block()
            } catch (e: IOException) {
                broken = true
                throw IOException("Connexion perdue", e)
            }

            override fun read(): Int = guard { super.read() }
            override fun read(b: ByteArray, off: Int, len: Int): Int = guard { super.read(b, off, len) }
            override fun skip(n: Long): Long = guard { super.skip(n) }

            override fun close() {
                if (done) return
                done = true
                try {
                    super.close()
                } catch (e: IOException) {
                    broken = true
                } finally {
                    giveBack(ch, broken)
                }
            }
        }
    }

    override fun create(parent: String, name: String): OutputStream {
        val ch = borrow()
        val output = try {
            ch.put(quote(DocumentIds.child(parent, name)), ChannelSftp.OVERWRITE)
        } catch (e: Exception) {
            val f = failure(e)
            giveBack(ch, f.broken)
            throw f.error
        }
        return object : FilterOutputStream(output) {
            private var done = false
            private var broken = false

            private inline fun guard(block: () -> Unit) = try {
                block()
            } catch (e: IOException) {
                broken = true
                throw IOException("Envoi interrompu : connexion perdue", e)
            }

            override fun write(b: Int) = guard { out.write(b) }
            override fun write(b: ByteArray, off: Int, len: Int) = guard { out.write(b, off, len) }
            override fun flush() = guard { out.flush() }

            override fun close() {
                if (done) return
                done = true
                try {
                    guard { out.close() } // l'envoi se termine ici : une erreur doit remonter
                } finally {
                    giveBack(ch, broken)
                }
            }
        }
    }

    // mkdir de JSch ne traite pas les motifs : chemin passé tel quel
    override fun mkdir(parent: String, name: String) = op { it.mkdir(DocumentIds.child(parent, name)) }

    override fun rename(path: String, newPath: String) = op { it.rename(quote(path), quote(newPath)) }

    override fun delete(path: String, dir: Boolean) = op { if (dir) it.rmdir(quote(path)) else it.rm(quote(path)) }

    override fun close() {
        closed = true
        synchronized(idle) {
            idle.forEach { it.disconnect() }
            idle.clear()
        }
        synchronized(sessionLock) {
            session?.disconnect()
            session = null
        }
        runCatching { jsch.removeAllIdentity() } // efface les clés déchiffrées
    }

    companion object {
        const val MAX_CHANNELS = 3
        const val CONNECT_TIMEOUT_MS = 10_000
        const val DATA_TIMEOUT_MS = 30_000
        const val ALIVE_MS = 30_000

        /** Attente d'un canal libre quand les [MAX_CHANNELS] sont pris. */
        const val WAIT_MS = 30_000L

        /**
         * Connecte [server] : clé du serveur vérifiée puis authentification. Lève [HostKeyUnknown] (rien n'a été
         * envoyé), [HostKeyChanged], [AuthFailed] ou une [IOException] au message français, jamais avec un secret.
         */
        fun connect(server: ServerConfig, secrets: Secrets, waitMs: Long = WAIT_MS): SftpClient {
            val c = SftpClient(server, secrets, waitMs)
            try {
                c.prepareIdentity()
                c.session()
            } catch (e: Throwable) {
                c.close()
                throw e
            }
            return c
        }

        /** Chemin pris à la lettre par JSch, qui lit `*` et `?` comme des motifs et `\` comme un échappement. */
        internal fun quote(path: String): String = buildString(path.length + 4) {
            for (ch in path) {
                if (ch == '\\' || ch == '*' || ch == '?') append('\\')
                append(ch)
            }
        }
    }
}

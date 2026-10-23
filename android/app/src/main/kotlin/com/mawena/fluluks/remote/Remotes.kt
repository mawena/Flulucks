package com.mawena.fluluks.remote

import android.content.Context
import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.provider.StreamTokens
import java.io.FileNotFoundException
import java.util.concurrent.ConcurrentHashMap

/**
 * Connexions aux serveurs enregistrés (phase 10). Une connexion ouverte est une [RemoteSource] de `Sources.list()` ;
 * ses secrets restent en mémoire le temps de la connexion (reconnexions du client), puis sont remis à zéro.
 */
object Remotes {
    /** Crée le client d'un protocole ; les tâches FTP et SFTP le remplissent. */
    fun interface Factory {
        fun connect(server: ServerConfig, secrets: Secrets): RemoteClient
    }

    @Volatile private var store: RemoteServers? = null
    @Volatile private var factory: Factory = Factory { s, _ ->
        throw FilesException(EngineError.FL_ENOTSUP, "Protocole ${s.protocol.uppercase()} pas encore pris en charge")
    }
    private val open = ConcurrentHashMap<String, Pair<RemoteSource, Secrets>>()

    fun init(context: Context) {
        if (store == null) store = RemoteServers(context.applicationContext.filesDir, KeystoreSecretBox())
    }

    fun servers(): RemoteServers = store ?: throw FilesException(EngineError.FL_ENODEV, "Serveurs indisponibles")

    /** Tests JVM : magasin et fabrique simulés ; tout est déconnecté. */
    @VisibleForTesting
    fun resetForTests(store: RemoteServers?, factory: Factory?) {
        for (id in open.keys.toList()) disconnect(id)
        this.store = store
        if (factory != null) this.factory = factory
    }

    fun setFactory(f: Factory) {
        factory = f
    }

    /**
     * Connecte [serverId] ([password] : saisi à l'instant, sinon celui mémorisé) ; déjà connecté : la même source.
     * Lève [HostKeyUnknown], [HostKeyChanged] ou [AuthFailed] selon le cas.
     */
    fun connect(serverId: String, password: ByteArray?): RemoteSource {
        open[serverId]?.let { return it.first }
        val s = servers()
        val cfg = s.get(serverId) ?: throw FileNotFoundException("Serveur introuvable")
        val stored = s.secrets(serverId)
        val sec = Secrets(password?.copyOf() ?: stored?.password, stored?.keyPassphrase, stored?.privateKey)
        if (password != null) stored?.password?.fill(0)
        val client = try {
            factory.connect(cfg, sec)
        } catch (e: Exception) {
            sec.wipe()
            throw e
        }
        val src = RemoteSource(cfg, client)
        open[serverId] = src to sec
        return src
    }

    fun disconnect(serverId: String) {
        val (src, sec) = open.remove(serverId) ?: return
        runCatching { src.client.close() }
        sec.wipe()
        StreamTokens.forget(src.id)
    }

    fun connected(): List<RemoteSource> = open.values.map { it.first }

    fun isConnected(serverId: String) = open.containsKey(serverId)

    /** Accepte la clé SFTP [fingerprint] pour [serverId] (première connexion, après l'avoir montrée). */
    fun trustHostKey(serverId: String, fingerprint: String) = servers().setHostKey(serverId, fingerprint)

    fun forgetHostKey(serverId: String) = servers().setHostKey(serverId, null)
}

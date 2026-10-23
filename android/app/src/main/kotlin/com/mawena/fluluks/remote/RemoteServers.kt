package com.mawena.fluluks.remote

import com.mawena.fluluks.files.PathRules
import java.io.File
import java.util.Base64
import java.util.UUID
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/**
 * Serveur enregistré : [protocol] `ftp` ou `sftp` ; [startPath] : dossier ouvert à la connexion ; [remember] : mot de
 * passe et phrase de clé mémorisés (scellés) ; [hostKey] : clé SFTP acceptée (« ssh-ed25519 SHA256:… ») ;
 * [keyInfo] : clé privée importée (type et empreinte), null sans clé.
 */
data class ServerConfig(
    val id: String, val protocol: String, val name: String, val host: String, val port: Int, val user: String,
    val startPath: String, val remember: Boolean, val hostKey: String?, val keyInfo: String?,
)

/** Secrets d'un serveur, en mémoire ; [wipe] les remet à zéro. */
class Secrets(var password: ByteArray?, var keyPassphrase: ByteArray?, var privateKey: ByteArray?) {
    fun wipe() {
        for (b in listOf(password, keyPassphrase, privateKey)) b?.fill(0)
    }
}

/**
 * Serveurs enregistrés (`remote-servers.json` sous [dir]) et leurs secrets, un fichier scellé par [box] et par serveur
 * (`remote-secrets/<id>`) : la clé privée importée, plus le mot de passe et la phrase de la clé si « Mémoriser » est
 * coché. Écritures par fichier temporaire puis renommage. Un fichier abîmé donne une liste vide.
 */
class RemoteServers(private val dir: File, private val box: SecretBox) {
    private val file = File(dir, "remote-servers.json")
    private val secretsDir = File(dir, "remote-secrets")

    @Synchronized
    fun list(): List<ServerConfig> {
        if (!file.isFile) return emptyList()
        return try {
            val a = JSONArray(file.readText())
            (0 until a.length()).mapNotNull { a.optJSONObject(it)?.let(::fromJson) }
        } catch (e: JSONException) {
            emptyList()
        }
    }

    fun get(id: String): ServerConfig? = list().firstOrNull { it.id == id }

    /**
     * Enregistre [c] (nouvel identifiant s'il est vide) ; rend la fiche telle qu'enregistrée. [password] et
     * [keyPassphrase] : null garde ceux déjà mémorisés ; ils ne sont écrits que si [ServerConfig.remember].
     * [IllegalArgumentException] : protocole, adresse ou port invalides.
     */
    @Synchronized
    fun save(c: ServerConfig, password: ByteArray?, keyPassphrase: ByteArray?): ServerConfig {
        require(c.protocol == "ftp" || c.protocol == "sftp") { "Protocole inconnu" }
        require(c.host.isNotBlank()) { "Adresse du serveur manquante" }
        require(c.port in 1..65535) { "Port invalide" }
        val s = c.copy(
            id = c.id.ifEmpty { UUID.randomUUID().toString() },
            name = c.name.trim().ifEmpty { c.host.trim() },
            host = c.host.trim(),
            startPath = PathRules.check(c.startPath) ?: "/",
        )
        val all = list().toMutableList()
        val i = all.indexOfFirst { it.id == s.id }
        if (i >= 0) all[i] = s else all += s
        write(all)
        val old = secrets(s.id)
        val sec = Secrets(
            if (s.remember) password ?: old?.password else null,
            if (s.remember) keyPassphrase ?: old?.keyPassphrase else null,
            old?.privateKey,
        )
        writeSecrets(s.id, sec)
        old?.wipe()
        return s
    }

    @Synchronized
    fun delete(id: String) {
        write(list().filter { it.id != id })
        File(secretsDir, id).delete()
    }

    @Synchronized
    fun setHostKey(id: String, hostKey: String?) = update(id) { it.copy(hostKey = hostKey) }

    /** Clé privée importée ([key] null : retirée) et sa description. */
    @Synchronized
    fun setPrivateKey(id: String, key: ByteArray?, info: String?) {
        update(id) { it.copy(keyInfo = info) }
        val old = secrets(id) ?: Secrets(null, null, null)
        writeSecrets(id, Secrets(old.password, old.keyPassphrase, key))
        old.privateKey?.fill(0)
    }

    /** Secrets enregistrés de [id], ou null s'il n'y en a pas (ou s'ils ne s'ouvrent plus). */
    @Synchronized
    fun secrets(id: String): Secrets? {
        val f = File(secretsDir, id)
        if (!f.isFile) return null
        return try {
            val plain = box.open(f.readBytes())
            val o = JSONObject(String(plain, Charsets.UTF_8))
            plain.fill(0)
            fun b(k: String) = o.optString(k).takeIf { it.isNotEmpty() }?.let { Base64.getDecoder().decode(it) }
            Secrets(b("password"), b("keyPassphrase"), b("privateKey"))
        } catch (e: Exception) {
            null
        }
    }

    private fun update(id: String, change: (ServerConfig) -> ServerConfig) {
        write(list().map { if (it.id == id) change(it) else it })
    }

    private fun writeSecrets(id: String, s: Secrets) {
        val f = File(secretsDir, id)
        if (s.password == null && s.keyPassphrase == null && s.privateKey == null) {
            f.delete()
            return
        }
        val o = JSONObject()
        fun put(k: String, v: ByteArray?) {
            if (v != null) o.put(k, Base64.getEncoder().encodeToString(v))
        }
        put("password", s.password); put("keyPassphrase", s.keyPassphrase); put("privateKey", s.privateKey)
        val plain = o.toString().toByteArray(Charsets.UTF_8)
        secretsDir.mkdirs()
        atomic(f, box.seal(plain))
        plain.fill(0)
    }

    private fun write(all: List<ServerConfig>) {
        dir.mkdirs()
        atomic(file, JSONArray().apply { all.forEach { put(toJson(it)) } }.toString().toByteArray(Charsets.UTF_8))
    }

    private fun atomic(f: File, bytes: ByteArray) {
        val tmp = File(f.parentFile, ".${f.name}.tmp")
        tmp.writeBytes(bytes)
        if (!tmp.renameTo(f)) {
            tmp.delete()
            throw java.io.IOException("Enregistrement impossible")
        }
    }

    private fun toJson(c: ServerConfig) = JSONObject()
        .put("id", c.id).put("protocol", c.protocol).put("name", c.name).put("host", c.host).put("port", c.port)
        .put("user", c.user).put("startPath", c.startPath).put("remember", c.remember)
        .put("hostKey", c.hostKey ?: JSONObject.NULL).put("keyInfo", c.keyInfo ?: JSONObject.NULL)

    private fun fromJson(o: JSONObject): ServerConfig? {
        val id = o.optString("id").ifEmpty { return null }
        return ServerConfig(
            id, o.optString("protocol"), o.optString("name"), o.optString("host"), o.optInt("port"), o.optString("user"),
            PathRules.check(o.optString("startPath")) ?: "/", o.optBoolean("remember"),
            o.optString("hostKey").takeIf { !o.isNull("hostKey") && it.isNotEmpty() },
            o.optString("keyInfo").takeIf { !o.isNull("keyInfo") && it.isNotEmpty() },
        )
    }
}

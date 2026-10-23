package com.mawena.fluluks.remote

import com.jcraft.jsch.JSch
import com.jcraft.jsch.KeyPair
import java.io.IOException
import java.security.MessageDigest
import java.util.Base64

/**
 * Clés SSH (phase 10) : clé privée importée pour un serveur SFTP (type, empreinte, chiffrée ou non) et empreintes
 * au format d'OpenSSH (« SHA256: » puis base64 sans `=`). Les octets d'une clé ne sortent jamais dans un message.
 */
object PrivateKeys {
    const val MSG_UNREADABLE = "Clé privée illisible"

    /** [type] : « ssh-ed25519 », « ecdsa-sha2-nistp256 », « ssh-rsa »… ; [fingerprint] : « SHA256:… », vide si illisible sans la phrase (ancien PEM chiffré). */
    data class Info(val type: String, val fingerprint: String, val encrypted: Boolean)

    private val SUPPORTED = setOf(
        "ssh-ed25519", "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384", "ecdsa-sha2-nistp521", "ssh-rsa",
    )

    /**
     * Clé privée OpenSSH (Ed25519, ECDSA, RSA ; ancien PEM accepté) lue sans sa phrase ; [key] n'est pas modifié.
     * [IOException] « Clé privée illisible » sinon.
     */
    fun inspect(key: ByteArray): Info {
        val copy = key.copyOf()
        try {
            val kp = try {
                KeyPair.load(jsch(), copy, null)
            } catch (e: Exception) {
                throw IOException(MSG_UNREADABLE)
            }
            try {
                val type = kp.keyTypeString
                if (type !in SUPPORTED) throw IOException(MSG_UNREADABLE)
                return Info(type, kp.publicKeyBlob?.let(::fingerprint).orEmpty(), kp.isEncrypted)
            } finally {
                kp.dispose()
            }
        } finally {
            copy.fill(0)
        }
    }

    /** Texte rangé dans [ServerConfig.keyInfo] : « <type> <empreinte> ». */
    fun describe(info: Info): String = "${info.type} ${info.fingerprint}".trim()

    /** « SHA256:… » d'un blob de clé publique SSH. */
    internal fun fingerprint(blob: ByteArray): String =
        "SHA256:" + Base64.getEncoder().withoutPadding().encodeToString(MessageDigest.getInstance("SHA-256").digest(blob))

    /** « <type> SHA256:… » d'une clé publique de serveur, le type lu dans le blob. */
    internal fun hostKey(blob: ByteArray): String = "${blobType(blob)} ${fingerprint(blob)}"

    private fun blobType(blob: ByteArray): String {
        if (blob.size < 4) return "inconnu"
        val n = ((blob[0].toInt() and 0xff) shl 24) or ((blob[1].toInt() and 0xff) shl 16) or
            ((blob[2].toInt() and 0xff) shl 8) or (blob[3].toInt() and 0xff)
        if (n <= 0 || n > 64 || 4 + n > blob.size) return "inconnu"
        return String(blob, 4, n, Charsets.US_ASCII)
    }

    private val bouncyCastle by lazy {
        // Ed25519, Curve25519 et ML-KEM par Bouncy Castle partout : sur Android le JDK ne les fournit pas, et les tests
        // JVM passent ainsi par le même chemin que le téléphone.
        JSch.setConfig("xdh", "com.jcraft.jsch.bc.XDH")
        JSch.setConfig("keypairgen.eddsa", "com.jcraft.jsch.bc.KeyPairGenEdDSA")
        JSch.setConfig("keypairgen_fromprivate.eddsa", "com.jcraft.jsch.bc.KeyPairGenEdDSA")
        JSch.setConfig("ssh-ed25519", "com.jcraft.jsch.bc.SignatureEd25519")
        JSch.setConfig("ssh-ed448", "com.jcraft.jsch.bc.SignatureEd448")
        JSch.setConfig("mlkem768", "com.jcraft.jsch.bc.MLKEM768")
        JSch.setConfig("mlkem1024", "com.jcraft.jsch.bc.MLKEM1024")
    }

    /** Une instance de JSch, réglée pour Bouncy Castle. */
    internal fun jsch(): JSch {
        bouncyCastle
        return JSch()
    }
}

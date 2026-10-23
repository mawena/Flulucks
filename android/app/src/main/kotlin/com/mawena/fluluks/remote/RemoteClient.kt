package com.mawena.fluluks.remote

import com.mawena.fluluks.files.Entry
import java.io.Closeable
import java.io.InputStream
import java.io.OutputStream

/**
 * Un protocole de dossiers distants (FTP, SFTP), connecté à un serveur. Chemins absolus et normalisés ; les noms
 * rendus par [list] sont filtrés par [RemoteSource]. Les erreurs sortent en [java.io.IOException] ou
 * [java.io.FileNotFoundException] avec un message français, sans jamais contenir de secret.
 */
interface RemoteClient : Closeable {
    fun list(path: String): List<Entry>
    fun stat(path: String): Entry?

    /** Flux du fichier [path] à partir de [offset] ; sa fermeture rend la connexion. */
    fun openReadAt(path: String, offset: Long): InputStream

    /** Crée (ou remplace) le fichier [name] sous [parent] ; l'envoi se termine à la fermeture du flux. */
    fun create(parent: String, name: String): OutputStream
    fun mkdir(parent: String, name: String)
    fun rename(path: String, newPath: String)

    /** Supprime un fichier, un lien, ou un dossier vide ([dir]). */
    fun delete(path: String, dir: Boolean)
}

/**
 * Échec d'un dossier distant au message français écrit pour l'utilisateur (« Serveur injoignable », « Connexion
 * perdue », « Serveur occupé… ») : montré tel quel, jamais remplacé par « L'opération a échoué ». Ne contient pas de
 * secret.
 */
class RemoteIOException(message: String, cause: Throwable? = null) : java.io.IOException(message, cause)

/** Clé du serveur SFTP jamais vue : [fingerprint] (« ssh-ed25519 SHA256:… ») à montrer avant de lui faire confiance. */
class HostKeyUnknown(val fingerprint: String) : Exception("Clé du serveur inconnue")

/** Clé du serveur différente de celle acceptée : refusée (attaque possible, ou serveur réinstallé). */
class HostKeyChanged(val old: String, val new: String) : Exception(
    "La clé du serveur a changé : connexion refusée. Si le serveur a été réinstallé, oubliez l'ancienne clé dans sa fiche.",
)

/** Identifiants manquants ou refusés ; [missing] : aucun mot de passe connu, à demander. */
class AuthFailed(val missing: Boolean) : Exception(if (missing) "Mot de passe requis" else "Identifiant ou mot de passe refusé")

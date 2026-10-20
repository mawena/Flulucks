package com.mawena.fluluks.files

import android.net.Uri
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.DocumentIds
import java.io.FileNotFoundException
import java.io.InputStream
import java.io.OutputStream

/** Élément d'un dossier, quelle que soit la source. `path` absolu dans la source, `mtime` en secondes UTC. */
data class Entry(val name: String, val path: String, val type: FileType, val size: Long, val mtime: Long)

/** Dossier dont Android refuse la lecture (Android/data, Android/obb…). */
class ProtectedDirException : FileNotFoundException(Files.MSG_PROTECTED)

/** Chemins venus de Dart : mêmes règles que les identifiants de documents du provider. */
object PathRules {
    /** Le chemin s'il est absolu et normalisé (pas de « . », « .. », « // », « / » final, NUL), sinon null. */
    fun check(path: String): String? = DocumentIds.decode("x:$path")?.second

    fun require(path: String): String = check(path) ?: throw FileNotFoundException(Files.MSG_BAD_PATH)
}

/**
 * Un stockage parcouru par le gestionnaire de fichiers : stockage du téléphone, carte SD ou volume d'un
 * disque USB. Les erreurs sortent en [FileNotFoundException] avec un message français, ou en
 * [com.mawena.fluluks.engine.EngineError] pour un disque.
 */
interface FileSource {
    /** Identifiant du moment (`phone`, `sd:<uuid>`, identifiant du volume). */
    val id: String

    /** Clé qui survit aux sessions (favoris) : `phone`, `sd:<uuid>`, `disk:<uuid de la partition>`. */
    val stableKey: String
    val label: String

    /** Raison de la lecture seule, ou null si l'écriture est permise. */
    val readOnlyReason: String?

    /** Noms insensibles à la casse (FAT, NTFS, stockage du téléphone). */
    val caseInsensitive: Boolean

    fun list(path: String): List<Entry>
    fun stat(path: String): Entry?

    /** (octets libres, octets au total). */
    fun space(): Pair<Long, Long>

    /** Crée le dossier [name] sous [parent] (« nom (1) » si le nom est pris) ; rend son chemin. */
    fun mkdir(parent: String, name: String): String

    /** Renomme dans le même dossier ; rend le nouveau chemin. */
    fun rename(path: String, newName: String): String

    /** Supprime un fichier, ou un dossier et tout son contenu. */
    fun delete(path: String)

    /** Déplace sous [targetParent], dans la même source (nom unique en cas de conflit) ; rend le nouveau chemin. */
    fun move(path: String, targetParent: String): String

    fun openRead(path: String): InputStream

    /** Crée le fichier [name] sous [parent] (nom unique) ; rend son chemin et un flux d'écriture à fermer. */
    fun createFile(parent: String, name: String): Pair<String, OutputStream>

    /** URI de contenu lisible par une autre application (Ouvrir avec, Partager). */
    fun contentUri(path: String): Uri
}

internal fun FileType.isDir() = this == FileType.DIR

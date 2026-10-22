package com.mawena.fluluks.provider

import com.mawena.fluluks.engine.EngineError

/** Codes du moteur → errno et messages français rendus aux autres applications (sans API Android : testable sur la JVM). */
object ProviderErrors {
    /** errno rendu par onWrite / onFsync ; le provider le traduit en OsConstants. */
    enum class Errno { ENOSPC, EROFS, ENXIO, EFBIG, EIO }

    /** Opérations dont un FL_EINVAL vient d'un nom refusé par le système de fichiers (FAT : `: * ? " < > | \`…). */
    private val NAMING_OPS = setOf("createDocument", "renameDocument", "moveDocument", "copyDocument")

    const val VOLUME_CLOSED = "Volume fermé"
    const val READ_ONLY = "Volume en lecture seule"

    /** « Volume en lecture seule : <raison> » (ou sans raison), pour une écriture refusée. */
    fun readOnly(reason: String): String = if (reason.isBlank()) READ_ONLY else "$READ_ONLY : $reason"

    fun errno(code: Int): Errno = when (code) {
        EngineError.FL_ENOSPC -> Errno.ENOSPC
        EngineError.FL_EROFS -> Errno.EROFS
        EngineError.FL_ENODEV -> Errno.ENXIO
        EngineError.FL_EFBIG -> Errno.EFBIG
        else -> Errno.EIO
    }

    /** Message de l'erreur `code` pendant l'opération `op` ; null pour FL_EROFS (raison propre au volume). */
    fun message(code: Int, op: String): String? = when (code) {
        EngineError.FL_ENODEV -> VOLUME_CLOSED
        EngineError.FL_ENOSPC -> "Disque plein"
        EngineError.FL_EROFS -> null
        EngineError.FL_EEXIST -> "Un élément porte déjà ce nom"
        EngineError.FL_EBUSY -> "Fichier en cours d'utilisation"
        EngineError.FL_EFBIG -> "Fichier trop gros pour ce disque (4 Gio au plus en FAT)"
        // FL_ENOTSUP sur un fichier : NTFS compressé ou chiffré (EFS), ou ext4 sans extents (ext3) ; FAT n'en a pas
        EngineError.FL_ENOTSUP -> "Fichier non modifiable ici : compressé ou chiffré par Windows, ou à l'ancien format ext3"
        EngineError.FL_EINVAL -> if (op in NAMING_OPS) "Nom non autorisé sur ce disque" else "Document introuvable ou illisible"
        else -> "Document introuvable ou illisible"
    }
}

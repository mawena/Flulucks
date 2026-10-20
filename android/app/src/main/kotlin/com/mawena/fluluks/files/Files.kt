package com.mawena.fluluks.files

import android.content.Context

/** Opérations du gestionnaire de fichiers sur toutes les sources (complétées en T3). */
object Files {
    const val FILE_AUTHORITY = "com.mawena.fluluks.fichiers"
    const val MSG_BAD_PATH = "Document introuvable ou illisible"
    const val MSG_PROTECTED = "Dossier protégé par Android"
    const val MSG_NO_ACCESS = "Accès aux fichiers du téléphone non autorisé"
    const val MSG_INTO_ITSELF = "Un dossier ne peut pas être copié dans lui-même"

    /** Contexte de l'application (URI du FileProvider) ; posé par [Sources.init]. */
    @Volatile var context: Context? = null
}

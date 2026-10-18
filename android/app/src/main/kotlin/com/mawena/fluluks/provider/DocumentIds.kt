package com.mawena.fluluks.provider

/**
 * Identifiants de documents : "<volumeId>:<chemin absolu>". Un identifiant vient d'une autre
 * application, donc hostile : [decode] rejette tout ce qui n'est pas un chemin normalisé.
 */
object DocumentIds {
    /** Taille maximale d'un chemin, en octets UTF-8 (PATH_MAX - 1). */
    const val MAX_PATH_BYTES = 4095

    fun encode(volumeId: String, path: String): String = "$volumeId:$path"

    /**
     * (volumeId, chemin) ou null si l'identifiant est mal formé : pas de « : », volume vide, chemin non
     * absolu, composant vide, « . » ou « .. », « / » final (sauf la racine), octet NUL, chemin trop long.
     * Seul le premier « : » sépare (un nom de fichier peut en contenir).
     */
    fun decode(id: String): Pair<String, String>? {
        val sep = id.indexOf(':')
        if (sep <= 0) return null
        val volume = id.substring(0, sep)
        val path = id.substring(sep + 1)
        if (volume.indexOf('\u0000') >= 0 || path.indexOf('\u0000') >= 0) return null
        if (!path.startsWith("/")) return null
        if (path.length > MAX_PATH_BYTES) return null // borne rapide avant l'encodage
        if (path.toByteArray(Charsets.UTF_8).size > MAX_PATH_BYTES) return null
        if (path == "/") return volume to path
        for (part in path.substring(1).split('/')) {
            if (part.isEmpty() || part == "." || part == "..") return null // couvre aussi « / » final et « // »
        }
        return volume to path
    }

    /** Vrai si [path] est strictement sous [parent], en comparant des composants entiers. */
    fun isDescendant(parent: String, path: String): Boolean {
        if (parent == path) return false
        if (parent == "/") return path.startsWith("/")
        return path.startsWith("$parent/")
    }

    /** Chemin de l'enfant [name] de [parent]. */
    fun child(parent: String, name: String): String = if (parent == "/") "/$name" else "$parent/$name"
}

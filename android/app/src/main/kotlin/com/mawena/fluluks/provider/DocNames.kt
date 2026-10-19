package com.mawena.fluluks.provider

/** Noms de documents créés ou renommés par une autre application (donc hostiles). */
object DocNames {
    /** Taille maximale d'un nom ext4, en octets UTF-8 (sur FAT, 255 unités UTF-16 : jamais atteintes avant). */
    const val MAX_NAME_BYTES = 255

    /** Nombre maximal de variantes « nom (n) » essayées en cas de conflit. */
    const val MAX_TRIES = 999

    /** Faux si le nom est vide, « . », « .. », contient « / » ou NUL, ou dépasse 255 octets UTF-8. */
    fun isValid(name: String): Boolean =
        name.isNotEmpty() && name != "." && name != ".." && name.indexOf('/') < 0 && name.indexOf('\u0000') < 0 &&
            name.toByteArray(Charsets.UTF_8).size <= MAX_NAME_BYTES

    /**
     * Variante n° [n] de [name], comme Android : « a.txt » → « a (1).txt », « a » → « a (1) »,
     * « .bashrc » → « .bashrc (1) », « a.tar.gz » → « a.tar (1).gz » (extension = après le dernier point).
     */
    fun numbered(name: String, n: Int): String {
        val dot = name.lastIndexOf('.')
        return if (dot <= 0) "$name ($n)" else name.substring(0, dot) + " ($n)" + name.substring(dot)
    }

    /** Premier nom libre parmi [name], « nom (1) »… « nom (999) » selon [exists] ; null si aucun. */
    fun unique(name: String, exists: (String) -> Boolean): String? {
        if (!exists(name)) return name
        for (n in 1..MAX_TRIES) {
            val c = numbered(name, n)
            if (isValid(c) && !exists(c)) return c
        }
        return null
    }
}

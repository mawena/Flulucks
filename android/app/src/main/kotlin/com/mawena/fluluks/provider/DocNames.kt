package com.mawena.fluluks.provider

/** Noms de documents créés ou renommés par une autre application (donc hostiles). */
object DocNames {
    /** Taille maximale d'un nom ext4, en octets UTF-8 (sur FAT et NTFS, 255 unités UTF-16 : jamais atteintes avant). */
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

    /**
     * Premier nom libre parmi [name], « nom (1) »… « nom (999) » selon [exists] ; null si aucun. Une variante
     * qui dépasserait 255 octets est raccourcie (extension gardée, sans couper un caractère).
     */
    fun unique(name: String, exists: (String) -> Boolean): String? {
        if (!exists(name)) return name
        for (n in 1..MAX_TRIES) {
            val c = fitted(name, n)
            if (isValid(c) && !exists(c)) return c
        }
        return null
    }

    /** [numbered], raccourci au besoin pour tenir en [MAX_NAME_BYTES] octets. */
    private fun fitted(name: String, n: Int): String {
        val c = numbered(name, n)
        if (c.toByteArray(Charsets.UTF_8).size <= MAX_NAME_BYTES) return c
        val dot = name.lastIndexOf('.')
        val ext = if (dot > 0 && name.length - dot <= 16) name.substring(dot) else ""
        val suffix = " ($n)$ext"
        return truncateUtf8(name.substring(0, name.length - ext.length), MAX_NAME_BYTES - suffix.toByteArray(Charsets.UTF_8).size) + suffix
    }

    /** Caractères refusés par FAT et NTFS (en plus des caractères de contrôle). */
    private const val FORBIDDEN = "/\\:*?\"<>|"

    /** Nom de repli quand il ne reste rien. */
    const val FALLBACK = "fichier"

    /**
     * Nom acceptable sur FAT, NTFS et ext4 : `/ \ : * ? " < > |` et caractères de contrôle remplacés par
     * « _ », point et espaces finaux retirés, 255 octets UTF-8 au plus en gardant l'extension, [FALLBACK]
     * s'il ne reste rien. Idempotent ; un nom déjà propre est rendu tel quel.
     */
    fun sanitize(name: String): String {
        val sb = StringBuilder(name.length)
        for (c in name) sb.append(if (c < ' ' || c == '\u007F' || FORBIDDEN.indexOf(c) >= 0) '_' else c)
        var n = sb.toString().trimEnd('.', ' ')
        if (n.isEmpty() || n == "." || n == "..") return FALLBACK
        if (n.toByteArray(Charsets.UTF_8).size > MAX_NAME_BYTES) {
            val dot = n.lastIndexOf('.')
            val ext = if (dot > 0 && n.length - dot <= 16) n.substring(dot) else ""
            val base = truncateUtf8(n.substring(0, n.length - ext.length), MAX_NAME_BYTES - ext.toByteArray(Charsets.UTF_8).size)
            n = (base.trimEnd('.', ' ') + ext).trimEnd('.', ' ')
            if (n.isEmpty()) return FALLBACK
        }
        return n
    }

    /** Préfixe de [s] d'au plus [max] octets UTF-8, sans couper un caractère (ni une paire de substitution). */
    fun truncateUtf8(s: String, max: Int): String {
        var bytes = 0
        var i = 0
        while (i < s.length) {
            val cp = s.codePointAt(i)
            val len = String(Character.toChars(cp)).toByteArray(Charsets.UTF_8).size
            if (bytes + len > max) break
            bytes += len
            i += Character.charCount(cp)
        }
        return s.substring(0, i)
    }
}

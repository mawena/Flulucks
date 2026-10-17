package com.mawena.fluluks.provider

import com.mawena.fluluks.engine.PartitionInfo

/** Résumé d'une racine du provider, selon la partition du volume. */
internal object RootSummary {
    const val PLAIN = "Lecture seule"

    /** « Chiffré (LUKS1) — lecture seule » ou « Chiffré (LUKS2) — … » selon la version réelle ; en clair : PLAIN. */
    fun of(part: PartitionInfo?): String {
        if (part == null || !part.encrypted) return PLAIN
        val version = part.luks?.version ?: part.content.removePrefix("LUKS").toIntOrNull()
        return if (version == 1 || version == 2) "Chiffré (LUKS$version) — lecture seule" else "Chiffré (LUKS) — lecture seule"
    }
}

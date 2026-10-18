package com.mawena.fluluks.provider

import com.mawena.fluluks.engine.PartitionInfo

/** Résumé d'une racine du provider, selon la partition et le mode du volume. */
internal object RootSummary {
    const val PLAIN_RW = "Lecture et écriture"

    /**
     * Volume en lecture seule : « Lecture seule : <raison> ». En écriture : « Chiffré (LUKS2) — lecture et
     * écriture » (version réelle) ou, en clair, [PLAIN_RW].
     */
    fun of(part: PartitionInfo?, readOnly: Boolean, roReason: String): String {
        if (readOnly) return if (roReason.isBlank()) "Lecture seule" else "Lecture seule : $roReason"
        if (part == null || !part.encrypted) return PLAIN_RW
        val version = part.luks?.version ?: part.content.removePrefix("LUKS").toIntOrNull()
        val luks = if (version == 1 || version == 2) "LUKS$version" else "LUKS"
        return "Chiffré ($luks) — lecture et écriture"
    }
}

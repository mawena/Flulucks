package com.mawena.fluluks

import com.mawena.fluluks.engine.LuksInfo
import com.mawena.fluluks.engine.PartitionInfo
import com.mawena.fluluks.provider.RootSummary
import org.junit.Assert.assertEquals
import org.junit.Test

class RootSummaryTest {
    private fun part(content: String, luksVersion: Int?) = PartitionInfo(
        index = 1, firstLba = 2048, sizeBytes = 1_000_000, content = content, label = "", uuid = "u",
        supported = true, luks = luksVersion?.let { LuksInfo(it, "aes-xts-plain64", false, 1024) }, error = null,
    )

    @Test fun luksVersionIsTheRealOne() {
        assertEquals("Chiffré (LUKS1) — lecture et écriture", RootSummary.of(part("LUKS1", 1), false, ""))
        assertEquals("Chiffré (LUKS2) — lecture et écriture", RootSummary.of(part("LUKS2", 2), false, ""))
        assertEquals("Chiffré (LUKS1) — lecture et écriture", RootSummary.of(part("LUKS1", null), false, "")) // en-tête non détaillé
    }

    @Test fun plainWritable() {
        assertEquals("Lecture et écriture", RootSummary.of(part("ext4", null), false, ""))
        assertEquals("Lecture et écriture", RootSummary.of(null, false, ""))
    }

    @Test fun readOnlyGivesTheReason() {
        val r = "journal à rejouer : branchez le disque sous Ubuntu"
        assertEquals("Lecture seule : $r", RootSummary.of(part("LUKS2", 2), true, r))
        assertEquals("Lecture seule : $r", RootSummary.of(null, true, r))
        assertEquals("Lecture seule", RootSummary.of(null, true, ""))
    }
}

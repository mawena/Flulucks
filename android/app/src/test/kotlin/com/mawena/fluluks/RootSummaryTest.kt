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
        assertEquals("Chiffré (LUKS1) — lecture seule", RootSummary.of(part("LUKS1", 1)))
        assertEquals("Chiffré (LUKS2) — lecture seule", RootSummary.of(part("LUKS2", 2)))
        assertEquals("Chiffré (LUKS1) — lecture seule", RootSummary.of(part("LUKS1", null))) // en-tête non détaillé
    }

    @Test fun plainUnchanged() {
        assertEquals("Lecture seule", RootSummary.of(part("ext4", null)))
        assertEquals("Lecture seule", RootSummary.of(null))
    }
}

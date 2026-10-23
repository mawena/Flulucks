package com.mawena.fluluks

import com.mawena.fluluks.media.TimeText.clamp
import com.mawena.fluluks.media.TimeText.clock
import com.mawena.fluluks.media.TimeText.delta
import com.mawena.fluluks.media.TimeText.preview
import com.mawena.fluluks.media.TimeText.seconds
import org.junit.Assert.assertEquals
import org.junit.Test

/** Durées affichées par les gestes du lecteur vidéo (phase 12). */
class TimeTextTest {
    @Test fun clocks() {
        assertEquals("0:05", clock(5_400))
        assertEquals("12:34", clock(754_000))
        assertEquals("1:02:03", clock(3_723_000))
        assertEquals("0:00", clock(-1))
    }

    @Test fun deltas() {
        assertEquals("+0:25", delta(25_000))
        assertEquals("−1:05", delta(-65_000))
        assertEquals("+0:00", delta(0))
        assertEquals("+20 s", seconds(20_000))
        assertEquals("−30 s", seconds(-30_000))
    }

    @Test fun previews() {
        assertEquals("12:34 / 1:30:00\n+0:25", preview(754_000, 25_000, 5_400_000))
        assertEquals("12:34\n−0:05", preview(754_000, -5_000, -1))
    }

    @Test fun clamps() {
        assertEquals(0L, clamp(-5_000, 600_000))
        assertEquals(600_000L, clamp(610_000, 600_000))
        assertEquals(610_000L, clamp(610_000, -1)) // durée inconnue : seulement ≥ 0
        assertEquals(42L, clamp(42, 600_000))
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.media.SeekGestures
import com.mawena.fluluks.media.SeekGestures.Effect.DragEnd
import com.mawena.fluluks.media.SeekGestures.Effect.Preview
import com.mawena.fluluks.media.SeekGestures.Playback
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Gestes du lecteur vidéo (phase 12, spec § 20) : densité 2 (1 dp = 2 px), seuil 16 px, 150 ms par dp, écran de
 * 1 000 px de large, lecture à 1:00 sur 10:00.
 */
class SeekGesturesTest {
    private val g = SeekGestures(density = 2f, touchSlop = 16f, doubleTapSlop = 200f)
    private val pb = Playback(60_000, 600_000, true)
    private val w = 1000

    // --- glissement ---

    @Test fun dragRightPreviewsThenSeeksOnRelease() {
        assertTrue(g.down(100f, 500f, 0, w, false, pb).dispatch)
        val start = g.move(140f, 502f) // au-delà du seuil : le geste est pris au lecteur
        assertFalse(start.dispatch)
        assertTrue(start.cancelChildren)
        assertEquals(Preview(63_000, 3_000), start.effect) // 40 px = 20 dp = 3 s
        val next = g.move(300f, 510f)
        assertFalse(next.cancelChildren)
        assertEquals(Preview(75_000, 15_000), next.effect) // 200 px = 100 dp = 15 s
        val end = g.up(300f, 510f, 400)
        assertFalse(end.dispatch)
        assertEquals(DragEnd(75_000, 15_000), end.effect)
    }

    @Test fun dragSeeksOnlyOnRelease() {
        g.down(100f, 500f, 0, w, false, pb)
        val effects = listOf(140f, 200f, 260f, 300f).map { g.move(it, 500f).effect }
        assertTrue(effects.all { it is Preview })
        assertEquals(DragEnd(75_000, 15_000), g.up(300f, 500f, 500).effect)
        // le geste suivant repart de zéro
        assertNull(g.move(400f, 500f).effect)
    }

    @Test fun dragLeftRewinds() {
        g.down(300f, 500f, 0, w, false, pb)
        assertEquals(Preview(45_000, -15_000), g.move(100f, 500f).effect)
    }

    @Test fun dragClampsToDuration() {
        g.down(100f, 500f, 0, w, false, Playback(595_000, 600_000, true))
        assertEquals(Preview(600_000, 5_000), g.move(300f, 500f).effect)
        g.up(300f, 500f, 300)
        g.down(300f, 500f, 1_000, w, false, Playback(2_000, 600_000, true))
        assertEquals(Preview(0, -2_000), g.move(100f, 500f).effect)
    }

    @Test fun dragUnknownDurationOnlyClampsAtZero() {
        g.down(100f, 500f, 0, w, false, Playback(60_000, -1, true))
        assertEquals(Preview(75_000, 15_000), g.move(300f, 500f).effect)
    }

    @Test fun verticalMoveIsNotADrag() {
        g.down(100f, 500f, 0, w, false, pb)
        val m = g.move(102f, 600f)
        assertTrue(m.dispatch)
        assertNull(m.effect)
        assertNull(g.move(400f, 600f).effect) // plus jamais un glissement dans ce geste
        val up = g.up(400f, 600f, 300)
        assertTrue(up.dispatch)
        assertNull(up.effect)
    }

    @Test fun dragNeverStartsOnControl() {
        assertTrue(g.down(100f, 900f, 0, w, true, pb).dispatch)
        val m = g.move(400f, 900f)
        assertTrue(m.dispatch)
        assertNull(m.effect)
        assertTrue(g.up(400f, 900f, 300).dispatch)
    }

    @Test fun notSeekableNeverDrags() {
        g.down(100f, 500f, 0, w, false, Playback(0, -1, false))
        val m = g.move(400f, 500f)
        assertTrue(m.dispatch)
        assertNull(m.effect)
        assertNull(g.up(400f, 500f, 300).effect)
    }

    @Test fun cancelEndsDragWithoutSeek() {
        g.down(100f, 500f, 0, w, false, pb)
        g.move(300f, 500f)
        val c = g.cancel()
        assertTrue(c.dispatch)
        assertEquals(DragEnd(60_000, 0), c.effect)
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.media.SeekGestures
import com.mawena.fluluks.media.SeekGestures.Effect.DragEnd
import com.mawena.fluluks.media.SeekGestures.Effect.Jump
import com.mawena.fluluks.media.SeekGestures.Outcome
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

    // --- toucher deux fois, série ---

    /** Toucher simple de 80 ms à [x] ; rend la réponse du lever. */
    private fun tap(x: Float, t: Long, onControl: Boolean = false, p: Playback = pb): Pair<Outcome, Outcome> =
        g.down(x, 500f, t, w, onControl, p) to g.up(x, 500f, t + 80)

    @Test fun doubleTapRightJumpsForward() {
        val (d1, u1) = tap(800f, 0)
        assertTrue(d1.dispatch)
        assertTrue(u1.dispatch)
        assertNull(u1.effect)
        assertFalse(g.down(805f, 505f, 250, w, false, pb).dispatch)
        assertEquals(Outcome(false, effect = Jump(10_000, 10_000, true)), g.up(805f, 505f, 320))
    }

    @Test fun doubleTapLeftJumpsBack() {
        tap(200f, 0)
        g.down(210f, 500f, 250, w, false, pb)
        assertEquals(Jump(-10_000, -10_000, true), g.up(210f, 500f, 320).effect)
    }

    @Test fun streakAddsTenSecondsPerTap() {
        tap(800f, 0)
        tap(800f, 240) // up à 320 : +10 s
        assertFalse(g.down(820f, 500f, 600, w, false, pb).dispatch)
        assertEquals(Outcome(false, effect = Jump(10_000, 20_000, false)), g.up(820f, 500f, 680))
        val (d4, u4) = tap(700f, 1_000) // 320 ms après le précédent
        assertFalse(d4.dispatch)
        assertEquals(Jump(10_000, 30_000, false), u4.effect)
    }

    @Test fun streakChangesSide() {
        tap(800f, 0)
        tap(800f, 240)
        assertEquals(Jump(-10_000, -10_000, false), tap(300f, 600).second.effect)
        assertEquals(Jump(-10_000, -20_000, false), tap(300f, 900).second.effect)
    }

    @Test fun streakEndsAfterDelay() {
        tap(800f, 0)
        tap(800f, 240) // dernier lever à 320
        val (d, u) = tap(800f, 1_100) // 780 ms plus tard
        assertTrue(d.dispatch)
        assertNull(u.effect)
    }

    @Test fun streakTapOnControlPasses() {
        tap(800f, 0)
        tap(800f, 240)
        val (d, u) = tap(500f, 600, onControl = true)
        assertTrue(d.dispatch)
        assertNull(u.effect)
    }

    @Test fun slowSecondTapIsNotADoubleTap() {
        tap(800f, 0) // lever à 80
        val (d, u) = tap(800f, 430)
        assertTrue(d.dispatch)
        assertNull(u.effect)
    }

    @Test fun farSecondTapIsNotADoubleTap() {
        tap(800f, 0)
        val (d, u) = tap(500f, 200) // 300 px > 200 px
        assertTrue(d.dispatch)
        assertNull(u.effect)
    }

    @Test fun longPressIsNotATap() {
        g.down(800f, 500f, 0, w, false, pb)
        g.up(800f, 500f, 600)
        val (d, u) = tap(800f, 700)
        assertTrue(d.dispatch)
        assertNull(u.effect)
    }

    @Test fun doubleTapOnPlayButtonIsNotAJump() {
        val (d1, u1) = tap(500f, 0, onControl = true)
        val (d2, u2) = tap(500f, 200, onControl = true)
        assertTrue(d1.dispatch && u1.dispatch && d2.dispatch && u2.dispatch)
        assertNull(u2.effect)
    }

    /** Le premier toucher a montré les commandes : le second, même sur un bouton apparu, est un double toucher. */
    @Test fun doubleTapRestoresControls() {
        tap(800f, 0)
        val (d, u) = tap(800f, 200, onControl = true)
        assertFalse(d.dispatch)
        assertEquals(Jump(10_000, 10_000, true), u.effect)
    }

    @Test fun secondTapTurnedIntoDragIsADrag() {
        tap(100f, 0)
        assertFalse(g.down(100f, 500f, 200, w, false, pb).dispatch)
        val m = g.move(300f, 500f)
        assertFalse(m.dispatch)
        assertFalse(m.cancelChildren) // le lecteur n'a pas reçu ce toucher
        assertEquals(Preview(75_000, 15_000), m.effect)
        assertEquals(DragEnd(75_000, 15_000), g.up(300f, 500f, 400).effect)
    }

    @Test fun notSeekableDoubleTapPasses() {
        val p = Playback(0, -1, false)
        tap(800f, 0, p = p)
        val (d, u) = tap(800f, 200, p = p)
        assertTrue(d.dispatch)
        assertNull(u.effect)
    }
}

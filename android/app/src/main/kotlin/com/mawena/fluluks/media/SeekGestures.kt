package com.mawena.fluluks.media

import kotlin.math.abs
import kotlin.math.roundToLong

/**
 * Gestes du lecteur vidéo, comme VLC (phase 12, spec § 20), sans vue : glisser horizontalement avance ou recule
 * ([msPerDp] par dp parcouru, saut au lever du doigt) ; toucher deux fois la moitié gauche recule de [stepMs], la
 * moitié droite avance d'autant, et chaque toucher suivant arrivé moins de [streakMs] après le précédent ajoute
 * [stepMs] (série). Le premier toucher d'un double passe au lecteur (il montre ou cache les commandes) ; les suivants
 * sont retenus. Aucun geste ne part d'une commande affichée, sauf le second toucher d'un double. L'activité donne
 * chaque événement tactile ; la réponse dit s'il faut le passer au lecteur ([Outcome.dispatch]), lui retirer le geste
 * en cours ([Outcome.cancelChildren]) et quoi afficher ou faire ([Outcome.effect]). Coordonnées et seuils en pixels.
 */
class SeekGestures(
    private val density: Float,
    private val touchSlop: Float,
    private val doubleTapSlop: Float,
    private val doubleTapTimeoutMs: Long = 300,
    private val streakMs: Long = 700,
    private val tapMaxMs: Long = 500,
    private val stepMs: Long = 10_000,
    private val msPerDp: Long = 150,
) {
    /** État de la lecture au début d'un geste ; [durationMs] ≤ 0 si inconnue. */
    data class Playback(val positionMs: Long, val durationMs: Long, val seekable: Boolean)

    sealed interface Effect {
        /** Glissement en cours : position visée et écart. */
        data class Preview(val targetMs: Long, val deltaMs: Long) : Effect

        /** Fin du glissement : sauter à [targetMs] si [deltaMs] n'est pas nul, et cacher l'aperçu. */
        data class DragEnd(val targetMs: Long, val deltaMs: Long) : Effect

        /** Toucher qui saute de [stepMs] ; [totalMs] cumulé de la série ; [restoreControls] : second toucher du double. */
        data class Jump(val stepMs: Long, val totalMs: Long, val restoreControls: Boolean) : Effect
    }

    data class Outcome(val dispatch: Boolean, val cancelChildren: Boolean = false, val effect: Effect? = null)

    /**
     * `PASS` : geste laissé au lecteur ; `PRESSED` : toucher passé au lecteur, qui peut devenir un toucher ou un
     * glissement ; `HELD` : toucher retenu (second d'un double, ou de la série) ; `DRAGGING` : glissement.
     */
    private enum class Phase { IDLE, PASS, PRESSED, HELD, DRAGGING }

    private var phase = Phase.IDLE
    private var downX = 0f
    private var downY = 0f
    private var playback = Playback(0, -1, false)
    private var target = 0L
    private var downTime = 0L
    private var moved = false
    private var forward = true

    /** Premier toucher d'un double possible : lieu et heure du lever (null s'il n'y en a pas). */
    private var candidate: Triple<Float, Float, Long>? = null

    /** Série en cours : sens du cumul (0 : aucune), cumul, heure du dernier lever. */
    private var streakSide = 0
    private var streakTotal = 0L
    private var streakUp = 0L
    private var held: Boolean? = null // vrai : second toucher d'un double ; faux : toucher de série

    fun down(x: Float, y: Float, timeMs: Long, width: Int, onControl: Boolean, playback: Playback): Outcome {
        downX = x
        downY = y
        downTime = timeMs
        moved = false
        forward = x >= width / 2f
        this.playback = playback
        val c = candidate
        candidate = null
        val inStreak = streakSide != 0 && timeMs - streakUp <= streakMs
        held = when {
            !playback.seekable -> null
            inStreak && !onControl -> false
            !inStreak && c != null && timeMs - c.third <= doubleTapTimeoutMs &&
                abs(x - c.first) <= doubleTapSlop && abs(y - c.second) <= doubleTapSlop -> true
            else -> null
        }
        if (held == null) streakSide = 0
        phase = when {
            held != null -> Phase.HELD
            onControl || !playback.seekable -> Phase.PASS
            else -> Phase.PRESSED
        }
        return if (phase == Phase.HELD) HOLD else PASS
    }

    fun move(x: Float, y: Float): Outcome = when (phase) {
        Phase.IDLE, Phase.PASS -> PASS
        Phase.DRAGGING -> Outcome(false, effect = preview(x))
        Phase.PRESSED, Phase.HELD -> {
            val pressed = phase == Phase.PRESSED
            val dx = abs(x - downX)
            val dy = abs(y - downY)
            when {
                moved -> if (pressed) PASS else HOLD
                dx > touchSlop && dx > dy -> {
                    phase = Phase.DRAGGING
                    streakSide = 0
                    Outcome(false, cancelChildren = pressed, effect = preview(x))
                }
                dx > touchSlop || dy > touchSlop -> {
                    moved = true
                    if (pressed) { phase = Phase.PASS; PASS } else HOLD
                }
                else -> if (pressed) PASS else HOLD
            }
        }
    }

    fun up(x: Float, y: Float, timeMs: Long): Outcome {
        val was = phase
        phase = Phase.IDLE
        val tap = !moved && timeMs - downTime <= tapMaxMs
        return when (was) {
            Phase.DRAGGING -> Outcome(false, effect = dragEnd())
            Phase.PRESSED -> {
                if (tap) candidate = Triple(downX, downY, timeMs)
                PASS
            }
            Phase.HELD -> {
                if (!tap) {
                    streakSide = 0
                    return HOLD
                }
                val side = if (forward) 1 else -1
                val first = held == true
                streakTotal = if (first || side != streakSide) stepMs * side else streakTotal + stepMs * side
                streakSide = side
                streakUp = timeMs
                Outcome(false, effect = Effect.Jump(stepMs * side, streakTotal, restoreControls = first))
            }
            else -> PASS
        }
    }

    fun cancel(): Outcome {
        val was = phase
        phase = Phase.IDLE
        return if (was == Phase.DRAGGING) Outcome(true, effect = Effect.DragEnd(playback.positionMs, 0)) else PASS
    }

    private fun preview(x: Float): Effect.Preview {
        target = TimeText.clamp(playback.positionMs + ((x - downX) / density * msPerDp).roundToLong(), playback.durationMs)
        return Effect.Preview(target, target - playback.positionMs)
    }

    private fun dragEnd() = Effect.DragEnd(target, target - playback.positionMs)

    private companion object {
        val PASS = Outcome(true)
        val HOLD = Outcome(false)
    }
}

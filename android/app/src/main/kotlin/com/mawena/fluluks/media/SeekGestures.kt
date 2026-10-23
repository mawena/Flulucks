package com.mawena.fluluks.media

import kotlin.math.abs
import kotlin.math.roundToLong

/**
 * Gestes du lecteur vidéo, comme VLC (phase 12, spec § 20), sans vue : glisser horizontalement avance ou recule
 * ([msPerDp] par dp parcouru, saut au lever du doigt). L'activité donne chaque événement tactile ; la réponse dit
 * s'il faut le passer au lecteur ([Outcome.dispatch]), lui retirer le geste en cours ([Outcome.cancelChildren]) et
 * quoi afficher ou faire ([Outcome.effect]). Coordonnées et seuils en pixels.
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

    private enum class Phase { IDLE, PASS, PRESSED, DRAGGING }

    private var phase = Phase.IDLE
    private var downX = 0f
    private var downY = 0f
    private var playback = Playback(0, -1, false)
    private var target = 0L

    fun down(x: Float, y: Float, timeMs: Long, width: Int, onControl: Boolean, playback: Playback): Outcome {
        downX = x
        downY = y
        this.playback = playback
        phase = if (onControl || !playback.seekable) Phase.PASS else Phase.PRESSED
        return PASS
    }

    fun move(x: Float, y: Float): Outcome = when (phase) {
        Phase.IDLE, Phase.PASS -> PASS
        Phase.DRAGGING -> Outcome(false, effect = preview(x))
        Phase.PRESSED -> {
            val dx = abs(x - downX)
            val dy = abs(y - downY)
            when {
                dx > touchSlop && dx > dy -> {
                    phase = Phase.DRAGGING
                    Outcome(false, cancelChildren = true, effect = preview(x))
                }
                dx > touchSlop || dy > touchSlop -> { phase = Phase.PASS; PASS }
                else -> PASS
            }
        }
    }

    fun up(x: Float, y: Float, timeMs: Long): Outcome {
        val was = phase
        phase = Phase.IDLE
        return if (was == Phase.DRAGGING) Outcome(false, effect = dragEnd()) else PASS
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
    }
}

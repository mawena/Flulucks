package com.mawena.fluluks.media

import java.util.Locale
import kotlin.math.abs

/** Durées affichées par les gestes du lecteur vidéo (phase 12). */
object TimeText {
    private const val MINUS = "−"

    /** « 0:05 », « 12:34 », « 1:02:03 » ; négatif → « 0:00 ». */
    fun clock(ms: Long): String {
        val s = maxOf(ms, 0) / 1000
        val h = s / 3600
        val m = s / 60 % 60
        val sec = s % 60
        return if (h > 0) "%d:%02d:%02d".format(Locale.ROOT, h, m, sec) else "%d:%02d".format(Locale.ROOT, m, sec)
    }

    private fun sign(ms: Long) = if (ms < 0) MINUS else "+"

    /** Écart signé : « +0:25 », « −1:05 ». */
    fun delta(ms: Long): String = sign(ms) + clock(abs(ms))

    /** Écart signé en secondes : « +20 s », « −30 s ». */
    fun seconds(ms: Long): String = "${sign(ms)}${abs(ms) / 1000} s"

    /** Aperçu d'un glissement : « 12:34 / 1:30:00 », puis l'écart à la ligne ; sans durée si elle est inconnue (≤ 0). */
    fun preview(targetMs: Long, deltaMs: Long, durationMs: Long): String =
        (if (durationMs > 0) "${clock(targetMs)} / ${clock(durationMs)}" else clock(targetMs)) + "\n" + delta(deltaMs)

    /** Position [ms] bornée à [0, durée] ; durée inconnue (≤ 0) : seulement ≥ 0. */
    fun clamp(ms: Long, durationMs: Long): Long = maxOf(0, if (durationMs > 0) minOf(ms, durationMs) else ms)
}

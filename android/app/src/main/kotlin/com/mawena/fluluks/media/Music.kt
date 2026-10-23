package com.mawena.fluluks.media

import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.files.Files
import java.io.FileNotFoundException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Lecture en cours : morceau [index] sur [count] de la file, positions en millisecondes (durée −1 : inconnue). */
data class MusicState(
    val sourceId: String, val title: String, val index: Int, val count: Int, val playing: Boolean,
    val positionMs: Long, val durationMs: Long,
)

/** Le lecteur réel ([AndroidMusic] : MediaController vers [MusicService]) ou un faux dans les tests. */
interface MusicBackend {
    fun start(sourceId: String, paths: List<String>, startIndex: Int)

    /** `play`, `pause`, `next`, `previous`, `seek` ([positionMs]), `back` et `forward` (10 s). */
    fun command(command: String, positionMs: Long?)
    fun stop()
}

/**
 * Pont du lecteur de musique (phase 10) : file d'attente faite des sons du dossier, dans l'ordre affiché ; état publié
 * vers Flutter ([state] : lecture en cours, ou null, et le message de l'arrêt). Un stockage fermé ou un disque arraché
 * arrête la lecture : « Lecture arrêtée : stockage fermé ».
 */
object Music {
    val AUDIO = setOf("mp3", "m4a", "aac", "ogg", "oga", "opus", "flac", "wav")
    const val MSG_SOURCE_CLOSED = "Lecture arrêtée : stockage fermé"
    const val MSG_PLAY_ERROR = "Lecture impossible"
    const val MSG_NOTHING = "Aucun fichier audio à lire"

    @Volatile var backend: MusicBackend? = null
    @Volatile private var current: String? = null
    private val _state = MutableStateFlow<Pair<MusicState?, String?>>(null to null)
    val state: StateFlow<Pair<MusicState?, String?>> = _state.asStateFlow()

    @VisibleForTesting
    fun resetForTests(b: MusicBackend?) {
        backend = b
        current = null
        _state.value = null to null
    }

    fun isAudio(name: String) = name.substringAfterLast('.', "").lowercase() in AUDIO

    /** Sons de [shown] (dans l'ordre) et rang de départ : celui de `shown[startIndex]`, ou 0 si ce n'est pas un son. */
    fun queueFrom(shown: List<String>, startIndex: Int): Pair<List<String>, Int> {
        val queue = shown.filter { isAudio(it.substringAfterLast('/')) }
        val i = shown.getOrNull(startIndex)?.let(queue::indexOf) ?: -1
        return queue to maxOf(0, i)
    }

    fun play(sourceId: String, shown: List<String>, startIndex: Int) {
        Files.source(sourceId)
        val (queue, i) = queueFrom(shown, startIndex)
        if (queue.isEmpty()) throw FileNotFoundException(MSG_NOTHING)
        current = sourceId
        val b = backend ?: throw FileNotFoundException(MSG_PLAY_ERROR)
        b.start(sourceId, queue, i)
    }

    fun command(command: String, positionMs: Long?) {
        backend?.command(command, positionMs)
    }

    /** État lu sur le lecteur (changement, ou toutes les 500 ms pendant la lecture) ; null : arrêté. */
    fun publish(s: MusicState?) {
        if (s == null) current = null
        _state.value = s to null
    }

    /** Stockages changés (volume éjecté, disque arraché, carte retirée, serveur déconnecté). */
    fun checkSources() {
        val id = current ?: return
        if (Files.sourceOrNull(id) == null) stopWith(MSG_SOURCE_CLOSED)
    }

    /** Erreur du lecteur : souvent le stockage qui vient de disparaître. */
    fun onError() {
        val id = current
        stopWith(if (id != null && Files.sourceOrNull(id) == null) MSG_SOURCE_CLOSED else MSG_PLAY_ERROR)
    }

    fun stop() {
        backend?.stop()
        current = null
        _state.value = null to null
    }

    private fun stopWith(message: String) {
        backend?.stop()
        current = null
        _state.value = null to message
    }
}

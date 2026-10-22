package com.mawena.fluluks.volumes

import android.content.Context
import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.files.Transfers

/**
 * Verrouillage automatique (spec § 4.4 et § 13) : un volume **chiffré** sans activité depuis [minutes] est
 * éjecté par [VolumeManager.eject] (synchronisation, démontage, clé effacée). Jamais pendant une copie qui
 * le lit ou l'écrit. Toute lecture ou écriture ([VolumeManager.noteActivity]) repousse le délai.
 *
 * Une éjection refusée (fichier ouvert, disque qui refuse la synchronisation) n'est pas retentée tant que
 * le volume n'a pas eu de nouvelle activité : une seule notification, pas une toutes les 30 s.
 * [check] est appelé par [com.mawena.fluluks.service.VolumeService] toutes les [PERIOD_MS] ms.
 */
object AutoLock {
    /** Délais proposés, en minutes ; 0 = jamais (valeur par défaut). */
    val CHOICES = listOf(0, 5, 15, 30, 60)
    const val PERIOD_MS = 30_000L

    fun msgLocked(minutes: Int) = "Disque verrouillé après $minutes min sans activité"

    /** Délai accepté, ou 0 (jamais) pour une valeur hors de [CHOICES]. */
    fun validate(minutes: Int): Int = if (minutes in CHOICES) minutes else 0

    /** Délai réglé, relu à chaque contrôle ; branché sur les préférences par le pont. */
    @Volatile var minutes: () -> Int = { 0 }
    @Volatile private var busy: (String) -> Boolean = Transfers::isBusy
    @Volatile private var warn: (String) -> Unit = {}

    /** Volume → activité au moment de la dernière tentative (pas de nouvelle tentative sans nouvelle activité). */
    private val attempted = HashMap<String, Long>()

    /** Préférence partagée avec le pont Pigeon (fichier de préférences `fluluks`). */
    const val PREF_KEY = "verrouillage_auto"
    private const val PREFS = "fluluks"

    fun readPref(context: Context): Int =
        validate(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(PREF_KEY, 0))

    fun writePref(context: Context, minutes: Int) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt(PREF_KEY, validate(minutes)).apply()

    /** Branche le délai sur les préférences et l'avertissement sur une notification (service). */
    fun bind(context: Context, warner: (String) -> Unit) {
        val app = context.applicationContext
        minutes = { readPref(app) }
        warn = warner
    }

    /** Volumes à verrouiller maintenant. */
    @Synchronized
    fun due(now: Long): List<String> {
        val m = validate(minutes())
        val open = VolumeManager.volumes.value
        attempted.keys.retainAll(open.map { it.id }.toSet())
        if (m == 0) return emptyList()
        return open.filter { v ->
            val last = VolumeManager.lastActivity(v.id) ?: return@filter false
            v.encrypted && now - last >= m * 60_000L && !busy(v.id) && attempted[v.id] != last
        }.map { it.id }
    }

    /** Verrouille les volumes dus, avec une notification par volume. */
    fun check(now: Long) {
        val m = validate(minutes())
        for (id in due(now)) {
            synchronized(this) { VolumeManager.lastActivity(id)?.let { attempted[id] = it } }
            VolumeManager.eject(id)
            warn(msgLocked(m))
        }
    }

    @VisibleForTesting
    fun resetForTests(minutes: () -> Int = { 0 }, busy: (String) -> Boolean = { false }, warn: (String) -> Unit = {}) {
        synchronized(this) { attempted.clear() }
        this.minutes = minutes
        this.busy = busy
        this.warn = warn
    }
}

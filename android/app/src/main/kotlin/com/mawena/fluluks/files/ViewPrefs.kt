package com.mawena.fluluks.files

import android.content.Context
import com.mawena.fluluks.engine.EngineError
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** Réglages clé-valeur (SharedPreferences « fluluks », ou une table en mémoire dans les tests JVM). */
interface PrefStore {
    fun getString(key: String): String?
    fun putString(key: String, value: String)
    fun getBoolean(key: String, default: Boolean): Boolean
    fun putBoolean(key: String, value: Boolean)
}

class SharedPrefStore(context: Context) : PrefStore {
    private val p = context.getSharedPreferences("fluluks", Context.MODE_PRIVATE)
    override fun getString(key: String): String? = p.getString(key, null)
    override fun putString(key: String, value: String) = p.edit().putString(key, value).apply()
    override fun getBoolean(key: String, default: Boolean) = p.getBoolean(key, default)
    override fun putBoolean(key: String, value: Boolean) = p.edit().putBoolean(key, value).apply()
}

/** Préférences d'affichage. `sort` : `name`, `size` ou `date` ; [builtInViewers] : lecteurs de Portail (phase 10). */
data class ViewSettings(
    val sort: String = "name", val ascending: Boolean = true, val grid: Boolean = false, val showHidden: Boolean = false,
    val builtInViewers: Boolean = true,
)

/** Favori : `key` stable de la source, chemin, libellé ; `sourceId` null si le stockage n'est pas ouvert. */
data class Favorite(val key: String, val path: String, val label: String, val sourceId: String?)

/** Tri, affichage et favoris, persistés ; une valeur inconnue ou abîmée revient au défaut. */
class ViewPrefs(private val store: PrefStore, private val sources: () -> List<FileSource> = Sources::list) {
    fun get(): ViewSettings {
        val sort = store.getString(SORT)?.takeIf { it in SORTS } ?: "name"
        return ViewSettings(
            sort, store.getBoolean(ASC, true), store.getBoolean(GRID, false), store.getBoolean(HIDDEN, false),
            store.getBoolean(VIEWERS, true),
        )
    }

    fun set(v: ViewSettings) {
        store.putString(SORT, v.sort.takeIf { it in SORTS } ?: "name")
        store.putBoolean(ASC, v.ascending)
        store.putBoolean(GRID, v.grid)
        store.putBoolean(HIDDEN, v.showHidden)
        store.putBoolean(VIEWERS, v.builtInViewers)
    }

    private fun load(): MutableList<Triple<String, String, String>> {
        val raw = store.getString(FAVORITES) ?: return mutableListOf()
        val out = mutableListOf<Triple<String, String, String>>()
        try {
            val a = JSONArray(raw)
            for (i in 0 until minOf(a.length(), MAX_FAVORITES)) {
                val o = a.optJSONObject(i) ?: continue
                val key = o.optString("key"); val path = o.optString("path")
                if (key.isEmpty() || PathRules.check(path) == null) continue
                out += Triple(key, path, o.optString("label"))
            }
        } catch (e: JSONException) { // réglage abîmé : liste vide
        }
        return out
    }

    private fun save(l: List<Triple<String, String, String>>) {
        val a = JSONArray()
        for ((k, p, lab) in l) a.put(JSONObject().put("key", k).put("path", p).put("label", lab))
        store.putString(FAVORITES, a.toString())
    }

    /** Favoris, résolus vers les sources ouvertes du moment par leur clé stable. */
    fun favorites(): List<Favorite> {
        val open = sources().associateBy({ it.stableKey }, { it.id })
        return load().map { (k, p, lab) -> Favorite(k, p, lab, open[k]) }
    }

    fun add(sourceId: String, path: String, label: String) {
        val p = PathRules.require(path)
        val key = Files.source(sourceId).stableKey
        val l = load()
        if (l.any { it.first == key && it.second == p }) return
        if (l.size >= MAX_FAVORITES) throw FilesException(EngineError.FL_ENOSPC, MSG_TOO_MANY)
        l += Triple(key, p, label)
        save(l)
    }

    fun remove(key: String, path: String) {
        val l = load()
        if (l.removeAll { it.first == key && it.second == path }) save(l)
    }

    companion object {
        const val MAX_FAVORITES = 100
        const val MSG_TOO_MANY = "100 favoris au plus : retirez-en un d'abord"
        private val SORTS = setOf("name", "size", "date")
        private const val SORT = "tri"
        private const val ASC = "tri_croissant"
        private const val GRID = "grille"
        private const val HIDDEN = "caches"
        private const val VIEWERS = "lecteurs_portail"
        private const val FAVORITES = "favoris"
    }
}

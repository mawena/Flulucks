package com.mawena.fluluks.files

import com.mawena.fluluks.provider.WriteOps
import java.io.FileNotFoundException
import java.text.Normalizer

/** Résultat d'une recherche ; `truncated` si la limite de [Search.MAX_RESULTS] a été atteinte. */
data class SearchResult(val entries: List<Entry>, val truncated: Boolean)

/**
 * Recherche par nom sous un dossier, sans tenir compte de la casse ni des accents. Parcours en largeur,
 * borné (profondeur, résultats, dossiers visités) ; les dossiers illisibles sont sautés. Les résultats ne
 * sont gardés qu'en mémoire.
 */
object Search {
    const val MAX_RESULTS = 500
    const val MAX_DEPTH = 64

    /** Drapeau d'annulation de la recherche en cours (une seule à la fois du point de vue de l'interface). */
    @Volatile private var current: BooleanArray? = null

    /** Minuscules, sans marques diacritiques : « Été » → « ete ». */
    fun normalize(s: String): String =
        Normalizer.normalize(s, Normalizer.Form.NFD).replace(MARKS, "").lowercase()

    private val MARKS = Regex("\\p{Mn}+")

    fun run(sourceId: String, path: String, query: String, showHidden: Boolean): SearchResult = Files.guard("queryChildDocuments") {
        val q = normalize(query.trim())
        val start = PathRules.require(path)
        if (q.isEmpty()) return@guard SearchResult(emptyList(), false)
        val src = Files.source(sourceId)
        val flag = BooleanArray(1)
        current = flag
        val out = ArrayList<Entry>()
        val queue = ArrayDeque<Pair<String, Int>>().apply { add(start to 0) }
        var visited = 0
        while (queue.isNotEmpty() && !flag[0]) {
            val (dir, depth) = queue.removeFirst()
            if (++visited > WriteOps.MAX_ENTRIES) break
            val children = try {
                src.list(dir)
            } catch (e: FileNotFoundException) {
                if (dir == start && e !is ProtectedDirException) throw e // le dossier de départ doit exister
                continue // dossier protégé ou disparu : sauté
            }
            for (e in children.sortedBy { it.name.lowercase() }) {
                if (flag[0]) break
                if (!showHidden && e.name.startsWith(".")) continue
                if (normalize(e.name).contains(q)) {
                    if (out.size >= MAX_RESULTS) return@guard SearchResult(out, true)
                    out.add(e)
                }
                if (e.type.isDir() && depth + 1 < MAX_DEPTH) queue.addLast(e.path to depth + 1)
            }
        }
        if (current === flag) current = null
        SearchResult(out, false)
    }

    /** La recherche en cours s'arrête et rend ce qu'elle a trouvé. */
    fun cancel() {
        current?.let { it[0] = true }
    }
}

package com.mawena.fluluks.files

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.WriteOps
import java.io.FileNotFoundException
import java.util.PriorityQueue

/** Élément d'un dossier analysé : taille (totale pour un dossier) et nombre de fichiers. */
data class SpaceItem(val name: String, val path: String, val isDir: Boolean, val size: Long, val fileCount: Int)

/**
 * Occupation d'un dossier : [children] du plus gros au plus petit (au plus [SpaceAnalysis.MAX_CHILDREN], le reste
 * résumé par [otherCount] et [otherBytes]) et les plus gros fichiers sous lui ; [truncated] si l'analyse s'est
 * arrêtée avant la fin (annulation, bornes de profondeur ou de nombre de dossiers).
 */
data class SpaceReport(
    val path: String, val totalBytes: Long, val fileCount: Int, val children: List<SpaceItem>, val otherCount: Int,
    val otherBytes: Long, val biggest: List<Entry>, val truncated: Boolean,
)

/**
 * Analyse de l'espace d'un stockage (phase 9) : taille de chaque dossier et les [MAX_BIGGEST] plus gros fichiers,
 * avec les bornes de la recherche (profondeur [Search.MAX_DEPTH], [WriteOps.MAX_ENTRIES] dossiers). Fichiers cachés
 * compris (ils occupent la place), liens jamais suivis, dossiers illisibles sautés. Une analyse à la fois : une
 * nouvelle arrête la précédente ; [cancel] l'arrête et elle rend ce qu'elle a vu. Le résultat reste en mémoire pour
 * descendre dans les dossiers sans relire le stockage ([children]) ; il ne voit pas les changements faits après.
 */
object SpaceAnalysis {
    const val MAX_BIGGEST = 100
    const val MAX_CHILDREN = 200

    private class Dir(val size: Long, val files: Int, val children: List<SpaceItem>, val otherCount: Int, val otherBytes: Long)

    private class Result(val sourceId: String, val dirs: Map<String, Dir>, val biggest: List<Entry>, val truncated: Boolean)

    @Volatile private var current: BooleanArray? = null
    @Volatile private var last: Result? = null
    private val lock = Any()

    fun run(sourceId: String, path: String): SpaceReport = Files.guard("queryChildDocuments") {
        val start = PathRules.require(path)
        val src = Files.source(sourceId)
        val flag = BooleanArray(1)
        current?.let { it[0] = true } // une seule analyse à la fois
        current = flag
        synchronized(lock) {
            try {
                val w = Walk(src, flag)
                w.walk(start, 0)
                val r = Result(sourceId, w.dirs, w.top.sortedByDescending { it.size }, w.truncated || flag[0])
                last = r
                report(r, start)
            } finally {
                if (current === flag) current = null
            }
        }
    }

    /** Occupation de [path], lue dans la dernière analyse si elle l'a parcouru, sinon analysé maintenant. */
    fun children(sourceId: String, path: String): SpaceReport {
        val p = Files.guard("queryChildDocuments") { PathRules.require(path) }
        val r = last
        if (r != null && r.sourceId == sourceId && r.dirs.containsKey(p)) return report(r, p)
        return run(sourceId, p)
    }

    /** L'analyse en cours s'arrête et rend ce qu'elle a vu. */
    fun cancel() {
        current?.let { it[0] = true }
    }

    fun clearForTests() {
        current = null
        last = null
    }

    private fun report(r: Result, path: String): SpaceReport {
        val d = r.dirs.getValue(path)
        val prefix = if (path == "/") "/" else "$path/"
        val biggest = r.biggest.filter { it.path.startsWith(prefix) }
        return SpaceReport(path, d.size, d.files, d.children, d.otherCount, d.otherBytes, biggest, r.truncated)
    }

    private class Walk(val src: FileSource, val flag: BooleanArray) {
        val dirs = HashMap<String, Dir>()
        val top = PriorityQueue<Entry>(compareBy<Entry> { it.size })
        var visited = 0
        var truncated = false

        fun stopped() = flag[0] || truncated

        /** Taille et nombre de fichiers sous [path] ; un dossier illisible compte pour rien (le départ : erreur). */
        fun walk(path: String, depth: Int): Pair<Long, Int> {
            val list = try {
                src.list(path)
            } catch (e: FileNotFoundException) {
                if (depth == 0) throw e
                return 0L to 0
            } catch (e: EngineError) {
                if (depth == 0) throw e
                return 0L to 0
            }
            if (++visited > WriteOps.MAX_ENTRIES) truncated = true
            var size = 0L
            var files = 0
            val items = ArrayList<SpaceItem>()
            for (e in list) {
                if (stopped()) break
                when (e.type) {
                    FileType.FILE -> {
                        size += e.size
                        files++
                        items.add(SpaceItem(e.name, e.path, false, e.size, 1))
                        top.add(e)
                        if (top.size > MAX_BIGGEST) top.poll()
                    }
                    FileType.DIR -> {
                        if (depth + 1 >= Search.MAX_DEPTH) {
                            truncated = true
                            continue
                        }
                        val (s, f) = walk(e.path, depth + 1)
                        size += s
                        files += f
                        items.add(SpaceItem(e.name, e.path, true, s, f))
                    }
                    else -> {} // liens et fichiers spéciaux : ni suivis, ni comptés
                }
            }
            items.sortWith(compareByDescending<SpaceItem> { it.size }.thenBy { it.name.lowercase() })
            val rest = items.drop(MAX_CHILDREN)
            dirs[path] = Dir(size, files, items.take(MAX_CHILDREN), rest.size, rest.sumOf { it.size })
            return size to files
        }
    }
}

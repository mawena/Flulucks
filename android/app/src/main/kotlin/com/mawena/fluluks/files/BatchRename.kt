package com.mawena.fluluks.files

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.provider.DocNames
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import java.io.FileNotFoundException
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/**
 * Règle de renommage par lot : `number` (« [base] (01) », numéros d'au moins deux chiffres depuis [start]),
 * `replace` ([find] → [replace] dans le nom sans l'extension, casse respectée ou non) ou `photoDate`
 * (« AAAA-MM-JJ HH.MM.SS » : DateTimeOriginal de l'EXIF d'un JPEG, sinon date de modification). L'extension est
 * toujours gardée.
 */
data class RenameRule(
    val mode: String, val base: String = "", val start: Int = 1, val find: String = "", val replace: String = "",
    val caseSensitive: Boolean = false,
)

/** Nouveau nom de [path] ; [conflict] : pourquoi il est refusé (null si permis). */
data class RenamePreview(val path: String, val newName: String, val conflict: String?)

/**
 * Renommage par lot (phase 9) : un aperçu d'abord, puis tout ou rien. Un conflit (nom pris par un élément hors du
 * lot, doublon dans le lot, nom invalide pour le stockage) refuse le lot avant tout renommage. Les noms passent
 * par des noms temporaires : un échange (a → b, b → a) fonctionne.
 */
object BatchRename {
    const val CONFLICT_EXISTS = "Un élément porte déjà ce nom"
    const val CONFLICT_DUPLICATE = "Nom en double dans le lot"
    const val CONFLICT_INVALID = "Nom invalide pour ce stockage"
    const val MSG_CONFLICTS = "Renommage impossible : des noms sont refusés"
    private const val TMP = ".portail-renommage-"
    private val DATE = DateTimeFormatter.ofPattern("yyyy-MM-dd HH.mm.ss")

    fun preview(src: FileSource, paths: List<String>, rule: RenameRule): List<RenamePreview> = check(src, plan(src, paths, rule))

    fun apply(src: FileSource, paths: List<String>, rule: RenameRule) = applyPlanned(src, plan(src, paths, rule))

    /** Lot explicite : (chemin, nouveau nom). */
    fun planned(pairs: List<Pair<String, String>>): List<Pair<String, String>> = pairs.map { (p, n) -> PathRules.require(p) to n }

    /** (nom sans extension, extension avec son point) ; « .bashrc » n'a pas d'extension. */
    private fun split(name: String): Pair<String, String> {
        val dot = name.lastIndexOf('.')
        return if (dot > 0) name.substring(0, dot) to name.substring(dot) else name to ""
    }

    private fun plan(src: FileSource, paths: List<String>, rule: RenameRule): List<Pair<String, String>> {
        val ps = paths.map { PathRules.require(it) }
        if (ps.any { it == "/" }) throw FileNotFoundException("La racine ne peut pas être renommée")
        val width = maxOf(2, (rule.start + ps.size - 1).toString().length)
        return ps.mapIndexed { i, p ->
            val (base, ext) = split(WriteOps.nameOf(p))
            val newBase = when (rule.mode) {
                "number" -> listOf(rule.base.trim(), "(${(rule.start + i).toString().padStart(width, '0')})")
                    .filter { it.isNotEmpty() }.joinToString(" ")
                "replace" -> if (rule.find.isEmpty()) base else base.replace(rule.find, rule.replace, ignoreCase = !rule.caseSensitive)
                "photoDate" -> photoDate(src, p).format(DATE)
                else -> throw FilesException(EngineError.FL_EINVAL, "Règle de renommage inconnue")
            }
            p to newBase + ext
        }
    }

    /** Date de prise de vue d'un JPEG, sinon date de modification (heure locale). */
    private fun photoDate(src: FileSource, path: String): LocalDateTime {
        val name = path.lowercase()
        if (name.endsWith(".jpg") || name.endsWith(".jpeg")) {
            val exif = try {
                src.openRead(path).use { Exif.dateTimeOriginal(it) }
            } catch (e: Exception) {
                null
            }
            if (exif != null) return exif
        }
        val mtime = src.stat(path)?.mtime ?: throw FileNotFoundException("Document introuvable")
        return LocalDateTime.ofEpochSecond(mtime, 0, ZoneId.systemDefault().rules.getOffset(java.time.Instant.ofEpochSecond(mtime)))
    }

    private fun valid(src: FileSource, name: String) =
        DocNames.isValid(name) && (!src.restrictedNames || DocNames.sanitize(name) == name)

    fun check(src: FileSource, plan: List<Pair<String, String>>): List<RenamePreview> {
        fun key(n: String) = if (src.caseInsensitive) n.lowercase() else n
        val inBatch = plan.map { it.first }.toSet()
        // noms des éléments hors du lot, par dossier
        val others = plan.map { WriteOps.parentOf(it.first) }.distinct().associateWith { d ->
            src.list(d).filter { it.path !in inBatch }.map { key(it.name) }.toSet()
        }
        val finals = plan.groupingBy { (p, n) -> WriteOps.parentOf(p) to key(n) }.eachCount()
        return plan.map { (p, n) ->
            val d = WriteOps.parentOf(p)
            val conflict = when {
                WriteOps.nameOf(p) == n -> null // inchangé
                !valid(src, n) -> CONFLICT_INVALID
                key(n) in others.getValue(d) -> CONFLICT_EXISTS
                finals.getValue(d to key(n)) > 1 -> CONFLICT_DUPLICATE
                else -> null
            }
            RenamePreview(p, n, conflict)
        }
    }

    fun applyPlanned(src: FileSource, plan: List<Pair<String, String>>) {
        if (check(src, plan).any { it.conflict != null }) throw FilesException(EngineError.FL_EEXIST, MSG_CONFLICTS)
        val todo = plan.filter { (p, n) -> WriteOps.nameOf(p) != n }
        val current = todo.map { it.first }.toMutableList()
        try {
            // noms temporaires d'abord : un nom libéré par un élément du lot peut être repris par un autre
            for (i in todo.indices) {
                val dir = WriteOps.parentOf(current[i])
                val tmp = DocNames.unique("$TMP$i") { src.stat(DocumentIds.child(dir, it)) != null }
                    ?: throw FileNotFoundException("Trop d'éléments portent déjà ce nom")
                current[i] = src.rename(current[i], tmp)
            }
            for (i in todo.indices) current[i] = src.rename(current[i], todo[i].second)
        } catch (e: Exception) {
            // au mieux : chaque élément reprend son nom d'origine
            for (i in todo.indices.reversed()) {
                val orig = WriteOps.nameOf(todo[i].first)
                if (WriteOps.nameOf(current[i]) != orig) runCatching { src.rename(current[i], orig) }
            }
            throw e
        }
    }
}

package com.mawena.fluluks.files

import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.DocNames
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import com.mawena.fluluks.volumes.VolumeManager
import java.io.FileNotFoundException
import java.io.OutputStream
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/** Avancement d'une copie ou d'un déplacement. `fileIndex` part de 1 ; `error` peut accompagner `finished`. */
data class TransferState(
    val move: Boolean,
    val currentName: String,
    val fileIndex: Int,
    val fileCount: Int,
    val doneBytes: Long,
    val totalBytes: Long,
    val finished: Boolean = false,
    val cancelled: Boolean = false,
    val error: String? = null,
    /** Durée de l'opération (ms), connue à la fin. */
    val elapsedMs: Long = 0,
)

/**
 * Copier et déplacer entre deux sources quelconques, une opération à la fois.
 *
 * Avant toute écriture : parcours borné de la source, place libre de la destination, copie dans soi-même
 * refusée. Un déplacement entre deux sources ne supprime un élément de premier niveau qu'une fois tous ses
 * fichiers copiés et leur taille vérifiée sur la destination, et jamais si un fichier a changé de taille
 * pendant la copie (téléchargement en cours). En cas d'échec ou d'annulation, le fichier en
 * cours de la destination est supprimé (si elle est encore là) ; ce qui est complet est gardé.
 */
object Transfers {
    const val MSG_BUSY = "Une copie est déjà en cours"
    const val MSG_INTO_ITSELF = Files.MSG_INTO_ITSELF
    const val MSG_SOURCE_GONE = "Stockage fermé : opération interrompue"
    const val MSG_LINKS = "Les liens symboliques et fichiers spéciaux ne sont pas copiés"
    const val MSG_SIZE_MISMATCH = "Copie incomplète : taille différente sur la destination"
    const val MSG_CHANGED = "Fichier modifié pendant le déplacement : l'original est gardé"
    fun msgNoSpace(need: Long, free: Long) =
        "Pas assez de place : ${VolumeManager.humanSize(need)} nécessaires, ${VolumeManager.humanSize(free)} libres"

    /** Publication de l'avancement au plus toutes les [PUBLISH_MS] ms (et à chaque fin de fichier). */
    const val PUBLISH_MS = 200L

    private val _state = MutableStateFlow<TransferState?>(null)
    val state: StateFlow<TransferState?> = _state.asStateFlow()

    private class Running(val srcId: String, val dstId: String) {
        @Volatile var cancelled = false
    }

    @Volatile private var running: Running? = null
    @Volatile private var dispatcher: CoroutineDispatcher = Dispatchers.IO
    @Volatile private var clock: () -> Long = { System.nanoTime() / 1_000_000 }

    @VisibleForTesting
    fun resetForTests(dispatcher: CoroutineDispatcher = Dispatchers.IO, clock: () -> Long = { System.nanoTime() / 1_000_000 }) {
        running?.cancelled = true
        running = null
        this.dispatcher = dispatcher
        this.clock = clock
        _state.value = null
    }

    /** Vrai si une opération lit ou écrit [sourceId] (verrouillage automatique : jamais pendant une copie). */
    fun isBusy(sourceId: String): Boolean = running?.let { it.srcId == sourceId || it.dstId == sourceId } ?: false

    /** Lance l'opération ; [FilesException] avec [MSG_BUSY] si une autre est en cours. */
    fun start(srcId: String, paths: List<String>, dstId: String, dstDir: String, move: Boolean) {
        val r = Running(srcId, dstId)
        synchronized(this) {
            if (running != null) throw FilesException(EngineError.FL_EBUSY, MSG_BUSY)
            running = r
        }
        _state.value = TransferState(move, "", 0, 0, 0, 0)
        CoroutineScope(SupervisorJob() + dispatcher).launch {
            try {
                Job(r, srcId, paths, dstId, dstDir, move).run()
            } finally {
                synchronized(this@Transfers) { if (running === r) running = null }
            }
        }
    }

    /** Arrête l'opération en cours au prochain morceau ; le fichier partiel est supprimé. */
    fun cancel() {
        running?.cancelled = true
    }

    private class Cancelled : Exception()

    /** Élément à copier : chemin source, chemin relatif au premier niveau (vide pour lui-même). */
    private class Node(val src: String, val rel: String, val type: FileType, val size: Long, val depth: Int)

    private class Job(
        val r: Running, val srcId: String, val paths: List<String>, val dstId: String, val dstDir: String, val move: Boolean,
    ) {
        var st = TransferState(move, "", 0, 0, 0, 0)
        val startMs = clock()
        var lastPublish = 0L
        var skipped = 0
        var changed = 0

        fun publish(force: Boolean = false) {
            val now = clock()
            if (force || now - lastPublish >= PUBLISH_MS) {
                lastPublish = now
                _state.value = st
            }
        }

        fun finish(error: String? = null, cancelled: Boolean = false) {
            val warning = when {
                changed > 0 -> MSG_CHANGED
                skipped > 0 -> MSG_LINKS
                else -> null
            }
            st = st.copy(finished = true, cancelled = cancelled, error = error ?: warning, elapsedMs = clock() - startMs)
            _state.value = st
        }

        fun checkCancel() { if (r.cancelled) throw Cancelled() }

        fun run() {
            val src = Files.sourceOrNull(srcId)
            val dst = Files.sourceOrNull(dstId)
            if (src == null || dst == null) return finish(MSG_SOURCE_GONE)
            try {
                execute(src, dst)
            } catch (e: Cancelled) {
                finish(cancelled = true)
            } catch (e: Throwable) {
                val gone = !alive(src) || !alive(dst)
                finish(if (gone) MSG_SOURCE_GONE else Files.toFilesException(e, "copyDocument").message)
            }
        }

        fun execute(src: FileSource, dst: FileSource) {
            val tops = paths.map { PathRules.require(it) }
            val dir = PathRules.require(dstDir)
            dst.readOnlyReason?.let { throw FileNotFoundException(Files.readOnlyMessage(dst, it)) }
            if (move) src.readOnlyReason?.let { throw FileNotFoundException(Files.readOnlyMessage(src, it)) }
            val d = dst.stat(dir) ?: throw FileNotFoundException("Dossier introuvable")
            if (!d.type.isDir()) throw FileNotFoundException("Ce n'est pas un dossier")
            if (tops.any { it == "/" }) throw FileNotFoundException("La racine ne peut pas être copiée")
            if (srcId == dstId && tops.any { dir == it || DocumentIds.isDescendant(it, dir) }) {
                throw FilesException(EngineError.FL_EINVAL, MSG_INTO_ITSELF)
            }
            if (move && srcId == dstId) return moveSameSource(src, tops, dir)

            // parcours borné de toute la sélection avant d'écrire quoi que ce soit
            val plans = tops.map { scan(src, it) }
            val files = plans.sumOf { p -> p.count { it.type == FileType.FILE } }
            val total = plans.sumOf { p -> p.filter { it.type == FileType.FILE }.sumOf { it.size } }
            val (free, _) = dst.space()
            if (total > free) throw FilesException(EngineError.FL_ENOSPC, msgNoSpace(total, free))
            st = st.copy(fileCount = files, totalBytes = total)
            publish(force = true)

            val restricted = dst.restrictedNames
            for ((i, plan) in plans.withIndex()) {
                checkCancel()
                val top = tops[i]
                val mapped = HashMap<String, String>() // chemin relatif → chemin sur la destination
                var complete = true
                for (n in plan) {
                    if (n.type != FileType.DIR && n.type != FileType.FILE) { skipped++; complete = false; continue }
                    val parentRel = if (n.rel.isEmpty()) null else WriteOps.parentOf(n.rel).let { if (it == "/") "" else it }
                    val parentDst = if (parentRel == null) dir else mapped[parentRel] ?: continue
                    val raw = WriteOps.nameOf(if (n.rel.isEmpty()) top else n.rel)
                    val name = if (restricted) DocNames.sanitize(raw) else raw
                    if (n.type == FileType.DIR) {
                        checkCancel()
                        mapped[n.rel] = dst.mkdir(parentDst, name)
                    } else if (!copyFile(src, dst, n, parentDst, name) && move) {
                        changed++; complete = false // fichier encore en cours d'écriture : source gardée
                    }
                }
                // déplacement : la source n'est supprimée qu'une fois tout l'élément copié et vérifié
                if (move && complete) src.delete(top)
            }
            finish()
        }

        fun moveSameSource(src: FileSource, tops: List<String>, dir: String) {
            st = st.copy(fileCount = tops.size)
            for ((i, p) in tops.withIndex()) {
                checkCancel()
                st = st.copy(currentName = WriteOps.nameOf(p), fileIndex = i + 1)
                publish(force = true)
                src.move(p, dir)
            }
            finish()
        }

        /** Arborescence de [top], parents avant enfants, bornée (profondeur, nombre d'éléments). */
        fun scan(src: FileSource, top: String): List<Node> {
            val root = src.stat(top) ?: throw FileNotFoundException("Document introuvable")
            val out = ArrayList<Node>()
            val stack = ArrayDeque<Node>()
            stack.addLast(Node(top, "", root.type, root.size, 0))
            while (stack.isNotEmpty()) {
                checkCancel()
                val n = stack.removeLast()
                if (n.depth > WriteOps.MAX_DEPTH || out.size >= WriteOps.MAX_ENTRIES) throw FileNotFoundException(WriteOps.TOO_DEEP)
                out.add(n)
                if (n.type != FileType.DIR) continue
                // ordre des noms à la sortie de la pile : la copie suit l'ordre affiché
                for (e in src.list(n.src).sortedByDescending { it.name.lowercase() }) {
                    if (e.name == "." || e.name == "..") continue
                    val rel = if (n.rel.isEmpty()) "/${e.name}" else "${n.rel}/${e.name}"
                    stack.addLast(Node(e.path, rel, e.type, e.size, n.depth + 1))
                    if (stack.size + out.size > WriteOps.MAX_ENTRIES) throw FileNotFoundException(WriteOps.TOO_DEEP)
                }
            }
            return out
        }

        /** Copie un fichier ; faux si la source a changé de taille depuis le parcours (encore en écriture). */
        fun copyFile(src: FileSource, dst: FileSource, n: Node, parentDst: String, name: String): Boolean {
            checkCancel()
            st = st.copy(currentName = name, fileIndex = st.fileIndex + 1)
            publish(force = true)
            var created: String? = null
            var out: OutputStream? = null
            var written = 0L
            try {
                src.openRead(n.src).use { input ->
                    val (path, o) = dst.createFile(parentDst, name)
                    created = path
                    out = o
                    val buf = ByteArray(WriteOps.COPY_CHUNK)
                    while (true) {
                        checkCancel()
                        val k = input.read(buf)
                        if (k < 0) break
                        if (k == 0) continue
                        o.write(buf, 0, k)
                        written += k
                        st = st.copy(doneBytes = st.doneBytes + k)
                        publish()
                    }
                    out = null
                    o.close()
                }
                val copied = created!!
                val check = dst.stat(copied)
                if (check == null || check.size != written) {
                    throw FilesException(EngineError.FL_EIO, MSG_SIZE_MISMATCH)
                }
                if (written != n.size) st = st.copy(totalBytes = st.totalBytes + (written - n.size))
                created = null
                publish(force = true)
                return written == n.size && src.stat(n.src)?.size == written
            } finally {
                out?.let { runCatching { it.close() } }
                created?.let { partial -> if (alive(dst)) runCatching { dst.delete(partial) } }
            }
        }
    }

    /** La source répond encore (volume ouvert, carte SD présente). */
    private fun alive(s: FileSource): Boolean = try {
        s.stat("/") != null
    } catch (e: Exception) {
        false
    }
}

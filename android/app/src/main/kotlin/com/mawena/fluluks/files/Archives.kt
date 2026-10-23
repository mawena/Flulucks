package com.mawena.fluluks.files

import android.os.ParcelFileDescriptor
import androidx.annotation.VisibleForTesting
import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.engine.NativeEngine
import com.mawena.fluluks.provider.DocNames
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import java.io.File
import java.io.FileNotFoundException
import java.io.OutputStream
import java.util.Base64
import org.json.JSONObject

/**
 * Compresser une sélection en ZIP et extraire une archive (ZIP, 7z, RAR, TAR, GZ, XZ), d'une source vers
 * n'importe quelle autre, sur le créneau unique des copies ([Transfers.launch]) : progression et annulation
 * comme une copie.
 *
 * Une archive est hostile. Chaque nom passe par [ArchivePaths.safeRelative] ; les dossiers sont tous créés par
 * l'extraction (jamais à travers un lien, jamais dans un dossier qui existait déjà sous la destination) ; liens,
 * liens physiques et fichiers spéciaux ne sont pas extraits (comptés et signalés) ; une entrée chiffrée est
 * refusée. Avant d'écrire, la somme des tailles annoncées doit tenir dans la place libre moins une réserve
 * ([reserve]) ; pendant l'extraction, tout s'arrête dès que ce budget est dépassé (tailles mensongères, flux
 * sans taille). Le fichier en cours porte un nom temporaire, supprimé en cas d'échec ou d'annulation.
 */
object Archives {
    const val MSG_ENCRYPTED = "Archive protégée par mot de passe : non prise en charge"
    const val MSG_CORRUPT = "Archive incohérente"
    const val MSG_NOT_ARCHIVE = "Ce fichier n'est pas une archive reconnue"
    const val MSG_TOO_BIG = "Pas assez de place : l'archive se décompresse au-delà de la place libre"
    const val MSG_CHANGED = "Fichier modifié pendant la compression"
    fun msgSkipped(n: Int) = "$n élément(s) de l'archive ignoré(s)"

    /** Extensions reconnues, les doubles avant les simples. */
    private val EXTENSIONS = listOf(".tar.gz", ".tar.xz", ".tgz", ".txz", ".zip", ".7z", ".rar", ".tar", ".gz", ".xz")

    /** Formats déjà compressés : stockés sans compression dans un ZIP. */
    private val STORED = setOf(
        "jpg", "jpeg", "png", "gif", "webp", "heic", "mp4", "mkv", "mov", "avi", "mp3", "m4a", "ogg", "opus", "flac",
        "zip", "7z", "rar", "gz", "xz", "bz2", "apk",
    )

    /** Réserve laissée libre par une extraction : min(256 Mio, 5 % du stockage). */
    fun reserve(total: Long): Long = minOf(256L shl 20, total / 20)

    private fun extension(name: String): String? =
        EXTENSIONS.firstOrNull { name.length > it.length && name.endsWith(it, ignoreCase = true) }

    fun isArchive(name: String): Boolean = extension(name) != null

    /** Nom sans l'extension d'archive : « photos.tar.gz » → « photos » (dossier de « Extraire ici »). */
    fun stripExtension(name: String): String = extension(name)?.let { name.dropLast(it.length) } ?: name

    private fun stored(entryName: String): Boolean =
        entryName.substringAfterLast('/').substringAfterLast('.', "").lowercase() in STORED

    @Volatile private var engineForTests: Engine? = null
    @Volatile private var openFdForTests: ((File, Boolean) -> Int)? = null
    private val engine: Engine get() = engineForTests ?: NativeEngine

    /** Tests JVM : moteur et ouverture des descripteurs simulés ; null rend ceux d'Android. */
    @VisibleForTesting
    fun resetForTests(engine: Engine?, openFd: ((File, Boolean) -> Int)?) {
        engineForTests = engine
        openFdForTests = openFd
    }

    /** Descripteur détaché du fichier local [f] : il appartient ensuite à l'archive, qui le ferme. */
    private fun openFd(f: File, write: Boolean): Int {
        openFdForTests?.let { return it(f, write) }
        val mode = if (write) ParcelFileDescriptor.MODE_WRITE_ONLY or ParcelFileDescriptor.MODE_TRUNCATE
        else ParcelFileDescriptor.MODE_READ_ONLY
        return ParcelFileDescriptor.open(f, mode).detachFd()
    }

    /**
     * Compresse [paths] de [srcId] dans un ZIP sous [dstDir] (même source). [name] : nom choisi (« .zip » ajouté
     * au besoin), sinon celui de l'élément seul (sans son extension) ou « Archive » ; nom unique.
     */
    fun startCompress(srcId: String, paths: List<String>, dstDir: String, name: String?) =
        Transfers.launch("compress", srcId, srcId) { Compress(it, srcId, paths, dstDir, name).run() }

    /** Extrait [archive] de [srcId] sous [dstDir] de [dstId], dans un nouveau dossier au nom de l'archive si [intoNewFolder]. */
    fun startExtract(srcId: String, archive: String, dstId: String, dstDir: String, intoNewFolder: Boolean) =
        Transfers.launch("extract", srcId, dstId) { Extract(it, srcId, archive, dstId, dstDir, intoNewFolder).run() }

    private class ArcEntry(val raw: ByteArray, val type: String, val size: Long, val encrypted: Boolean)

    /** Erreur de lecture du moteur, en message français pour les cas propres aux archives. */
    private fun readError(code: Int): Exception = when (code) {
        EngineError.FL_ECORRUPT -> FilesException(code, MSG_CORRUPT)
        EngineError.FL_ENOTSUP -> FilesException(code, MSG_ENCRYPTED)
        else -> EngineError(code)
    }

    /** Entrée suivante de l'archive [h], null à la fin. */
    private fun next(h: Long): ArcEntry? {
        val b = engine.arcNext(h) ?: return null
        val o = JSONObject(String(b, Charsets.UTF_8))
        if (o.has("err")) throw readError(o.getInt("err"))
        return ArcEntry(Base64.getDecoder().decode(o.getString("name")), o.getString("type"), o.getLong("size"), o.optBoolean("encrypted"))
    }

    /** Ouvre l'archive [path] de [src], passe sa poignée et l'action « activité » de la source à [body], la ferme toujours. */
    private fun <T> withReader(src: FileSource, path: String, body: (Long, () -> Unit) -> T): T {
        var activity: () -> Unit = {}
        val h = when (val n = src.openNative(path)) {
            is NativeIn.Fd -> engine.arcOpenFd(openFd(n.file, false))
            is NativeIn.EngineFile -> {
                activity = n.onActivity
                engine.arcOpenFile(n.handle, n.size)
            }
        }
        if (h < 0) {
            val code = h.toInt()
            throw when (code) {
                EngineError.FL_ENOTSUP -> FilesException(code, MSG_NOT_ARCHIVE)
                EngineError.FL_ECORRUPT -> FilesException(code, MSG_CORRUPT)
                else -> EngineError(code)
            }
        }
        try {
            return body(h, activity)
        } finally {
            engine.arcClose(h) // FL_EINVAL si le disque a été fermé entre-temps : déjà libérée
        }
    }

    private class Extract(
        val c: Transfers.JobContext, val srcId: String, val archive: String, val dstId: String, val dstDir: String,
        val intoNewFolder: Boolean,
    ) {
        var skipped = 0

        fun run() {
            val src = Files.sourceOrNull(srcId)
            val dst = Files.sourceOrNull(dstId)
            if (src == null || dst == null) return c.finish(Transfers.MSG_SOURCE_GONE)
            try {
                execute(src, dst)
                c.finish(if (skipped > 0) msgSkipped(skipped) else null)
            } catch (e: Transfers.Cancelled) {
                c.finish(cancelled = true)
            } catch (e: Throwable) {
                val gone = !Transfers.alive(src) || !Transfers.alive(dst)
                c.finish(if (gone) Transfers.MSG_SOURCE_GONE else Files.toFilesException(e, "copyDocument").message)
            }
        }

        fun execute(src: FileSource, dst: FileSource) {
            val arc = PathRules.require(archive)
            val dir = PathRules.require(dstDir)
            dst.readOnlyReason?.let { throw FileNotFoundException(Files.readOnlyMessage(dst, it)) }
            val d = dst.stat(dir) ?: throw FileNotFoundException("Dossier introuvable")
            if (!d.type.isDir()) throw FileNotFoundException("Ce n'est pas un dossier")
            val arcName = WriteOps.nameOf(arc)
            val zip = arcName.endsWith(".zip", ignoreCase = true)
            fun relOf(e: ArcEntry) = ArchivePaths.safeRelative(ArchivePaths.decodeName(e.raw, zip))

            // premier passage, en-têtes seulement : entrée chiffrée et place vérifiées avant d'écrire quoi que ce soit
            var files = 0
            var announced = 0L
            withReader(src, arc) { h, activity ->
                while (true) {
                    c.checkCancel()
                    activity()
                    val e = next(h) ?: break
                    if (e.encrypted) throw FilesException(EngineError.FL_ENOTSUP, MSG_ENCRYPTED)
                    if (e.type == "f" && relOf(e) != null) {
                        files++
                        if (e.size > 0) announced += e.size
                    }
                }
            }
            val (free, total) = dst.space()
            val budget = free - reserve(total)
            if (announced > budget) throw FilesException(EngineError.FL_ENOSPC, Transfers.msgNoSpace(announced, maxOf(0L, budget)))
            c.st = c.st.copy(fileCount = files, totalBytes = announced)
            c.publish(force = true)

            val restricted = dst.restrictedNames
            fun clean(n: String) = if (restricted) DocNames.sanitize(n) else n
            val root = if (intoNewFolder) dst.mkdir(dir, clean(stripExtension(arcName))) else dir
            // dossiers de l'archive (chemin relatif) → dossiers créés par nous sur la destination
            val dirs = HashMap<String, String>()
            fun dirFor(rel: String): String {
                if (rel.isEmpty()) return root
                dirs[rel]?.let { return it }
                val parent = dirFor(rel.substringBeforeLast('/', ""))
                c.checkCancel()
                return dst.mkdir(parent, clean(rel.substringAfterLast('/'))).also { dirs[rel] = it }
            }
            var written = 0L
            withReader(src, arc) { h, activity ->
                val buf = ByteArray(WriteOps.COPY_CHUNK)
                while (true) {
                    c.checkCancel()
                    activity()
                    val e = next(h) ?: break
                    if (e.encrypted) throw FilesException(EngineError.FL_ENOTSUP, MSG_ENCRYPTED)
                    val rel = relOf(e)
                    when {
                        rel == null || (e.type != "f" && e.type != "d") -> skipped++
                        e.type == "d" -> dirFor(rel)
                        else -> {
                            val parent = dirFor(rel.substringBeforeLast('/', ""))
                            written += extractFile(h, e, dst, parent, clean(rel.substringAfterLast('/')), buf, budget - written, activity)
                        }
                    }
                }
            }
        }

        /** Écrit les données de l'entrée courante dans [parent]/[name] (nom unique) ; rend le nombre d'octets écrits. */
        fun extractFile(
            h: Long, e: ArcEntry, dst: FileSource, parent: String, name: String, buf: ByteArray, budget: Long, activity: () -> Unit,
        ): Long {
            c.checkCancel()
            c.st = c.st.copy(currentName = name, fileIndex = c.st.fileIndex + 1)
            c.publish(force = true)
            var created: String? = null
            var out: OutputStream? = null
            var done = 0L
            try {
                // nom temporaire, comme une copie : un fichier incomplet ne porte jamais le vrai nom
                val tmp = Transfers.partialName(name)
                dst.stat(DocumentIds.child(parent, tmp))?.let { if (it.type == FileType.FILE) dst.delete(it.path) }
                val (path, o) = dst.createFile(parent, tmp)
                created = path
                out = o
                while (true) {
                    c.checkCancel()
                    activity()
                    val k = engine.arcRead(h, buf, buf.size)
                    if (k < 0) throw readError(k)
                    if (k == 0) break
                    if (e.size >= 0 && done + k > e.size) throw FilesException(EngineError.FL_ECORRUPT, MSG_CORRUPT)
                    if (done + k > budget) throw FilesException(EngineError.FL_ENOSPC, MSG_TOO_BIG)
                    o.write(buf, 0, k)
                    done += k
                    c.st = c.st.copy(doneBytes = c.st.doneBytes + k)
                    c.publish()
                }
                out = null
                o.close()
                if (done != e.size) c.st = c.st.copy(totalBytes = c.st.totalBytes + done - maxOf(e.size, 0L))
                val finalName = DocNames.unique(name) { dst.stat(DocumentIds.child(parent, it)) != null }
                    ?: throw FileNotFoundException("Trop d'éléments portent déjà ce nom")
                dst.rename(path, finalName)
                created = null
                c.publish(force = true)
                return done
            } finally {
                out?.let { runCatching { it.close() } }
                created?.let { partial -> if (Transfers.alive(dst)) runCatching { dst.delete(partial) } }
            }
        }
    }

    private class Compress(
        val c: Transfers.JobContext, val srcId: String, val paths: List<String>, val dstDir: String, val name: String?,
    ) {
        var skipped = 0

        fun run() {
            val s = Files.sourceOrNull(srcId) ?: return c.finish(Transfers.MSG_SOURCE_GONE)
            try {
                execute(s)
                c.finish(if (skipped > 0) Transfers.MSG_LINKS else null)
            } catch (e: Transfers.Cancelled) {
                c.finish(cancelled = true)
            } catch (e: Throwable) {
                c.finish(if (!Transfers.alive(s)) Transfers.MSG_SOURCE_GONE else Files.toFilesException(e, "createDocument").message)
            }
        }

        fun zipName(tops: List<String>, plans: List<List<Transfers.Node>>): String {
            val given = name?.trim().orEmpty()
            val base = when {
                given.isNotEmpty() -> given
                tops.size == 1 -> WriteOps.nameOf(tops[0]).let { n ->
                    if (plans[0][0].type == FileType.DIR) n else n.substringBeforeLast('.', n).ifEmpty { n }
                }
                else -> "Archive"
            }
            return if (base.endsWith(".zip", ignoreCase = true)) base else "$base.zip"
        }

        fun execute(s: FileSource) {
            val tops = paths.map { PathRules.require(it) }
            val dir = PathRules.require(dstDir)
            s.readOnlyReason?.let { throw FileNotFoundException(Files.readOnlyMessage(s, it)) }
            val d = s.stat(dir) ?: throw FileNotFoundException("Dossier introuvable")
            if (!d.type.isDir()) throw FileNotFoundException("Ce n'est pas un dossier")
            if (tops.isEmpty()) throw FileNotFoundException("Rien à compresser")
            if (tops.any { it == "/" }) throw FileNotFoundException("La racine ne peut pas être compressée")

            // parcours borné de toute la sélection avant de créer l'archive : elle n'en fait donc jamais partie
            val plans = tops.map { Transfers.scanTree(s, it, c::checkCancel) }
            val files = plans.sumOf { p -> p.count { it.type == FileType.FILE } }
            val total = plans.sumOf { p -> p.filter { it.type == FileType.FILE }.sumOf { it.size } }
            val (free, _) = s.space()
            if (total > free) throw FilesException(EngineError.FL_ENOSPC, Transfers.msgNoSpace(total, free))
            c.st = c.st.copy(fileCount = files, totalBytes = total)
            c.publish(force = true)

            val zipName = zipName(tops, plans).let { if (s.restrictedNames) DocNames.sanitize(it) else it }
            val tmp = Transfers.partialName(zipName)
            s.stat(DocumentIds.child(dir, tmp))?.let { if (it.type == FileType.FILE) s.delete(it.path) }
            val (partial, out) = s.createNative(dir, tmp)
            var created: String? = partial
            var onClosed: () -> Unit = {}
            var h = 0L
            try {
                h = when (out) {
                    is NativeOut.Fd -> engine.arcwOpenFd(openFd(out.file, true))
                    is NativeOut.EngineFile -> {
                        onClosed = out.onClosed
                        engine.arcwOpenFile(out.handle)
                    }
                }
                EngineError.check(h) // en cas d'échec, le moteur a déjà fermé la destination
                val buf = ByteArray(WriteOps.COPY_CHUNK)
                for ((i, plan) in plans.withIndex()) {
                    val topName = WriteOps.nameOf(tops[i])
                    for (n in plan) {
                        c.checkCancel()
                        if (n.type != FileType.DIR && n.type != FileType.FILE) {
                            skipped++
                            continue
                        }
                        val entryName = topName + n.rel
                        val isDir = n.type == FileType.DIR
                        val json = JSONObject().put("name", entryName).put("type", if (isDir) "d" else "f")
                            .put("size", if (isDir) 0L else n.size).put("mtime", n.mtime).put("store", !isDir && stored(entryName))
                        EngineError.check(engine.arcwAdd(h, json.toString().toByteArray(Charsets.UTF_8)))
                        if (!isDir) addData(s, n, h, buf, WriteOps.nameOf(entryName))
                    }
                }
                val rc = engine.arcwClose(h, true)
                h = 0L
                EngineError.check(rc)
                val finalName = DocNames.unique(zipName) { s.stat(DocumentIds.child(dir, it)) != null }
                    ?: throw FileNotFoundException("Trop d'éléments portent déjà ce nom")
                s.rename(partial, finalName)
                created = null
            } finally {
                if (h > 0) runCatching { engine.arcwClose(h, false) }
                onClosed()
                created?.let { p -> if (Transfers.alive(s)) runCatching { s.delete(p) } }
            }
        }

        /** Données du fichier [n] dans l'entrée courante : exactement la taille annoncée, sinon le fichier a changé. */
        fun addData(s: FileSource, n: Transfers.Node, h: Long, buf: ByteArray, name: String) {
            c.st = c.st.copy(currentName = name, fileIndex = c.st.fileIndex + 1)
            c.publish(force = true)
            var done = 0L
            s.openRead(n.src).use { input ->
                while (true) {
                    c.checkCancel()
                    val k = input.read(buf)
                    if (k < 0) break
                    if (k == 0) continue
                    if (done + k > n.size) throw FilesException(EngineError.FL_EIO, MSG_CHANGED)
                    EngineError.check(engine.arcwWrite(h, buf, k))
                    done += k
                    c.st = c.st.copy(doneBytes = c.st.doneBytes + k)
                    c.publish()
                }
            }
            if (done != n.size) throw FilesException(EngineError.FL_EIO, MSG_CHANGED)
            c.publish(force = true)
        }
    }
}

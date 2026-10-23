package com.mawena.fluluks.files

import android.graphics.Bitmap
import android.graphics.ImageDecoder
import android.os.Build
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import java.io.ByteArrayOutputStream
import java.io.FileNotFoundException
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction

/**
 * Texte ouvert dans l'éditeur : [text] sans le BOM, fins de ligne en « \n » ; [bom] et [crlf] à rendre à
 * l'enregistrement ; [mtime] (secondes) et [size] permettent de voir s'il a changé ; [readOnlyReason] : message si le
 * stockage est en lecture seule.
 */
data class TextDoc(val text: String, val bom: Boolean, val crlf: Boolean, val mtime: Long, val size: Long, val readOnlyReason: String?)

/** Enregistrement d'un texte : [changed] si le fichier a changé depuis son ouverture (alors rien n'est écrit). */
data class TextSave(val saved: Boolean, val changed: Boolean, val mtime: Long, val size: Long)

/**
 * Contenu des fichiers pour les lecteurs de Portail (phase 10), en mémoire seulement (§ 11) : octets d'une image
 * (HEIC et HEIF convertis en JPEG par Android), texte UTF-8 de l'éditeur. Un texte s'enregistre dans un fichier
 * temporaire qui remplace ensuite l'original : un échec d'écriture laisse l'original intact.
 */
object FileContent {
    const val MAX_IMAGE = 64L shl 20
    const val MAX_TEXT = 2L shl 20
    const val MSG_IMAGE_TOO_BIG = "Image trop grande pour la visionneuse (64 Mio au plus)"
    const val MSG_TEXT_TOO_BIG = "Fichier trop gros pour l'éditeur (2 Mio au plus)"
    const val MSG_NOT_TEXT = "Ce fichier n'est pas du texte UTF-8"
    const val MSG_HEIC = "Format HEIC non pris en charge sur cette version d'Android"
    private val BOM = byteArrayOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte())

    /** Plus grand côté d'une image HEIC convertie. */
    private const val MAX_SIDE = 4096

    /** Tests JVM : conversion HEIC simulée ; null : ImageDecoder d'Android. */
    @Volatile var heicToJpeg: ((ByteArray) -> ByteArray)? = null

    /** Fichier ordinaire [path] de [src]. */
    private fun regular(src: FileSource, path: String): Entry {
        val e = src.stat(PathRules.require(path)) ?: throw FileNotFoundException("Document introuvable")
        if (e.type != FileType.FILE) throw FilesException(EngineError.FL_EISDIR, "Ce n'est pas un fichier")
        return e
    }

    /** Tout le fichier [e], refusé ([msg]) au-delà de [max] octets, même si sa taille a menti. */
    private fun readAll(src: FileSource, e: Entry, max: Long, msg: String): ByteArray {
        if (e.size > max) throw FilesException(EngineError.FL_EFBIG, msg)
        val out = ByteArrayOutputStream(e.size.toInt())
        src.openRead(e.path).use { input ->
            val buf = ByteArray(64 * 1024)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                out.write(buf, 0, n)
                if (out.size() > max) throw FilesException(EngineError.FL_EFBIG, msg)
            }
        }
        return out.toByteArray()
    }

    fun readImage(src: FileSource, path: String): ByteArray {
        val e = regular(src, path)
        val bytes = readAll(src, e, MAX_IMAGE, MSG_IMAGE_TOO_BIG)
        val ext = e.name.substringAfterLast('.', "").lowercase()
        if (ext != "heic" && ext != "heif") return bytes
        return (heicToJpeg ?: ::androidHeicToJpeg)(bytes)
    }

    private fun androidHeicToJpeg(bytes: ByteArray): ByteArray {
        if (Build.VERSION.SDK_INT < 28) throw FilesException(EngineError.FL_ENOTSUP, MSG_HEIC)
        val bmp = ImageDecoder.decodeBitmap(ImageDecoder.createSource(ByteBuffer.wrap(bytes))) { d, info, _ ->
            val w = info.size.width
            val h = info.size.height
            val scale = minOf(1.0, MAX_SIDE.toDouble() / maxOf(w, h))
            if (scale < 1.0) d.setTargetSize(maxOf(1, (w * scale).toInt()), maxOf(1, (h * scale).toInt()))
            d.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
        }
        try {
            return ByteArrayOutputStream().also { bmp.compress(Bitmap.CompressFormat.JPEG, 92, it) }.toByteArray()
        } finally {
            bmp.recycle()
        }
    }

    fun readText(src: FileSource, path: String): TextDoc {
        val e = regular(src, path)
        var b = readAll(src, e, MAX_TEXT, MSG_TEXT_TOO_BIG)
        val bom = b.size >= 3 && b[0] == BOM[0] && b[1] == BOM[1] && b[2] == BOM[2]
        if (bom) b = b.copyOfRange(3, b.size)
        if (b.any { it == 0.toByte() }) throw FilesException(EngineError.FL_EINVAL, MSG_NOT_TEXT)
        val text = try {
            Charsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(b)).toString()
        } catch (x: CharacterCodingException) {
            throw FilesException(EngineError.FL_EINVAL, MSG_NOT_TEXT)
        }
        val crlf = text.contains("\r\n")
        val ro = src.readOnlyReason?.let { Files.readOnlyMessage(src, it) }
        return TextDoc(if (crlf) text.replace("\r\n", "\n") else text, bom, crlf, e.mtime, e.size, ro)
    }

    /** Réécriture en place interrompue : où trouver le texte complet (fichier caché). */
    fun msgKeptIn(tmpName: String) =
        "Enregistrement interrompu : le texte complet est dans « $tmpName » (fichier caché, à afficher dans le menu)"

    /**
     * Enregistre [text] dans [path] : refusé ([TextSave.changed]) si la date ou la taille diffèrent de celles lues à
     * l'ouverture, sauf [force]. [crlf] : toutes les fins de ligne en CRLF ; [bom] : BOM UTF-8 en tête.
     */
    fun writeText(
        src: FileSource, path: String, text: String, bom: Boolean, crlf: Boolean, expectedMtime: Long, expectedSize: Long,
        force: Boolean,
    ): TextSave {
        src.readOnlyReason?.let { throw FileNotFoundException(Files.readOnlyMessage(src, it)) }
        val e = regular(src, path)
        if (!force && (e.mtime != expectedMtime || e.size != expectedSize)) return TextSave(false, true, e.mtime, e.size)
        val lf = text.replace("\r\n", "\n")
        val body = (if (crlf) lf.replace("\n", "\r\n") else lf).toByteArray(Charsets.UTF_8)
        val parent = WriteOps.parentOf(e.path)
        val tmp = Transfers.partialName(e.name)
        src.stat(DocumentIds.child(parent, tmp))?.let { if (it.type == FileType.FILE) src.delete(it.path) }
        val (tmpPath, out) = src.createFile(parent, tmp)
        var written = false
        try {
            out.use {
                if (bom) it.write(BOM)
                it.write(body)
            }
            written = true
        } finally {
            if (!written) runCatching { src.delete(tmpPath) }
        }
        // le texte complet est sur le stockage (place vérifiée) : l'original est réécrit en place, ce qui garde ses
        // droits, son propriétaire et ses liens ; s'il échoue, le texte reste entier dans le temporaire
        val size = (if (bom) BOM.size else 0) + body.size.toLong()
        try {
            src.openOverwrite(e.path).use {
                if (bom) it.write(BOM)
                it.write(body)
            }
            if (src.stat(e.path)?.size != size) throw java.io.IOException("taille")
        } catch (x: Exception) {
            throw FileNotFoundException(msgKeptIn(WriteOps.nameOf(tmpPath)))
        }
        runCatching { src.delete(tmpPath) }
        val n = src.stat(e.path) ?: throw FileNotFoundException("Document introuvable")
        return TextSave(true, false, n.mtime, n.size)
    }
}

package com.mawena.fluluks.media

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.PathRules
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.provider.WriteOps
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.Charset
import java.nio.charset.CodingErrorAction
import java.util.Base64

/** Sous-titre d'une vidéo : [path] dans la source, [language] (2 ou 3 lettres) ou null, [dataUri] pour le lecteur. */
data class SubtitleTrack(val path: String, val language: String?, val dataUri: String)

/**
 * Sous-titres SubRip à côté d'une vidéo (phase 10) : `<base>.srt` et `<base>.<langue>.srt` du même dossier, casse
 * ignorée, 2 Mio au plus. Lus en mémoire et donnés au lecteur en `data:` URI : rien n'est écrit sur le téléphone.
 */
object Subtitles {
    const val MAX = 2L shl 20
    private const val MIME = "application/x-subrip"
    private val LANG = Regex("[a-z]{2,3}")

    /**
     * Sous-titres de la vidéo [videoPath] : `<base>.srt` d'abord, puis les langues par nom. Un fichier trop grand ou
     * illisible est passé.
     */
    fun load(src: FileSource, videoPath: String): List<SubtitleTrack> {
        val p = PathRules.require(videoPath)
        val base = WriteOps.nameOf(p).substringBeforeLast('.').lowercase()
        val found = src.list(WriteOps.parentOf(p)).mapNotNull { e ->
            val n = e.name.lowercase()
            if (e.type != FileType.FILE || e.size > MAX || !n.endsWith(".srt") || !n.startsWith("$base.")) return@mapNotNull null
            val middle = n.removePrefix("$base.").removeSuffix(".srt")
            when {
                n == "$base.srt" -> e to null
                LANG.matches(middle) -> e to middle
                else -> null
            }
        }.sortedWith(compareBy({ it.second != null }, { it.first.name.lowercase() }))
        return found.mapNotNull { (e, lang) ->
            val path = DocumentIds.child(WriteOps.parentOf(p), e.name)
            runCatching {
                val bytes = src.openRead(path).use { readAtMost(it, MAX.toInt() + 1) }
                if (bytes.size > MAX) null else SubtitleTrack(path, lang, dataUri(decode(bytes)))
            }.getOrNull()
        }
    }

    /** Au plus [n] octets de [input] (readNBytes n'existe qu'à partir d'Android 13). */
    private fun readAtMost(input: java.io.InputStream, n: Int): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        val buf = ByteArray(64 * 1024)
        while (out.size() < n) {
            val k = input.read(buf, 0, minOf(buf.size, n - out.size()))
            if (k < 0) break
            out.write(buf, 0, k)
        }
        return out.toByteArray()
    }

    /** UTF-8 (BOM retiré) s'il est valide, sinon Windows-1252. */
    fun decode(bytes: ByteArray): String {
        val bom = bytes.size >= 3 && bytes[0] == 0xEF.toByte() && bytes[1] == 0xBB.toByte() && bytes[2] == 0xBF.toByte()
        val b = if (bom) ByteBuffer.wrap(bytes, 3, bytes.size - 3) else ByteBuffer.wrap(bytes)
        return try {
            Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).decode(b).toString()
        } catch (e: CharacterCodingException) {
            String(bytes, Charset.forName("windows-1252"))
        }
    }

    fun dataUri(text: String): String =
        "data:$MIME;base64," + Base64.getEncoder().encodeToString(text.toByteArray(Charsets.UTF_8))
}

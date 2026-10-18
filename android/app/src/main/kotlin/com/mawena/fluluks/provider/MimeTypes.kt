package com.mawena.fluluks.provider

import android.webkit.MimeTypeMap
import java.util.Locale

object MimeTypes {
    const val DEFAULT = "application/octet-stream"

    private val table = mapOf(
        "mkv" to "video/x-matroska", "mp4" to "video/mp4", "avi" to "video/x-msvideo", "webm" to "video/webm",
        "mp3" to "audio/mpeg", "flac" to "audio/flac", "ogg" to "audio/ogg", "m4a" to "audio/mp4",
        "jpg" to "image/jpeg", "jpeg" to "image/jpeg", "png" to "image/png", "heic" to "image/heic",
        "gif" to "image/gif", "webp" to "image/webp", "pdf" to "application/pdf", "txt" to "text/plain",
        "srt" to "application/x-subrip", "zip" to "application/zip", "epub" to "application/epub+zip",
    )

    /** Type MIME d'après l'extension (insensible à la casse) : MimeTypeMap, puis la table locale. */
    fun forName(name: String): String {
        val dot = name.lastIndexOf('.')
        if (dot <= 0 || dot == name.length - 1) return DEFAULT
        val ext = name.substring(dot + 1).lowercase(Locale.ROOT)
        val system = try {
            MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext)
        } catch (e: RuntimeException) { // stubs android.jar dans les tests JVM
            null
        }
        return system ?: table[ext] ?: DEFAULT
    }
}

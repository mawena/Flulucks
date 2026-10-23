package com.mawena.fluluks.files

import java.io.BufferedInputStream
import java.io.DataInputStream
import java.io.IOException
import java.io.InputStream
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/**
 * Lecture minimale de l'EXIF d'un JPEG (renommage par date de prise de vue) : segment APP1 « Exif », en-tête TIFF
 * (petit- ou gros-boutiste), IFD0 puis IFD Exif. Rend DateTimeOriginal, sinon DateTime d'IFD0. Toute incohérence
 * (fichier hostile ou tronqué) rend null, jamais une erreur. Les autres formats (HEIC…) ne sont pas lus.
 */
object Exif {
    private const val TAG_EXIF_IFD = 0x8769
    private const val TAG_DATE_TIME = 0x0132
    private const val TAG_DATE_TIME_ORIGINAL = 0x9003
    private const val MAX_SEGMENT = 64 * 1024
    private val FORMAT = DateTimeFormatter.ofPattern("yyyy:MM:dd HH:mm:ss")

    fun dateTimeOriginal(input: InputStream): LocalDateTime? = try {
        readApp1(DataInputStream(BufferedInputStream(input)))?.let(::dateFromTiff)?.let(::parse)
    } catch (e: IOException) {
        null
    }

    /** Charge utile TIFF du segment APP1 « Exif », ou null (pas un JPEG, pas d'EXIF avant les données d'image). */
    private fun readApp1(ins: DataInputStream): ByteArray? {
        if (ins.readUnsignedShort() != 0xFFD8) return null
        while (true) {
            if (ins.readUnsignedByte() != 0xFF) return null
            var marker = ins.readUnsignedByte()
            while (marker == 0xFF) marker = ins.readUnsignedByte() // bourrage
            if (marker == 0xD9 || marker == 0xDA) return null // fin, ou début des données d'image
            if (marker in 0xD0..0xD7 || marker == 0x01) continue // marqueurs sans longueur
            val len = ins.readUnsignedShort() - 2
            if (len < 0) return null
            if (marker == 0xE1 && len in 8..MAX_SEGMENT) {
                val seg = ByteArray(len)
                ins.readFully(seg)
                if (String(seg, 0, 6, Charsets.ISO_8859_1) == "Exif\u0000\u0000") return seg.copyOfRange(6, len)
            } else {
                skipFully(ins, len.toLong())
            }
        }
    }

    private fun skipFully(ins: InputStream, n: Long) {
        var left = n
        while (left > 0) {
            val k = ins.skip(left)
            if (k <= 0) {
                if (ins.read() < 0) throw IOException("fin du fichier")
                left--
            } else {
                left -= k
            }
        }
    }

    private class Tiff(val b: ByteArray, val big: Boolean) {
        fun u16(o: Int): Int? = if (o < 0 || o + 2 > b.size) null else {
            val x = b[o].toInt() and 0xFF
            val y = b[o + 1].toInt() and 0xFF
            if (big) x shl 8 or y else y shl 8 or x
        }

        fun u32(o: Int): Long? {
            val hi = u16(if (big) o else o + 2) ?: return null
            val lo = u16(if (big) o + 2 else o) ?: return null
            return hi.toLong() shl 16 or lo.toLong()
        }

        /** Entrée [tag] de l'IFD à [ifd] : (type, nombre, valeur ou décalage), ou null. */
        fun entry(ifd: Int, tag: Int): Triple<Int, Long, Long>? {
            val n = u16(ifd) ?: return null
            for (i in 0 until n) {
                val e = ifd + 2 + 12 * i
                if (u16(e) != tag) continue
                return Triple(u16(e + 2) ?: return null, u32(e + 4) ?: return null, u32(e + 8) ?: return null)
            }
            return null
        }

        /** Chaîne ASCII (type 2) de [tag] dans l'IFD à [ifd]. */
        fun ascii(ifd: Int, tag: Int): String? {
            val (type, count, value) = entry(ifd, tag) ?: return null
            if (type != 2 || count !in 1..64) return null
            val start = if (count <= 4) -1 else value.toInt()
            if (start < 0 || start + count > b.size) return null
            return String(b, start, count.toInt(), Charsets.ISO_8859_1).trimEnd('\u0000', ' ')
        }
    }

    private fun dateFromTiff(b: ByteArray): String? {
        if (b.size < 8) return null
        val big = when (String(b, 0, 2, Charsets.ISO_8859_1)) {
            "MM" -> true
            "II" -> false
            else -> return null
        }
        val t = Tiff(b, big)
        if (t.u16(2) != 42) return null
        val ifd0 = t.u32(4)?.toInt() ?: return null
        val exif = t.entry(ifd0, TAG_EXIF_IFD)?.third?.toInt()
        return exif?.let { t.ascii(it, TAG_DATE_TIME_ORIGINAL) } ?: t.ascii(ifd0, TAG_DATE_TIME)
    }

    private fun parse(s: String): LocalDateTime? = try {
        LocalDateTime.parse(s, FORMAT)
    } catch (e: Exception) {
        null // « 0000:00:00 00:00:00 » et autres valeurs inventées
    }
}

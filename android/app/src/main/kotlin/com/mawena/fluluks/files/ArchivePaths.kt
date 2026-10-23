package com.mawena.fluluks.files

import com.mawena.fluluks.provider.DocNames
import com.mawena.fluluks.provider.WriteOps
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction

/**
 * Noms des entrées d'une archive (phase 9). Le moteur rend les octets bruts du nom : UTF-8 s'il est valide,
 * sinon page de code 437 pour un ZIP (Windows, sans le drapeau UTF-8) ou ISO-8859-1 pour les autres formats.
 * Le chemin est ensuite réduit à un chemin relatif sûr, ou refusé : rien ne sort jamais de la destination.
 */
object ArchivePaths {
    /** Caractères des octets 0x80 à 0xFF de la page de code 437. */
    private const val CP437_HIGH =
        "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐" +
            "└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ "

    fun decodeName(raw: ByteArray, zip: Boolean): String {
        val utf8 = Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
        try {
            return utf8.decode(ByteBuffer.wrap(raw)).toString()
        } catch (e: CharacterCodingException) {
            // pas de l'UTF-8 : page de code d'origine
        }
        if (!zip) return String(raw, Charsets.ISO_8859_1)
        val sb = StringBuilder(raw.size)
        for (b in raw) {
            val u = b.toInt() and 0xFF
            sb.append(if (u < 0x80) u.toChar() else CP437_HIGH[u - 0x80])
        }
        return sb.toString()
    }

    /**
     * Chemin relatif sûr de l'entrée [name] (segments séparés par « / », sans « / » au début ni à la fin), ou
     * null : chemin absolu, segment « .. », NUL, segment invalide (plus de 255 octets) ou profondeur au-delà de
     * [WriteOps.MAX_DEPTH]. Les segments vides et « . » sont retirés ; « \\ » reste un caractère du nom.
     */
    fun safeRelative(name: String): String? {
        if (name.isEmpty() || name.startsWith("/") || name.indexOf('\u0000') >= 0) return null
        val parts = name.split('/').filter { it.isNotEmpty() && it != "." }
        if (parts.isEmpty() || parts.size > WriteOps.MAX_DEPTH) return null
        if (parts.any { !DocNames.isValid(it) }) return null // « .. » compris
        return parts.joinToString("/")
    }
}

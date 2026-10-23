package com.mawena.fluluks

import com.mawena.fluluks.files.ArchivePaths
import com.mawena.fluluks.provider.WriteOps
import java.nio.charset.Charset
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assume.assumeTrue
import org.junit.Test

/** Noms des entrées d'archive : décodage des octets bruts, chemin gardé sous la destination. */
class ArchivePathsTest {
    @Test fun safeRelativeRules() {
        assertEquals("a/b", ArchivePaths.safeRelative("a/b"))
        assertEquals("a/b", ArchivePaths.safeRelative("a/./b"))
        assertEquals("a/b", ArchivePaths.safeRelative("a//b/"))
        assertEquals("dossier", ArchivePaths.safeRelative("dossier/"))
        assertEquals("été/a.bin", ArchivePaths.safeRelative("./été/a.bin"))
        // « \ » est un caractère du nom (assaini plus tard si la destination l'exige)
        assertEquals("a\\b", ArchivePaths.safeRelative("a\\b"))
        assertEquals("..a/b..", ArchivePaths.safeRelative("..a/b.."))
        for (bad in listOf("../x", "a/../../x", "a/../b", "..", "/x", "/etc/passwd", "", "/", ".", "./", "a/\u0000b")) {
            assertNull(bad, ArchivePaths.safeRelative(bad))
        }
        assertNull(ArchivePaths.safeRelative("a/" + "é".repeat(128))) // 256 octets : nom trop long
        val deep = List(WriteOps.MAX_DEPTH) { "d" }
        assertEquals(deep.joinToString("/"), ArchivePaths.safeRelative(deep.joinToString("/")))
        assertNull(ArchivePaths.safeRelative((deep + "d").joinToString("/")))
    }

    @Test fun cp437NamesDecoded() {
        val raw = byteArrayOf(0x82.toByte()) + ".txt".toByteArray()
        assertEquals("é.txt", ArchivePaths.decodeName(raw, zip = true))
        assertEquals("\u0082.txt", ArchivePaths.decodeName(raw, zip = false)) // ISO-8859-1 hors ZIP
        assertEquals("été/ü.txt", ArchivePaths.decodeName("été/ü.txt".toByteArray(), zip = true)) // UTF-8 valide gardé
        assertEquals("été", ArchivePaths.decodeName("été".toByteArray(), zip = false))
        // toute la moitié haute de la page 437, comparée à la table de la JVM
        assumeTrue(Charset.isSupported("IBM437"))
        val high = ByteArray(128) { (0x80 + it).toByte() }
        assertEquals(String(high, Charset.forName("IBM437")), ArchivePaths.decodeName(high, zip = true))
    }
}

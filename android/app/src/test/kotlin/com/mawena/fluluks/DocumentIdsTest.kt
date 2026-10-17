package com.mawena.fluluks

import com.mawena.fluluks.provider.DocumentIds
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DocumentIdsTest {
    @Test fun roundTrip() {
        assertEquals("v1:/a/b.txt", DocumentIds.encode("v1", "/a/b.txt"))
        assertEquals("v1" to "/a/b.txt", DocumentIds.decode("v1:/a/b.txt"))
        assertEquals("v1" to "/", DocumentIds.decode(DocumentIds.encode("v1", "/")))
        assertEquals("v2" to "/dossier/accentué é ☀.txt", DocumentIds.decode("v2:/dossier/accentué é ☀.txt"))
        assertEquals("v1" to "/a:b", DocumentIds.decode("v1:/a:b")) // seul le premier « : » sépare
    }

    @Test fun rejectsMalformed() {
        for (bad in listOf(
            "v1:../x", "v1:relatif", "v1:/a//b", "pasdedeuxpoints", "", ":/x", "v1:", "v1:/a/../b",
            "v1:/a/./b", "v1:/a/", "v1://", "v1:/a\u0000b", "v1\u0000:/a", "v1:/..", "v1:/.",
        )) assertNull("devrait être rejeté : $bad", DocumentIds.decode(bad))
    }

    @Test fun rejectsTooLongPath() {
        val ok = "/" + "a".repeat(4094) // 4095 octets
        assertEquals("v1" to ok, DocumentIds.decode("v1:$ok"))
        assertNull(DocumentIds.decode("v1:" + ok + "a"))
        assertNull(DocumentIds.decode("v1:/" + "é".repeat(2048))) // 4097 octets en UTF-8
    }

    @Test fun isDescendantOnWholeComponents() {
        assertTrue(DocumentIds.isDescendant("/", "/a"))
        assertTrue(DocumentIds.isDescendant("/a", "/a/b/c"))
        assertTrue(!DocumentIds.isDescendant("/a", "/a"))
        assertTrue(!DocumentIds.isDescendant("/a", "/ab"))
        assertTrue(!DocumentIds.isDescendant("/a/b", "/a"))
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.files.BatchRename
import com.mawena.fluluks.files.Exif
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.RenameRule
import java.io.ByteArrayOutputStream
import java.io.File
import java.time.LocalDateTime
import java.time.ZoneId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Renommage par lot sur une source locale (dossier temporaire). */
class BatchRenameTest {
    @get:Rule val tmp = TemporaryFolder()

    private val root: File get() = tmp.root
    private fun src(restricted: Boolean = false) = LocalSource(if (restricted) "sd:x" else "phone", "phone", "Téléphone", root) { true }
    private fun local(path: String, bytes: ByteArray = path.toByteArray()) =
        File(root, path).apply { parentFile!!.mkdirs(); writeBytes(bytes) }
    private fun names() = root.list()!!.sorted()

    /** JPEG minimal : SOI, APP1 « Exif » (IFD0 → IFD Exif → DateTimeOriginal), EOI. */
    private fun jpeg(date: String, bigEndian: Boolean): ByteArray {
        val tiff = ByteArrayOutputStream()
        fun u16(v: Int) = if (bigEndian) tiff.write(byteArrayOf((v shr 8).toByte(), v.toByte())) else tiff.write(byteArrayOf(v.toByte(), (v shr 8).toByte()))
        fun u32(v: Int) = if (bigEndian) {
            tiff.write(byteArrayOf((v shr 24).toByte(), (v shr 16).toByte(), (v shr 8).toByte(), v.toByte()))
        } else {
            tiff.write(byteArrayOf(v.toByte(), (v shr 8).toByte(), (v shr 16).toByte(), (v shr 24).toByte()))
        }
        tiff.write((if (bigEndian) "MM" else "II").toByteArray()); u16(42); u32(8)
        // IFD0 (offset 8) : une entrée, pointeur vers l'IFD Exif à 26
        u16(1); u16(0x8769); u16(4); u32(1); u32(26); u32(0)
        // IFD Exif (offset 26) : DateTimeOriginal, ASCII, 20 octets à 44
        u16(1); u16(0x9003); u16(2); u32(20); u32(44); u32(0)
        tiff.write((date + "\u0000").toByteArray())
        val app1 = "Exif\u0000\u0000".toByteArray() + tiff.toByteArray()
        val out = ByteArrayOutputStream()
        out.write(byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte(), 0xE1.toByte()))
        val len = app1.size + 2
        out.write(byteArrayOf((len shr 8).toByte(), len.toByte()))
        out.write(app1)
        out.write(byteArrayOf(0xFF.toByte(), 0xD9.toByte()))
        return out.toByteArray()
    }

    @Test fun numberingKeepsExtension() {
        local("b.png"); local("a.jpg"); local("c")
        val s = src()
        val rule = RenameRule(mode = "number", base = "Vacances", start = 1)
        val p = BatchRename.preview(s, listOf("/a.jpg", "/b.png", "/c"), rule)
        assertEquals(listOf("Vacances (01).jpg", "Vacances (02).png", "Vacances (03)"), p.map { it.newName })
        assertTrue(p.all { it.conflict == null })
        BatchRename.apply(s, listOf("/a.jpg", "/b.png", "/c"), rule)
        assertEquals(listOf("Vacances (01).jpg", "Vacances (02).png", "Vacances (03)"), names())
        assertEquals("a.jpg", File(root, "Vacances (01).jpg").readText()) // contenus suivis
        // au moins deux chiffres, plus si le lot le demande
        val many = (1..120).map { local("f$it.txt"); "/f$it.txt" }
        assertEquals("Lot (098).txt", BatchRename.preview(s, many, RenameRule(mode = "number", base = "Lot", start = 1))[97].newName)
    }

    @Test fun replaceCaseInsensitive() {
        local("IMG_001.jpg"); local("img_002.JPG"); local("autre.txt")
        val s = src()
        val p = BatchRename.preview(s, listOf("/IMG_001.jpg", "/img_002.JPG", "/autre.txt"), RenameRule(mode = "replace", find = "img_", replace = "Photo "))
        assertEquals(listOf("Photo 001.jpg", "Photo 002.JPG", "autre.txt"), p.map { it.newName })
        val cs = BatchRename.preview(s, listOf("/IMG_001.jpg", "/img_002.JPG"), RenameRule(mode = "replace", find = "img_", replace = "x", caseSensitive = true))
        assertEquals(listOf("IMG_001.jpg", "x002.JPG"), cs.map { it.newName })
        // l'extension n'est jamais touchée
        assertEquals("IMG_001.jpg", BatchRename.preview(s, listOf("/IMG_001.jpg"), RenameRule(mode = "replace", find = "jpg", replace = "png"))[0].newName)
    }

    @Test fun photoDateFromExif() {
        local("p1.jpg", jpeg("2024:05:01 12:34:56", bigEndian = false))
        local("p2.jpeg", jpeg("2023:12:31 23:59:59", bigEndian = true))
        assertEquals(LocalDateTime.of(2024, 5, 1, 12, 34, 56), Exif.dateTimeOriginal(File(root, "p1.jpg").readBytes().inputStream()))
        val p = BatchRename.preview(src(), listOf("/p1.jpg", "/p2.jpeg"), RenameRule(mode = "photoDate"))
        assertEquals(listOf("2024-05-01 12.34.56.jpg", "2023-12-31 23.59.59.jpeg"), p.map { it.newName })
    }

    @Test fun photoDateFallsBackToMtime() {
        val f = local("sans-exif.jpg", byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte(), 0xD9.toByte()))
        val g = local("note.txt")
        val t = LocalDateTime.of(2022, 2, 3, 4, 5, 6)
        val ms = t.atZone(ZoneId.systemDefault()).toInstant().toEpochMilli()
        f.setLastModified(ms); g.setLastModified(ms + 1000)
        assertNull(Exif.dateTimeOriginal(f.readBytes().inputStream()))
        val p = BatchRename.preview(src(), listOf("/sans-exif.jpg", "/note.txt"), RenameRule(mode = "photoDate"))
        assertEquals(listOf("2022-02-03 04.05.06.jpg", "2022-02-03 04.05.07.txt"), p.map { it.newName })
    }

    @Test fun conflictWithExistingRefusedBeforeAnyRename() {
        local("a.txt"); local("b.txt"); local("Lot (02).txt", "occupé".toByteArray())
        val s = src()
        val rule = RenameRule(mode = "number", base = "Lot")
        val p = BatchRename.preview(s, listOf("/a.txt", "/b.txt"), rule)
        assertNull(p[0].conflict)
        assertEquals(BatchRename.CONFLICT_EXISTS, p[1].conflict)
        try {
            BatchRename.apply(s, listOf("/a.txt", "/b.txt"), rule)
            throw AssertionError("accepté")
        } catch (e: FilesException) {
            assertEquals(BatchRename.MSG_CONFLICTS, e.message)
        }
        assertEquals(listOf("Lot (02).txt", "a.txt", "b.txt"), names()) // rien n'a bougé
    }

    @Test fun duplicatesInBatchFlagged() {
        local("x1.txt"); local("x2.txt")
        val p = BatchRename.preview(src(), listOf("/x1.txt", "/x2.txt"), RenameRule(mode = "replace", find = "x1", replace = "x2"))
        // x1 → x2 alors que x2 reste x2 : doublon
        assertEquals(listOf("x2.txt", "x2.txt"), p.map { it.newName })
        assertEquals(BatchRename.CONFLICT_DUPLICATE, p[0].conflict)
    }

    @Test fun swapNamesWorks() {
        local("a.txt", "A".toByteArray()); local("b.txt", "B".toByteArray())
        val s = src()
        // a → b, b → a en un seul lot (par des noms temporaires)
        val swap = BatchRename.planned(listOf("/a.txt" to "b.txt", "/b.txt" to "a.txt"))
        assertTrue(BatchRename.check(s, swap).all { it.conflict == null })
        BatchRename.applyPlanned(s, swap)
        assertEquals("B", File(root, "a.txt").readText())
        assertEquals("A", File(root, "b.txt").readText())
        assertEquals(listOf("a.txt", "b.txt"), names())
    }

    @Test fun invalidNameForFatRefused() {
        local("a.txt")
        val fat = src(restricted = true)
        val p = BatchRename.preview(fat, listOf("/a.txt"), RenameRule(mode = "replace", find = "a", replace = "x:y"))
        assertEquals(BatchRename.CONFLICT_INVALID, p.single().conflict)
        // téléphone : « : » permis, « / » jamais
        assertNull(BatchRename.preview(src(), listOf("/a.txt"), RenameRule(mode = "replace", find = "a", replace = "x:y")).single().conflict)
        assertEquals(BatchRename.CONFLICT_INVALID, BatchRename.preview(src(), listOf("/a.txt"), RenameRule(mode = "replace", find = "a", replace = "x/y")).single().conflict)
    }
}

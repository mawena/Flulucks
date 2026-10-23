package com.mawena.fluluks

import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileDetails
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.volumes.VolumeManager
import java.io.File
import java.security.MessageDigest
import java.util.Random
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Propriétés d'un élément : empreinte SHA-256, taille d'un dossier, droits ext4. */
class DetailsTest {
    @get:Rule val tmp = TemporaryFolder()

    private val root: File get() = tmp.root
    private fun src() = LocalSource("phone", "phone", "Téléphone", root) { true }
    private fun local(path: String, bytes: ByteArray) = File(root, path).apply { parentFile!!.mkdirs(); writeBytes(bytes) }

    @Test fun sha256KnownVector() {
        local("abc.txt", "abc".toByteArray())
        local("vide", ByteArray(0))
        val big = ByteArray(3 * (1 shl 20) + 17).also { Random(7).nextBytes(it) }
        local("gros.bin", big)
        assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", FileDetails.sha256(src(), "/abc.txt") {})
        assertEquals("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", FileDetails.sha256(src(), "/vide") {})
        val expected = MessageDigest.getInstance("SHA-256").digest(big).joinToString("") { "%02x".format(it) }
        val seen = ArrayList<Int>()
        assertEquals(expected, FileDetails.sha256(src(), "/gros.bin") { seen.add(it) })
        assertEquals(seen.sorted(), seen) // croissante
        assertEquals(100, seen.last())
    }

    @Test fun sha256Cancel() {
        local("gros.bin", ByteArray(3 shl 20))
        var calls = 0
        assertNull(FileDetails.sha256(src(), "/gros.bin") { calls++; FileDetails.cancel() })
        assertEquals(1, calls)
        // la suivante n'est pas annulée
        assertEquals(64, FileDetails.sha256(src(), "/gros.bin") {}!!.length)
    }

    @Test fun modeString() {
        assertEquals("rwxr-xr-x", FileDetails.modeString(0b111_101_101))
        assertEquals("rw-r--r--", FileDetails.modeString(0x81A4)) // 0100644 : bits de type ignorés
        assertEquals("rwsr-xr-x", FileDetails.modeString(0x9ED)) // 04755
        assertEquals("rwxr-sr-x", FileDetails.modeString(0x5ED)) // 02755
        assertEquals("rwxrwxrwt", FileDetails.modeString(0x3FF)) // 01777
        assertEquals("rw-r-Sr-T", FileDetails.modeString(0x7A4)) // 03644 : sans x, S et T majuscules
        assertEquals("---------", FileDetails.modeString(0))
    }

    @Test fun folderSizeCounts() {
        local("D/a.bin", ByteArray(100)); local("D/E/b.bin", ByteArray(250)); local("D/.cache/c", ByteArray(5))
        File(root, "D/vide").mkdirs()
        val s = FileDetails.folderSize(src(), "/D")
        assertEquals(355L, s.size); assertEquals(3, s.files); assertEquals(3, s.dirs); assertFalse(s.truncated)
    }

    @Test fun localDetailsHaveNoOwner() {
        local("a.txt", "abc".toByteArray())
        val d = src().details("/a.txt")!!
        assertEquals("a.txt", d.name); assertEquals(FileType.FILE, d.type); assertEquals(3L, d.size)
        assertNull(d.mode); assertNull(d.uid); assertNull(d.gid)
        assertNull(src().details("/absent"))
    }

    @Test fun diskDetailsCarryOwnerOnExt4() {
        val e = MemEngine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        val diskId = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        val volId = runBlocking { VolumeManager.mountPlain(diskId, 1, true) }.id
        val disk = DiskSource(volId, e)
        disk.createFile("/", "a.txt").second.use { it.write("abc".toByteArray()) }
        e.owner = Triple(0x81A4, 1000L, 1001L)
        var d = disk.details("/a.txt")!!
        assertEquals("ext4", d.fs)
        assertEquals(0x81A4, d.mode); assertEquals(1000L, d.uid); assertEquals(1001L, d.gid)
        assertEquals("rw-r--r--", d.permissions)
        // FAT, exFAT, NTFS : droits et propriétaire sans objet
        e.fs = "exfat"
        d = disk.details("/a.txt")!!
        assertEquals("exfat", d.fs)
        assertNull(d.mode); assertNull(d.uid); assertNull(d.permissions)
    }
}

package com.mawena.fluluks

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.engine.DirEntry
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.EngineError.Companion.FL_EBADPASS
import com.mawena.fluluks.engine.EngineError.Companion.FL_EBUSY
import com.mawena.fluluks.engine.EngineError.Companion.FL_EINVAL
import com.mawena.fluluks.engine.EngineError.Companion.FL_EEXIST
import com.mawena.fluluks.engine.EngineError.Companion.FL_ENOENT
import com.mawena.fluluks.engine.EngineError.Companion.FL_ENOTEMPTY
import com.mawena.fluluks.engine.EngineError.Companion.FL_EROFS
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.engine.FsStat
import com.mawena.fluluks.engine.NativeEngine
import com.mawena.fluluks.engine.OpenFlags
import com.mawena.fluluks.engine.ScanResult
import com.mawena.fluluks.engine.VolumeStat
import java.io.File
import java.util.Random
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class EngineTest {
    private fun utf8(s: String) = s.toByteArray(Charsets.UTF_8)

    private fun open(f: File): Long = EngineError.check(NativeEngine.openImage(utf8(f.absolutePath)))

    /** Ouvre l'image, monte la partition, exécute `block`, puis ferme le disque dans tous les cas. */
    private fun <T> withVolume(image: String, part: Int, passFile: String?, block: (disk: Long, vol: Long) -> T): T {
        val disk = open(Fixtures.file(image))
        try {
            val vol = EngineError.check(NativeEngine.mount(disk, part, passFile?.let { Fixtures.bytes(it) }))
            return block(disk, vol)
        } finally {
            NativeEngine.closeDisk(disk)
        }
    }

    private fun readAll(file: Long, offset: Long, len: Int): ByteArray {
        val buf = ByteArray(len)
        var done = 0
        while (done < len) {
            val chunk = ByteArray(len - done)
            val n = NativeEngine.pread(file, offset + done, chunk, chunk.size)
            if (n < 0) throw EngineError(n)
            if (n == 0) break
            System.arraycopy(chunk, 0, buf, done, n)
            done += n
        }
        return buf.copyOf(done)
    }

    private fun pattern(offset: Long, len: Int) = ByteArray(len) { (((offset + it) * 31 + 7) and 0xFF).toByte() }

    @Test
    fun versionLoads() {
        val v = NativeEngine.version()
        assertTrue("version inattendue : $v", v.startsWith("fluluks "))
    }

    @Test
    fun strerrorIsFrench() {
        assertEquals("phrase de passe incorrecte", EngineError(FL_EBADPASS).message)
        assertEquals("volume occupé", EngineError(FL_EBUSY).message)
    }

    @Test
    fun scanGptLuks2() {
        val disk = open(Fixtures.file("gpt-luks2.img"))
        try {
            val s = ScanResult.parse(NativeEngine.scan(disk))
            assertEquals(512, s.sectorSize)
            assertEquals(Fixtures.file("gpt-luks2.img").length(), s.sizeBytes)
            assertEquals("gpt", s.table)
            assertEquals(1, s.parts.size)
            val p = s.parts[0]
            assertEquals(1, p.index)
            assertEquals(2048L, p.firstLba)
            assertEquals("LUKS2", p.content)
            assertTrue(p.supported)
            assertNull(p.error)
            assertNotNull(p.luks)
            val l = p.luks!!
            assertEquals(2, l.version)
            assertEquals("aes-xts-plain64", l.cipher)
            assertFalse(l.primaryDamaged)
            assertEquals(65536L, l.maxKdfMemoryKib)
        } finally {
            assertEquals(0, NativeEngine.closeDisk(disk))
        }
        // poignée fermée : JSON d'erreur
        try {
            ScanResult.parse(NativeEngine.scan(disk))
            fail("scan sur un disque fermé")
        } catch (e: EngineError) {
            assertEquals(FL_EINVAL, e.code)
        }
    }

    @Test
    fun scanDamagedPrimary() {
        val disk = open(Fixtures.damagedPrimary("whole-luks2-4k.img"))
        try {
            val s = ScanResult.parse(NativeEngine.scan(disk))
            assertEquals("none", s.table)
            val p = s.parts.single()
            assertEquals("LUKS2", p.content)
            assertTrue(p.luks!!.primaryDamaged)
            assertEquals(32768L, p.luks!!.maxKdfMemoryKib)
            // et le volume s'ouvre grâce à l'en-tête secondaire
            val vol = EngineError.check(NativeEngine.mount(disk, 0, Fixtures.bytes("pass.txt")))
            assertTrue(DirEntry.parseList(NativeEngine.list(vol, utf8("/"))).any { it.name == "hello.txt" })
        } finally {
            NativeEngine.closeDisk(disk)
        }
    }

    @Test
    fun unlockAndRead() = withVolume("gpt-luks2.img", 1, "pass.txt") { _, vol ->
        val entries = DirEntry.parseList(NativeEngine.list(vol, utf8("/")))
        val hello = entries.firstOrNull { it.name == "hello.txt" }
        assertNotNull("hello.txt absent : $entries", hello)
        assertEquals(FileType.FILE, hello!!.type)
        assertEquals(16L, hello.size)
        assertEquals(entries.map { it.name }, entries.map { it.name }.sortedWith { a, b -> compareUtf8(a, b) })
        val sd = entries.first { it.name == "sous-dossier" }
        assertEquals(FileType.DIR, sd.type)

        val f = EngineError.check(NativeEngine.openFile(vol, utf8("/hello.txt")))
        assertEquals("Bonjour FluLuks\n", String(readAll(f, 0, 64), Charsets.UTF_8))
        assertEquals(0, NativeEngine.pread(f, 16, ByteArray(8), 8))
        assertEquals(0, NativeEngine.closeFile(f))

        val st = VolumeStat.parse(NativeEngine.statfs(vol))
        assertEquals("FLUTEST", st.label)
        assertTrue(st.readOnly)
        assertTrue(st.totalBytes > st.freeBytes)
        assertFalse(st.needsRecovery)
        assertEquals(0, NativeEngine.unmount(vol, false))
    }

    @Test
    fun wrongPassphrase() {
        val disk = open(Fixtures.file("gpt-luks2.img"))
        try {
            assertEquals(FL_EBADPASS.toLong(), NativeEngine.mount(disk, 1, utf8("pas la bonne")))
            val vol = NativeEngine.mount(disk, 1, Fixtures.bytes("pass.txt"))
            assertTrue("mount : $vol", vol > 0)
            assertEquals(16L, FsStat.parse(NativeEngine.stat(vol, utf8("/hello.txt"))).size)
        } finally {
            assertEquals(0, NativeEngine.closeDisk(disk))
        }
    }

    @Test
    fun passIsZeroed() {
        val disk = open(Fixtures.file("gpt-luks2.img"))
        try {
            val pass = Fixtures.bytes("pass.txt")
            assertTrue(pass.any { it != 0.toByte() })
            assertTrue(NativeEngine.mount(disk, 1, pass) > 0)
            assertArrayEquals(ByteArray(pass.size), pass)
            // aussi sur chaque chemin d'erreur
            val wrong = utf8("mauvaise phrase")
            assertEquals(FL_EBADPASS.toLong(), NativeEngine.mount(disk, 1, wrong))
            assertArrayEquals(ByteArray(wrong.size), wrong)
            val badHandle = Fixtures.bytes("pass.txt")
            assertEquals(FL_EINVAL.toLong(), NativeEngine.mount(-1L, 1, badHandle))
            assertArrayEquals(ByteArray(badHandle.size), badHandle)
            val badPart = Fixtures.bytes("pass.txt")
            assertEquals(FL_EINVAL.toLong(), NativeEngine.mount(disk, 42, badPart))
            assertArrayEquals(ByteArray(badPart.size), badPart)
            val huge = ByteArray(100_000) { 1 }
            assertTrue(NativeEngine.mount(disk, 1, huge) < 0)
            assertArrayEquals(ByteArray(huge.size), huge)
        } finally {
            NativeEngine.closeDisk(disk)
        }
    }

    @Test
    fun emojiName() = withVolume("ext4-plain.img", 0, null) { _, vol ->
        val name = "accentué é ☀.txt"
        val entries = DirEntry.parseList(NativeEngine.list(vol, utf8("/")))
        assertTrue("$name absent : ${entries.map { it.name }}", entries.any { it.name == name })
        val st = FsStat.parse(NativeEngine.stat(vol, utf8("/$name")))
        assertEquals(name, st.name)
        assertEquals(5L, st.size)
        val f = EngineError.check(NativeEngine.openFile(vol, utf8("/$name")))
        assertEquals("utf8\n", String(readAll(f, 0, 16), Charsets.UTF_8))
        NativeEngine.closeFile(f)
        try {
            FsStat.parse(NativeEngine.stat(vol, utf8("/absent ☀")))
            fail("stat d'un fichier absent")
        } catch (e: EngineError) {
            assertEquals(FL_ENOENT, e.code)
        }
    }

    @Test
    fun parallelReads() = withVolume("gpt-luks2.img", 1, "pass.txt") { _, vol ->
        val size = 5L shl 20
        val shared = EngineError.check(NativeEngine.openFile(vol, utf8("/gros.bin")))
        val failure = AtomicReference<Throwable?>(null)
        val reads = AtomicInteger()
        val threads = (0 until 8).map { t ->
            Thread {
                try {
                    val own = if (t % 2 == 0) EngineError.check(NativeEngine.openFile(vol, utf8("/gros.bin"))) else shared
                    val rnd = Random(42L + t)
                    repeat(60) {
                        val off = (rnd.nextDouble() * size).toLong()
                        val len = 1 + rnd.nextInt(128 * 1024)
                        val got = readAll(own, off, len)
                        assertEquals(minOf(len.toLong(), size - off).toInt(), got.size)
                        assertArrayEquals(pattern(off, got.size), got)
                        reads.incrementAndGet()
                    }
                    if (own != shared) assertEquals(0, NativeEngine.closeFile(own))
                } catch (e: Throwable) {
                    failure.compareAndSet(null, e)
                }
            }.apply { start() }
        }
        threads.forEach { it.join() }
        failure.get()?.let { throw it }
        assertEquals(8 * 60, reads.get())
        assertEquals(0, NativeEngine.closeFile(shared))
    }

    @Test
    fun staleHandle() = withVolume("ext4-plain.img", 0, null) { disk, vol ->
        val buf = ByteArray(16)
        val f = EngineError.check(NativeEngine.openFile(vol, utf8("/hello.txt")))
        assertEquals(16, NativeEngine.pread(f, 0, buf, 16))
        assertEquals(0, NativeEngine.closeFile(f))
        assertEquals(FL_EINVAL, NativeEngine.pread(f, 0, buf, 16))
        assertEquals(FL_EINVAL, NativeEngine.closeFile(f))
        // mauvais type, valeurs absurdes, arguments hors bornes
        assertEquals(FL_EINVAL, NativeEngine.pread(vol, 0, buf, 16))
        assertEquals(FL_EINVAL, NativeEngine.pread(disk, 0, buf, 16))
        assertEquals(FL_EINVAL, NativeEngine.pread(0, 0, buf, 16))
        assertEquals(FL_EINVAL, NativeEngine.pread(Long.MAX_VALUE, 0, buf, 16))
        val g = EngineError.check(NativeEngine.openFile(vol, utf8("/hello.txt")))
        assertEquals(FL_EINVAL, NativeEngine.pread(g, 0, buf, 17))
        assertEquals(FL_EINVAL, NativeEngine.pread(g, -1, buf, 16))
        assertEquals(FL_EINVAL, NativeEngine.pread(g, 0, buf, -1))
        // après un démontage forcé
        assertEquals(0, NativeEngine.unmount(vol, true))
        assertEquals(FL_EINVAL, NativeEngine.pread(g, 0, buf, 16))
        assertEquals(FL_EINVAL, NativeEngine.closeFile(g))
        assertEquals(FL_EINVAL, NativeEngine.unmount(vol, true))
        try {
            DirEntry.parseList(NativeEngine.list(vol, utf8("/")))
            fail("list sur un volume démonté")
        } catch (e: EngineError) {
            assertEquals(FL_EINVAL, e.code)
        }
    }

    @Test
    fun busyUnmount() = withVolume("ext4-plain.img", 0, null) { _, vol ->
        val f = EngineError.check(NativeEngine.openFile(vol, utf8("/hello.txt")))
        assertEquals(FL_EBUSY, NativeEngine.unmount(vol, false))
        assertEquals(16, NativeEngine.pread(f, 0, ByteArray(16), 16)) // toujours utilisable
        assertEquals(0, NativeEngine.closeFile(f))
        assertEquals(0, NativeEngine.unmount(vol, false))
    }

    @Test
    fun closeDiskDuringReads() {
        val disk = open(Fixtures.file("ext4-plain.img"))
        val vol = EngineError.check(NativeEngine.mount(disk, 0, null))
        val f = EngineError.check(NativeEngine.openFile(vol, utf8("/gros.bin")))
        val bad = AtomicReference<String?>(null)
        val threads = (0 until 4).map { t ->
            Thread {
                val rnd = Random(7L + t)
                val buf = ByteArray(65536)
                while (true) {
                    val off = rnd.nextInt(5 shl 20).toLong()
                    val n = NativeEngine.pread(f, off, buf, buf.size)
                    if (n == FL_EINVAL) break
                    if (n <= 0 || !buf.copyOf(n).contentEquals(pattern(off, n))) {
                        bad.compareAndSet(null, "lecture $n à $off")
                        break
                    }
                }
            }.apply { start() }
        }
        Thread.sleep(200)
        assertEquals(0, NativeEngine.closeDisk(disk))
        threads.forEach { it.join(10_000) }
        assertTrue(threads.none { it.isAlive })
        assertNull(bad.get())
        assertEquals(FL_EINVAL, NativeEngine.unmount(vol, true))
    }

    /**
     * Écriture sur une copie de ext4-rw-reel.img dans le cache de l'application, laissée en place pour
     * tools/android/check_image.sh (e2fsck -fn sur le PC).
     */
    @Test
    fun writeThenReadBack() {
        val ctx = InstrumentationRegistry.getInstrumentation().targetContext
        val img = File(ctx.cacheDir, "engine-ecriture.img")
        Fixtures.file("ext4-rw-reel.img").copyTo(img, overwrite = true)
        val size = 5 shl 20
        val emoji = "/écrit ☀ 🚀.txt"
        var disk = EngineError.check(NativeEngine.openImage(utf8(img.absolutePath), false))
        try {
            val vol = EngineError.check(NativeEngine.mount(disk, 0, null, true))
            assertFalse(VolumeStat.parse(NativeEngine.statfs(vol)).readOnly)
            assertEquals(0, NativeEngine.mkdir(vol, utf8("/dossier")))
            assertEquals(FL_EEXIST, NativeEngine.mkdir(vol, utf8("/dossier")))

            val f = EngineError.check(
                NativeEngine.openFile(vol, utf8("/dossier/gros.bin"), OpenFlags.RDWR or OpenFlags.CREAT))
            var off = 0
            while (off < size) {
                val len = minOf(1 shl 20, size - off)
                assertEquals(len, NativeEngine.pwrite(f, off.toLong(), pattern(off.toLong(), len), len))
                off += len
            }
            assertArrayEquals(pattern(0, size), readAll(f, 0, size))
            assertEquals(0, NativeEngine.closeFile(f))

            val e = EngineError.check(NativeEngine.openFile(vol, utf8(emoji), OpenFlags.RDWR or OpenFlags.CREAT))
            assertEquals(5, NativeEngine.pwrite(e, 0, utf8("soleil"), 5))
            assertEquals(0, NativeEngine.truncate(e, 4))
            assertEquals(0, NativeEngine.closeFile(e))

            assertEquals(0, NativeEngine.rename(vol, utf8("/dossier/gros.bin"), utf8("/dossier/renommé.bin")))
            assertEquals(FL_ENOTEMPTY, NativeEngine.rmdir(vol, utf8("/dossier")))
            val tmp = EngineError.check(NativeEngine.openFile(vol, utf8("/à-supprimer"), OpenFlags.RDWR or OpenFlags.CREAT))
            assertEquals(0, NativeEngine.closeFile(tmp))
            assertEquals(0, NativeEngine.unlink(vol, utf8("/à-supprimer")))
            assertEquals(0, NativeEngine.sync(vol))
            assertEquals(0, NativeEngine.unmount(vol, false))
        } finally {
            NativeEngine.closeDisk(disk)
        }

        // relecture après réouverture, en lecture seule
        disk = open(img)
        try {
            val vol = EngineError.check(NativeEngine.mount(disk, 0, null))
            assertTrue(VolumeStat.parse(NativeEngine.statfs(vol)).readOnly)
            val names = DirEntry.parseList(NativeEngine.list(vol, utf8("/"))).map { it.name }
            assertTrue(names.toString(), emoji.substring(1) in names)
            assertFalse(names.toString(), "à-supprimer" in names)
            assertEquals(4L, FsStat.parse(NativeEngine.stat(vol, utf8(emoji))).size)
            assertEquals(size.toLong(), FsStat.parse(NativeEngine.stat(vol, utf8("/dossier/renommé.bin"))).size)
            val f = EngineError.check(NativeEngine.openFile(vol, utf8("/dossier/renommé.bin")))
            assertArrayEquals(pattern(0, size), readAll(f, 0, size))
            assertEquals(FL_EROFS, NativeEngine.pwrite(f, 0, ByteArray(1), 1))
            assertEquals(0, NativeEngine.closeFile(f))
            assertEquals(FL_EROFS, NativeEngine.mkdir(vol, utf8("/non")))
        } finally {
            NativeEngine.closeDisk(disk)
        }
    }

    private fun compareUtf8(a: String, b: String): Int {
        val x = utf8(a)
        val y = utf8(b)
        for (i in 0 until minOf(x.size, y.size)) {
            val d = (x[i].toInt() and 0xFF) - (y[i].toInt() and 0xFF)
            if (d != 0) return d
        }
        return x.size - y.size
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.files.SeekableReader
import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import kotlin.random.Random
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Lecture à accès aléatoire sur un flux qu'on ne sait ouvrir qu'à un décalage (FTP REST, SFTP). */
class SeekableReaderTest {
    private val size = 5 shl 20
    private val data = ByteArray(size) { (it * 31 + it / 7919).toByte() }
    private val streams = ArrayList<Closing>()

    /** Flux de [data] à partir de [offset], qui note sa fermeture. */
    private inner class Closing(offset: Long) : InputStream() {
        private val s = ByteArrayInputStream(data, offset.toInt(), size - offset.toInt())
        var closed = false
        override fun read() = s.read()
        override fun read(b: ByteArray, off: Int, len: Int) = s.read(b, off, len)
        override fun close() { closed = true }
    }

    private fun reader() = SeekableReader(size.toLong()) { off -> Closing(off).also { streams += it } }

    private fun readAt(r: SeekableReader, off: Long, len: Int): ByteArray {
        val buf = ByteArray(len)
        val n = r.read(off, buf, len)
        return buf.copyOf(maxOf(n, 0))
    }

    @Test fun randomReadsMatch() {
        val r = reader()
        val rnd = Random(42)
        repeat(1000) {
            val off = rnd.nextLong(size.toLong())
            val len = rnd.nextInt(1, 70_000)
            val got = readAt(r, off, len)
            val want = data.copyOfRange(off.toInt(), minOf(size, off.toInt() + len))
            assertArrayEquals("décalage $off", want, got)
        }
        r.close()
    }

    @Test fun sequentialReadsOpenOnce() {
        val r = reader()
        var off = 0L
        while (off < size) {
            val got = readAt(r, off, 65536)
            off += got.size
        }
        assertEquals(1, r.opens)
        assertEquals(-1, r.read(size.toLong(), ByteArray(10), 10)) // au-delà de la fin
    }

    @Test fun nearSeekSkips() {
        val r = reader()
        readAt(r, 0, 1000)
        readAt(r, 200_000, 1000) // saut en avant de moins de 256 Kio : lu et jeté
        assertEquals(1, r.opens)
    }

    @Test fun farSeekReopensOnce() {
        val r = reader()
        readAt(r, 0, 1000)
        assertArrayEquals(data.copyOfRange(4_000_000, 4_001_000), readAt(r, 4_000_000, 1000))
        assertEquals(2, r.opens)
        assertTrue(streams[0].closed) // l'ancien flux est fermé, jamais accumulé
        readAt(r, 4_001_000, 1000)
        assertEquals(2, r.opens)
        assertArrayEquals(data.copyOfRange(10, 20), readAt(r, 10, 10)) // en arrière : rouvert
        assertEquals(3, r.opens)
        assertEquals(1, streams.count { !it.closed })
    }

    @Test fun closeClosesStream() {
        val r = reader()
        readAt(r, 0, 10)
        r.close()
        assertTrue(streams.all { it.closed })
    }

    /** Relecture finale (I5) : un flux inactif (musique en pause) rend sa connexion, puis se rouvre à la demande. */
    @Test fun idleStreamReleased() {
        var now = 0L
        val r = SeekableReader(size.toLong(), clock = { now }) { off -> Closing(off).also { streams += it } }
        readAt(r, 0, 10)
        now += SeekableReader.IDLE_MS - 1
        assertFalse(r.closeIfIdle())
        assertFalse(streams[0].closed)
        now += 1
        assertTrue(r.closeIfIdle())
        assertTrue(streams[0].closed)
        assertArrayEquals(data.copyOfRange(10, 20), readAt(r, 10, 10))
        assertEquals(2, r.opens)
    }

    @Test fun readerClosedWhenSourceGone() {
        var fail = false
        val r = SeekableReader(size.toLong()) { off ->
            if (fail) throw IOException("Connexion perdue")
            Closing(off).also { streams += it }
        }
        readAt(r, 0, 10)
        fail = true
        try {
            readAt(r, 4_000_000, 10)
            throw AssertionError("accepté")
        } catch (e: IOException) {
            assertEquals("Connexion perdue", e.message)
        }
        assertTrue(streams.all { it.closed })
    }
}

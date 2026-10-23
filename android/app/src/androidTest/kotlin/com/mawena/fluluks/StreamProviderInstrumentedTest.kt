package com.mawena.fluluks

import android.system.Os
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Entry
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.provider.StreamProvider
import com.mawena.fluluks.remote.RemoteClient
import com.mawena.fluluks.remote.RemoteSource
import com.mawena.fluluks.remote.ServerConfig
import java.io.File
import java.io.FileInputStream
import java.io.InputStream
import java.io.OutputStream
import kotlin.random.Random
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Descripteur mandataire du fournisseur de flux (dossiers distants) lu à des décalages aléatoires, comme le font
 * Media3 et le lecteur d'archives. Le « serveur » est un dossier du cache de l'application ; chaque ouverture est
 * comptée.
 */
@RunWith(AndroidJUnit4::class)
class StreamProviderInstrumentedTest {
    private val ctx get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var root: File

    private inner class DirClient : RemoteClient {
        var opens = 0
        private fun f(p: String) = File(root, p.removePrefix("/"))
        override fun list(path: String) = f(path).listFiles()!!.map { stat("/${it.name}")!! }
        override fun stat(path: String): Entry? = f(path).takeIf { it.exists() }
            ?.let { Entry(it.name, path, if (it.isDirectory) FileType.DIR else FileType.FILE, it.length(), it.lastModified() / 1000) }
        override fun openReadAt(path: String, offset: Long): InputStream {
            opens++
            return FileInputStream(f(path)).also { it.skip(offset) }
        }
        override fun create(parent: String, name: String): OutputStream = throw UnsupportedOperationException()
        override fun mkdir(parent: String, name: String) = throw UnsupportedOperationException()
        override fun rename(path: String, newPath: String) = throw UnsupportedOperationException()
        override fun delete(path: String, dir: Boolean) = throw UnsupportedOperationException()
        override fun close() {}
    }

    @Before fun setUp() {
        Files.context = ctx.applicationContext
        root = File(ctx.cacheDir, "flux-essai").apply { deleteRecursively(); mkdirs() }
    }

    @After fun tearDown() {
        root.deleteRecursively()
    }

    @Test fun randomReadsThroughProxy() {
        val bytes = Random(7).nextBytes(3 * (1 shl 20) + 123)
        File(root, "f.bin").writeBytes(bytes)
        val client = DirClient()
        val src = RemoteSource(ServerConfig("essai", "sftp", "Essai", "h", 22, "u", "/", false, null, null), client)
        StreamProvider.openProxy(src, "/f.bin", bytes.size.toLong()).use { pfd ->
            val fd = pfd.fileDescriptor
            val rnd = Random(11)
            val buf = ByteArray(70_000)
            repeat(200) {
                val off = rnd.nextInt(bytes.size)
                val n = rnd.nextInt(1, buf.size)
                var got = 0
                while (got < n) {
                    val k = Os.pread(fd, buf, got, n - got, (off + got).toLong())
                    if (k <= 0) break
                    got += k
                }
                assertEquals(minOf(n, bytes.size - off), got)
                assertArrayEquals(bytes.copyOfRange(off, off + got), buf.copyOf(got))
            }
            assertEquals(0, Os.pread(fd, buf, 0, 10, bytes.size.toLong())) // fin du fichier
        }
        // sauts lointains seulement rouverts : bien moins d'ouvertures que de lectures
        assert(client.opens < 200) { "ouvertures : ${client.opens}" }
    }
}

package com.mawena.fluluks

import android.media.MediaScannerConnection
import android.os.Environment
import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.mawena.fluluks.files.QuickFilters
import com.mawena.fluluks.files.Sources
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Filtres rapides sur le Pixel, par l'index d'Android : un fichier créé dans le dossier jetable Download/portail-tests/
 * (supprimé à la fin, index compris) apparaît dans « Récents » et « Documents », pas dans « Photos ».
 */
@RunWith(AndroidJUnit4::class)
class QuickFiltersInstrumentedTest {
    private val instr get() = InstrumentationRegistry.getInstrumentation()
    private val ctx get() = instr.targetContext

    @Suppress("DEPRECATION")
    private val testDir = File(Environment.getExternalStorageDirectory(), "Download/portail-tests")

    private fun shell(cmd: String) =
        ParcelFileDescriptor.AutoCloseInputStream(instr.uiAutomation.executeShellCommand(cmd)).use { it.readBytes() }

    /** Indexe (ou retire de l'index) [f], et attend la fin. */
    private fun scan(f: File) {
        val done = CountDownLatch(1)
        MediaScannerConnection.scanFile(ctx, arrayOf(f.absolutePath), null) { _, _ -> done.countDown() }
        assertTrue("indexation trop longue", done.await(30, TimeUnit.SECONDS))
    }

    @Before fun setUp() {
        shell("appops set ${ctx.packageName} MANAGE_EXTERNAL_STORAGE allow")
        Sources.init(ctx)
        assumeTrue("accès à tous les fichiers refusé", Sources.hasAllFilesAccess())
        testDir.deleteRecursively()
        assertTrue(testDir.mkdirs())
    }

    @After fun tearDown() {
        val files = testDir.listFiles().orEmpty().toList()
        testDir.deleteRecursively()
        files.forEach(::scan) // retire aussi les entrées de l'index
    }

    @Test fun newFileListedInRecentAndDocuments() {
        val name = "rapport-${System.nanoTime()}.pdf"
        val f = File(testDir, name).apply { writeBytes("%PDF-1.4\n%%EOF\n".toByteArray()) }
        scan(f)
        val path = "/Download/portail-tests/$name"
        val recent = QuickFilters.query(ctx, "recent")
        assertTrue(recent.map { it.path }.take(50).toString(), recent.any { it.sourceId == Sources.PHONE && it.path == path })
        val docs = QuickFilters.query(ctx, "documents")
        assertTrue(docs.any { it.sourceId == Sources.PHONE && it.path == path && it.name == name && it.size == f.length() })
        assertFalse(QuickFilters.query(ctx, "photos").any { it.path == path })
        // trié du plus récent : notre fichier, tout juste créé, est en tête des récents (à la seconde près)
        assertTrue(recent.first().mtime >= f.lastModified() / 1000 - 1)
    }
}

package com.mawena.fluluks

import android.os.ParcelFileDescriptor
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.io.RandomAccessFile

/**
 * Images de test, poussées sur le téléphone par tools/android/run_native_tests.sh (T2) dans
 * /data/local/tmp/fluluks/fixtures/, puis copiées dans filesDir de l'application (lisible par le natif)
 * via le shell de UiAutomation. On ne recopie que si le fichier manque ou si sa taille diffère.
 */
object Fixtures {
    private const val SRC = "/data/local/tmp/fluluks/fixtures"

    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()

    private fun shell(cmd: String): ParcelFileDescriptor = instrumentation.uiAutomation.executeShellCommand(cmd)

    private fun shellText(cmd: String): String =
        ParcelFileDescriptor.AutoCloseInputStream(shell(cmd)).use { String(it.readBytes(), Charsets.UTF_8) }.trim()

    private val dir: File
        get() = File(instrumentation.targetContext.filesDir, "fixtures").apply { mkdirs() }

    /** Chemin local de la fixture `name` (sans espace : executeShellCommand ne gère pas les guillemets). */
    @Synchronized
    fun file(name: String): File {
        require(name.matches(Regex("[A-Za-z0-9._-]+"))) { "nom de fixture inattendu : $name" }
        val expected = shellText("stat -c %s $SRC/$name").toLongOrNull()
            ?: error("fixture absente du téléphone : $SRC/$name (lancer tools/android/run_native_tests.sh)")
        val dst = File(dir, name)
        if (dst.exists() && dst.length() == expected) return dst
        val tmp = File(dir, "$name.part")
        ParcelFileDescriptor.AutoCloseInputStream(shell("cat $SRC/$name")).use { input ->
            tmp.outputStream().use { input.copyTo(it, 1 shl 20) }
        }
        check(tmp.length() == expected) { "copie incomplète de $name : ${tmp.length()} / $expected" }
        check(tmp.renameTo(dst)) { "renommage impossible : $dst" }
        return dst
    }

    fun bytes(name: String): ByteArray = file(name).readBytes()

    /** Copie de `name` dont la magie de l'en-tête LUKS principal (6 octets) est détruite. */
    @Synchronized
    fun damagedPrimary(name: String): File {
        val src = file(name)
        val dst = File(dir, "abime-$name")
        if (!dst.exists() || dst.length() != src.length()) {
            src.copyTo(dst, overwrite = true)
            RandomAccessFile(dst, "rw").use { it.write(ByteArray(6)) }
        }
        return dst
    }
}

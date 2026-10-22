package com.mawena.fluluks

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.drawable.AdaptiveIconDrawable
import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Nom visible et icône de l'application (phase 6 : Portail ; le paquet reste com.mawena.fluluks). */
@RunWith(AndroidJUnit4::class)
class AppIdentityTest {
    private val ctx get() = InstrumentationRegistry.getInstrumentation().targetContext

    @Test fun labelIsPortail() {
        assertEquals("Portail", ctx.packageManager.getApplicationLabel(ctx.applicationInfo).toString())
        assertEquals("com.mawena.fluluks", ctx.packageName)
    }

    /** Icône adaptative avec silhouette monochrome ; rendue dans cacheDir/icone.png pour être regardée. */
    @Test fun iconIsAdaptiveWithMonochrome() {
        val icon = ctx.packageManager.getApplicationIcon(ctx.applicationInfo)
        assertTrue(icon is AdaptiveIconDrawable)
        if (Build.VERSION.SDK_INT >= 33) assertNotNull((icon as AdaptiveIconDrawable).monochrome)
        val bmp = Bitmap.createBitmap(432, 432, Bitmap.Config.ARGB_8888)
        icon.setBounds(0, 0, 432, 432)
        icon.draw(Canvas(bmp))
        File(ctx.cacheDir, "icone.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    /**
     * Phase 7 : fond orange terracotta. Teinte lue sur un anneau hors de la zone sûre (le dessin) mais dans
     * le masque de l'icône : chaque point opaque doit être orange (teinte entre 10° et 30°).
     */
    @Test fun iconIsOrange() {
        val icon = ctx.packageManager.getApplicationIcon(ctx.applicationInfo)
        val n = 432
        val bmp = Bitmap.createBitmap(n, n, Bitmap.Config.ARGB_8888)
        icon.setBounds(0, 0, n, n)
        icon.draw(Canvas(bmp))
        val hsv = FloatArray(3)
        var seen = 0
        for (deg in 0 until 360 step 15) {
            val a = Math.toRadians(deg.toDouble())
            val x = (n / 2 + 0.47 * n * Math.cos(a)).toInt()
            val y = (n / 2 + 0.47 * n * Math.sin(a)).toInt()
            val c = bmp.getPixel(x, y)
            if (Color.alpha(c) < 255) continue // hors du masque
            Color.colorToHSV(c, hsv)
            assertTrue("teinte ${hsv[0]} à $deg°", hsv[0] in 10f..30f && hsv[1] > 0.5f)
            seen++
        }
        assertTrue("aucun point du fond lu", seen >= 8)
    }
}

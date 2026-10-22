package com.mawena.fluluks

import android.graphics.Bitmap
import android.graphics.Canvas
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
}

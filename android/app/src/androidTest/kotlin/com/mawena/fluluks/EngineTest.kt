package com.mawena.fluluks

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.mawena.fluluks.engine.NativeEngine
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class EngineTest {
    @Test
    fun versionLoads() {
        val v = NativeEngine.version()
        assertTrue("version inattendue : $v", v.startsWith("fluluks "))
    }
}

package com.mawena.fluluks

import com.mawena.fluluks.bridge.FlutterError
import com.mawena.fluluks.bridge.HostApiImpl
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.files.Files
import org.junit.Assert.assertEquals
import org.junit.Test

/** Erreurs envoyées à Flutter par le pont. */
class HostApiErrorsTest {
    /** Relecture finale : « Ouvrir avec » sans application garde son message français. */
    @Test fun flutterErrorKeptAsIs() {
        val e = HostApiImpl.toFlutterError(FlutterError(EngineError.FL_ENOTSUP.toString(), Files.MSG_NO_VIEWER))
        assertEquals(Files.MSG_NO_VIEWER, e.message)
        assertEquals(EngineError.FL_ENOTSUP.toString(), e.code)
    }
}

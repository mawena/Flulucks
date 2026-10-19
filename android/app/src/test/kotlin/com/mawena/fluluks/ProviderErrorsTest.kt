package com.mawena.fluluks

import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.provider.ProviderErrors
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Codes du moteur → errno et messages rendus aux autres applications. */
class ProviderErrorsTest {
    @Test fun errnoForEfbig() {
        assertEquals(ProviderErrors.Errno.EFBIG, ProviderErrors.errno(EngineError.FL_EFBIG))
        assertEquals(ProviderErrors.Errno.ENOSPC, ProviderErrors.errno(EngineError.FL_ENOSPC))
        assertEquals(ProviderErrors.Errno.EROFS, ProviderErrors.errno(EngineError.FL_EROFS))
        assertEquals(ProviderErrors.Errno.ENXIO, ProviderErrors.errno(EngineError.FL_ENODEV))
        assertEquals(ProviderErrors.Errno.EIO, ProviderErrors.errno(EngineError.FL_EIO))
        assertEquals(ProviderErrors.Errno.EIO, ProviderErrors.errno(EngineError.FL_EBUSY))
    }

    @Test fun efbigMessage() {
        for (op in listOf("openDocument", "copyDocument", "createDocument"))
            assertEquals("Fichier trop gros pour ce disque (4 Gio au plus en FAT)", ProviderErrors.message(EngineError.FL_EFBIG, op))
    }

    @Test fun invalidNameMessage() {
        for (op in listOf("createDocument", "renameDocument", "moveDocument", "copyDocument"))
            assertEquals("Nom non autorisé sur ce disque", ProviderErrors.message(EngineError.FL_EINVAL, op))
        assertEquals("Document introuvable ou illisible", ProviderErrors.message(EngineError.FL_EINVAL, "queryDocument"))
    }

    @Test fun otherMessagesUnchanged() {
        assertEquals("Disque plein", ProviderErrors.message(EngineError.FL_ENOSPC, "openDocument"))
        assertEquals("Un élément porte déjà ce nom", ProviderErrors.message(EngineError.FL_EEXIST, "renameDocument"))
        assertEquals("Fichier en cours d'utilisation", ProviderErrors.message(EngineError.FL_EBUSY, "deleteDocument"))
        assertEquals("Volume fermé", ProviderErrors.message(EngineError.FL_ENODEV, "queryDocument"))
        assertNull(ProviderErrors.message(EngineError.FL_EROFS, "createDocument")) // raison du volume, côté provider
    }
}

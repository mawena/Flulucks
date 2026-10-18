package com.mawena.fluluks

import com.mawena.fluluks.provider.MimeTypes
import org.junit.Assert.assertEquals
import org.junit.Test

class MimeTypesTest {
    @Test fun knownExtensionsAreCaseInsensitive() {
        assertEquals("video/x-matroska", MimeTypes.forName("film.MKV"))
        assertEquals("video/mp4", MimeTypes.forName("a.b.mp4"))
        assertEquals("text/plain", MimeTypes.forName("note.TXT"))
        assertEquals("application/epub+zip", MimeTypes.forName("livre.epub"))
    }

    @Test fun unknownFallsBack() {
        assertEquals("application/octet-stream", MimeTypes.forName("inconnu.xyz"))
        assertEquals("application/octet-stream", MimeTypes.forName("sansextension"))
        assertEquals("application/octet-stream", MimeTypes.forName("fini.par.point."))
        assertEquals("application/octet-stream", MimeTypes.forName(".cache"))
    }
}

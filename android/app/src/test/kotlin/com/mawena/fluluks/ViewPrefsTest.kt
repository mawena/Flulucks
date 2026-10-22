package com.mawena.fluluks

import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.LocalSource
import com.mawena.fluluks.files.PrefStore
import com.mawena.fluluks.files.ViewPrefs
import com.mawena.fluluks.files.ViewSettings
import com.mawena.fluluks.volumes.VolumeManager
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class ViewPrefsTest {
    @get:Rule val tmp = TemporaryFolder()

    private class MapStore : PrefStore {
        val m = HashMap<String, Any>()
        override fun getString(key: String) = m[key] as? String
        override fun putString(key: String, value: String) { m[key] = value }
        override fun getBoolean(key: String, default: Boolean) = m[key] as? Boolean ?: default
        override fun putBoolean(key: String, value: Boolean) { m[key] = value }
    }

    private val store = MapStore()
    private val open = ArrayList<FileSource>()
    private lateinit var e: MemEngine

    @Before fun setup() {
        e = MemEngine()
        VolumeManager.resetForTests(e, delayer = FakeDelayer())
        open += LocalSource("phone", "phone", "Téléphone", tmp.root) { true }
        Files.setResolverForTests { id -> open.firstOrNull { it.id == id } }
    }

    @After fun teardown() = Files.setResolverForTests(null)

    private fun prefs() = ViewPrefs(store) { open.toList() }

    private fun mountDisk(): String {
        val disk = VolumeManager.addImageDiskForTests("/x/mem.img", readOnly = false)
        val id = runBlocking { VolumeManager.mountPlain(disk, 1, true) }.id
        open += DiskSource(id, e)
        return id
    }

    @Test fun defaultsAndUnknownValues() {
        assertEquals(ViewSettings("name", true, false, false), prefs().get())
        prefs().set(ViewSettings("size", false, true, true))
        assertEquals(ViewSettings("size", false, true, true), prefs().get())
        store.m["tri"] = "couleur"
        assertEquals("name", prefs().get().sort)
        prefs().set(ViewSettings("n'importe", true, false, false))
        assertEquals("name", store.m["tri"])
        store.m["favoris"] = "{pas du json"
        assertEquals(emptyList<Any>(), prefs().favorites())
    }

    @Test fun favoritesPersistAndDedupe() {
        prefs().add("phone", "/DCIM", "Appareil photo")
        prefs().add("phone", "/DCIM", "Appareil photo")
        prefs().add("phone", "/Download", "Téléchargements")
        val f = prefs().favorites()
        assertEquals(listOf("/DCIM", "/Download"), f.map { it.path })
        assertEquals("phone", f[0].sourceId)
        prefs().remove("phone", "/DCIM")
        assertEquals(listOf("/Download"), prefs().favorites().map { it.path })
        try { prefs().add("phone", "/a/../b", "x"); throw AssertionError("accepté") } catch (x: Exception) { }
    }

    @Test fun favoriteResolvesToOpenVolumeByPartitionUuid() {
        val id1 = mountDisk()
        prefs().add(id1, "/Films", "Films")
        assertEquals("disk:uuid-mem", prefs().favorites().single().key)
        assertEquals(id1, prefs().favorites().single().sourceId)
        VolumeManager.eject(id1)
        open.removeAll { it.id == id1 }
        assertNull(prefs().favorites().single().sourceId) // disque non ouvert
        val id2 = mountDisk() // réouverture : nouvel identifiant, même partition
        assertEquals(id2, prefs().favorites().single().sourceId)
    }

    @Test fun favoriteLimit100() {
        for (i in 0 until 100) prefs().add("phone", "/d$i", "d$i")
        try { prefs().add("phone", "/trop", "x"); throw AssertionError("accepté") } catch (x: FilesException) {
            assertEquals(ViewPrefs.MSG_TOO_MANY, x.message)
        }
        prefs().add("phone", "/d5", "d5") // doublon : sans erreur
        assertEquals(100, prefs().favorites().size)
    }
}

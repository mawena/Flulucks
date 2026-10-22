package com.mawena.fluluks

import com.mawena.fluluks.volumes.AutoLock
import com.mawena.fluluks.volumes.VolumeManager
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Before
import org.junit.Test

/** Verrouillage automatique (phase 7) : volume chiffré éjecté après le délai choisi sans activité. */
class AutoLockTest {
    private lateinit var engine: FakeEngine
    private var now = 0L
    private var minutes = 0
    private val warnings = mutableListOf<String>()
    private lateinit var luks: String
    private lateinit var plain: String
    private val min = 60_000L

    /** Exécuteur du moteur qu'on peut retenir : l'éjection demandée attend son tour (Argon2, synchronisation…). */
    private var hold = false
    private val pending = ArrayDeque<Runnable>()

    @Before fun setup() {
        engine = FakeEngine()
        VolumeManager.resetForTests(engine, delayer = FakeDelayer(), clock = { now },
            executor = { r -> if (hold) pending.add(r) else r.run() })
        val disk = VolumeManager.addImageDiskForTests("/x/gpt-luks2.img")
        luks = runBlocking { VolumeManager.unlock(disk, 1, "secret".toByteArray()) }.id
        plain = runBlocking { VolumeManager.mountPlain(disk, 2) }.id
        AutoLock.resetForTests(minutes = { minutes }, busy = { false }, warn = { warnings += it })
    }

    @After fun teardown() = AutoLock.resetForTests()

    private fun open(id: String) = VolumeManager.volume(id) != null

    @Test fun offByDefault() {
        AutoLock.resetForTests()
        now = 600 * min
        AutoLock.check(now)
        assertNotNull(VolumeManager.volume(luks))
    }

    @Test fun locksEncryptedAfterTimeout() {
        minutes = 15
        now = 15 * min - 1_000
        AutoLock.check(now)
        assertEquals(true, open(luks))
        now = 15 * min
        AutoLock.check(now)
        assertEquals(false, open(luks))
        assertEquals(listOf("Disque verrouillé après 15 min sans activité"), warnings)
    }

    @Test fun plainNeverLocked() {
        minutes = 5
        now = 100 * min
        AutoLock.check(now)
        assertEquals(true, open(plain))
    }

    @Test fun activityPostponesLock() {
        minutes = 5
        now = 4 * min
        VolumeManager.noteActivity(luks)
        now = 8 * min
        AutoLock.check(now)
        assertEquals(true, open(luks))
        now = 9 * min
        AutoLock.check(now)
        assertEquals(false, open(luks))
    }

    @Test fun neverDuringTransfer() {
        minutes = 5
        AutoLock.resetForTests(minutes = { minutes }, busy = { it == luks }, warn = { warnings += it })
        now = 60 * min
        AutoLock.check(now)
        assertEquals(true, open(luks))
        assertEquals(emptyList<String>(), warnings)
    }

    @Test fun failedLockNotRetriedUntilNewActivity() {
        minutes = 5
        engine.syncResult = -5 // le disque refuse la synchronisation : « Éjection impossible », volume gardé
        now = 5 * min
        AutoLock.check(now)
        assertEquals(true, open(luks))
        now = 6 * min
        AutoLock.check(now)
        now = 7 * min
        AutoLock.check(now)
        assertEquals(1, engine.syncs) // une seule tentative
        assertEquals(emptyList<String>(), warnings) // jamais « verrouillé » quand le disque est resté ouvert
        VolumeManager.noteActivity(luks)
        engine.syncResult = 0
        now = 12 * min
        AutoLock.check(now)
        assertEquals(false, open(luks))
        assertEquals(listOf("Disque verrouillé après 5 min sans activité"), warnings)
    }

    /** Relecture finale : l'éjection attend son tour ; une activité arrivée entre-temps l'annule. */
    @Test fun activityBeforeQueuedEjectCancelsLock() {
        minutes = 5
        now = 5 * min
        hold = true
        AutoLock.check(now) // éjection mise en file derrière un travail long
        now = 5 * min + 10_000
        VolumeManager.noteActivity(luks) // l'utilisateur rouvre une vidéo
        hold = false
        while (pending.isNotEmpty()) pending.removeFirst().run()
        assertEquals(true, open(luks))
        assertEquals(emptyList<String>(), warnings)
    }

    @Test fun settingReadAtEachCheck() {
        minutes = 30
        now = 20 * min
        AutoLock.check(now)
        assertEquals(true, open(luks))
        minutes = 15
        AutoLock.check(now)
        assertEquals(false, open(luks))
    }

    @Test fun invalidSettingMeansNever() {
        assertEquals(0, AutoLock.validate(7))
        assertEquals(0, AutoLock.validate(-1))
        assertEquals(60, AutoLock.validate(60))
        assertEquals(listOf(0, 5, 15, 30, 60), AutoLock.CHOICES)
    }
}

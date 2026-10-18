package com.mawena.fluluks.volumes

import android.app.ActivityManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.provider.DocumentsContract
import android.util.Log
import androidx.annotation.VisibleForTesting
import androidx.core.content.ContextCompat
import com.mawena.fluluks.provider.DocumentIds
import com.mawena.fluluks.engine.Engine
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.NativeEngine
import com.mawena.fluluks.engine.ScanResult
import com.mawena.fluluks.engine.VolumeStat
import com.mawena.fluluks.service.VolumeService
import com.mawena.fluluks.usb.UsbDisks
import org.json.JSONException
import java.math.BigDecimal
import java.math.RoundingMode
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executor
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext

/**
 * État des disques et des volumes ouverts.
 *
 * Fils : toutes les opérations de cycle de vie du moteur (openUsb, scan, mount, unmount, closeDisk)
 * passent par UN exécuteur mono-fil appartenant à ce gestionnaire, jamais par le fil principal. Les
 * lectures du provider (nativeVolume, volume, pread…) viennent d'autres fils et sont sûres.
 * Conséquence : un déverrouillage Argon2 en cours retarde un éjection/débranchement jusqu'à sa fin.
 *
 * Écriture : le provider appelle [noteWrite] après chaque écriture ; le volume est synchronisé
 * [SYNC_DELAY_MS] après la dernière (une seule synchronisation pour une rafale), sur l'exécuteur.
 * L'éjection synchronise, démonte normalement (SYNCHRONIZE CACHE compris, clé effacée), puis libère le
 * disque (pile native et interface USB) s'il n'a plus de volume ; il est rouvert à la demande.
 */
object VolumeManager {
    private const val TAG = "VolumeManager"
    private const val MAX_VOLUMES = 8 // limite de montages de lwext4
    private const val ACTION_USB_PERMISSION = "com.mawena.fluluks.USB_PERMISSION"
    const val AUTHORITY = "com.mawena.fluluks.documents"

    const val MSG_LOW_MEMORY =
        "Mémoire insuffisante pour déverrouiller ce disque : fermez d'autres applications et réessayez"
    const val MSG_TOO_MANY = "Trop de volumes ouverts : éjectez-en un avant d'en ouvrir un autre"
    const val MSG_EJECTING = "Écriture des données en attente…"
    const val MSG_DETACH_DURING_WRITE =
        "Disque débranché pendant une écriture : vérifiez-le sous Ubuntu avant de le réutiliser"
    fun msgEjectFailed(detail: String) = "Éjection impossible : $detail"
    fun msgEjectIncomplete(detail: String) =
        "Éjection incomplète : des données ont pu ne pas être écrites ($detail). Vérifiez le disque sous Ubuntu."

    const val MSG_FILES_OPEN = "des fichiers sont encore ouverts"
    private const val RO_USER_REASON = "ouvert en lecture seule à votre demande" // FL_RO_USER
    fun msgModeChange(writable: Boolean, detail: String?) =
        "Impossible de rouvrir le volume en " + (if (writable) "écriture" else "lecture seule") + " : " +
            (detail ?: "erreur") + ". Éjectez-le d'abord."

    /** Délai entre la dernière écriture et la synchronisation du volume. */
    const val SYNC_DELAY_MS = 3000L

    /** Exécution différée, annulable (la fonction rendue annule) ; injectable pour les tests. */
    fun interface Delayer {
        fun schedule(delayMs: Long, task: Runnable): () -> Unit
    }

    private val realDelayer: Delayer by lazy {
        val timer = java.util.concurrent.Executors.newSingleThreadScheduledExecutor { r ->
            Thread(r, "fluluks-sync").apply { isDaemon = true }
        }
        Delayer { ms, task ->
            val f = timer.schedule(task, ms, java.util.concurrent.TimeUnit.MILLISECONDS)
            val cancel: () -> Unit = { f.cancel(false) }
            cancel
        }
    }
    @Volatile private var delayer: Delayer? = null

    /** Avertissement de débranchement pendant une écriture (notification) ; injectable pour les tests. */
    @Volatile private var detachWarner: (String) -> Unit = { msg -> appContext?.let { VolumeService.warn(it, msg) } }

    private var engineOverride: Engine? = null
    private val engine: Engine get() = engineOverride ?: NativeEngine.also { engineOverride = it }

    private var executor: ExecutorService? = null
    private var executorOverride: Executor? = null
    private val exec: Executor
        @Synchronized get() = executorOverride ?: executor ?: Executors.newSingleThreadExecutor { r ->
            Thread(r, "fluluks-engine").apply { isDaemon = true }
        }.also { executor = it }
    private val dispatcher get() = exec.asCoroutineDispatcher()

    @Volatile private var availMem: () -> Long = { Long.MAX_VALUE }

    /** Service au premier plan : état et démarrage, injectables pour les tests JVM. */
    @Volatile private var serviceRunning: () -> Boolean = { VolumeService.running }
    @Volatile private var serviceStarter: (() -> Unit)? = null
    @Volatile private var appContext: Context? = null
    @Volatile private var usbManager: UsbManager? = null

    /** Source des périphériques connus du système (deviceName -> UsbDevice) ; injectable pour les tests. */
    @Volatile private var deviceList: () -> Map<String, UsbDevice> = { usbManager?.deviceList ?: emptyMap() }

    private val _disks = MutableStateFlow<List<DiskState>>(emptyList())
    private val _volumes = MutableStateFlow<List<VolumeState>>(emptyList())
    val disks: StateFlow<List<DiskState>> = _disks.asStateFlow()
    val volumes: StateFlow<List<VolumeState>> = _volumes.asStateFlow()

    private val _ejecting = MutableStateFlow(false)
    private val _ejectError = MutableStateFlow<String?>(null)
    /** Vrai pendant une éjection (synchronisation et démontage en cours). */
    val ejecting: StateFlow<Boolean> = _ejecting.asStateFlow()
    /** « Éjection impossible : … » après un échec (le volume reste ouvert), null sinon. */
    val ejectError: StateFlow<String?> = _ejectError.asStateFlow()

    /** Écritures non encore synchronisées, par volume ; génération pour ne pas effacer une écriture
     * arrivée pendant la synchronisation. */
    private val writeGen = ConcurrentHashMap<String, Long>()
    private val syncedGen = ConcurrentHashMap<String, Long>()
    private val pendingSync = ConcurrentHashMap<String, () -> Unit>()

    private class DiskEntry(
        var state: DiskState,
        val device: UsbDevice?,
        var handle: Long = 0,
        var connection: UsbDeviceConnection? = null,
        var intfId: Int = -1,
        val imagePath: String? = null, // disque de test : rouvert depuis ce chemin
        val imageReadOnly: Boolean = true,
    )

    private class MountEntry(val state: VolumeState, val handle: Long)

    // Modifiés uniquement sur l'exécuteur (disks) ; mounts lisible de partout.
    private val diskEntries = LinkedHashMap<String, DiskEntry>()
    private val mounts = ConcurrentHashMap<String, MountEntry>()
    private val mountOrder = ArrayList<String>() // exécuteur seulement
    private val counter = AtomicInteger(0)

    /**
     * Jeton de session : 64 bits aléatoires, propres à ce processus. Les identifiants de volume en
     * dépendent, donc un identifiant de document gardé d'un processus précédent (autorisation
     * persistante) ne peut jamais désigner un autre disque. Des identifiants stables (UUID LUKS +
     * partition) sont une décision volontairement reportée à une phase ultérieure.
     */
    @Volatile private var sessionToken: String = newSessionToken()

    private fun newSessionToken(): String {
        val b = ByteArray(8).also { java.security.SecureRandom().nextBytes(it) }
        return b.joinToString("") { "%02x".format(it) }
    }

    /** Simule un redémarrage du processus : nouveau jeton, compteur remis à zéro. */
    @VisibleForTesting
    fun newSessionForTests() {
        sessionToken = newSessionToken(); counter.set(0)
    }
    private val imageCounter = AtomicInteger(0)
    private var initialized = false

    // ---------------------------------------------------------------- initialisation

    @Synchronized
    fun init(context: Context) {
        if (initialized) return
        initialized = true
        val app = context.applicationContext
        appContext = app
        val usb = app.getSystemService(Context.USB_SERVICE) as UsbManager
        usbManager = usb
        val am = app.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        availMem = { ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }.availMem }

        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                val device = intent.usbDevice() ?: return
                when (intent.action) {
                    UsbManager.ACTION_USB_DEVICE_DETACHED -> onUsbDetached(device)
                    UsbManager.ACTION_USB_DEVICE_ATTACHED -> onUsbAttached(device)
                    ACTION_USB_PERMISSION -> onUsbAttached(device) // accordée ou non : hasPermission tranche
                }
            }
        }
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(ACTION_USB_PERMISSION)
        }
        ContextCompat.registerReceiver(app, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)

        usb.deviceList.keys.toList().forEach { onUsbAttachedName(it) }
    }

    @Suppress("DEPRECATION")
    fun Intent.usbDevice(): UsbDevice? =
        if (Build.VERSION.SDK_INT >= 33) getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        else getParcelableExtra(UsbManager.EXTRA_DEVICE)

    /** Configuration pour les tests JVM : moteur factice, mémoire injectée, exécuteur synchrone. */
    @VisibleForTesting
    @Synchronized
    fun resetForTests(
        engine: Engine,
        availMem: () -> Long = { Long.MAX_VALUE },
        executor: Executor = Executor { it.run() },
        devices: () -> Map<String, UsbDevice> = { emptyMap() },
        delayer: Delayer? = null,
        detachWarner: (String) -> Unit = {},
    ) {
        deviceList = devices
        engineOverride = engine
        executorOverride = executor
        this.availMem = availMem
        this.delayer = delayer
        this.detachWarner = detachWarner
        pendingSync.values.forEach { it() }
        pendingSync.clear(); writeGen.clear(); syncedGen.clear()
        _ejecting.value = false; _ejectError.value = null
        diskEntries.clear(); mounts.clear(); mountOrder.clear()
        counter.set(0); imageCounter.set(0); sessionToken = newSessionToken()
        _disks.value = emptyList(); _volumes.value = emptyList()
        serviceRunning = { false }; serviceStarter = null
    }

    /** Remplace l'observation et le démarrage du service au premier plan (tests JVM). */
    @VisibleForTesting
    fun setServiceHooksForTests(running: () -> Boolean, start: () -> Unit) {
        serviceRunning = running; serviceStarter = start
    }

    // ---------------------------------------------------------------- USB

    /**
     * L'UsbDevice d'un intent peut être forgé par une autre application : seul le nom est retenu, et
     * l'objet utilisé est celui que le système renvoie dans `deviceList`.
     */
    fun onUsbAttached(device: UsbDevice) = onUsbAttachedName(device.deviceName)

    internal fun onUsbAttachedName(name: String) {
        exec.execute {
            runSafely(name) {
                val real = deviceList()[name] ?: return@runSafely // inconnu du système : ignoré
                if (UsbDisks.isMassStorageCandidate(real)) attach(real)
            }
        }
    }

    fun onUsbDetached(device: UsbDevice) = onUsbDetachedName(device.deviceName)

    /** Seuls les disques connus sont concernés (un nom forgé ne fait rien). */
    internal fun onUsbDetachedName(name: String) {
        exec.execute { runSafely(name) { if (diskEntries.containsKey(name)) detach(name) } }
    }

    fun requestPermission(diskId: String) {
        val ctx = appContext ?: return
        val usb = usbManager ?: return
        val device = usb.deviceList[diskId] ?: return
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or
            (if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
        // Intent explicite : l'extra EXTRA_PERMISSION_GRANTED est ajouté par le système.
        val intent = Intent(ACTION_USB_PERMISSION).setPackage(ctx.packageName)
        usb.requestPermission(device, PendingIntent.getBroadcast(ctx, 0, intent, flags))
    }

    private fun attach(device: UsbDevice) {
        val id = device.deviceName
        val existing = diskEntries[id]
        if (existing != null && existing.handle > 0) return // déjà ouvert
        val name = UsbDisks.displayName(device)
        val usb = usbManager
        val entry = existing ?: DiskEntry(DiskState(id, name, 0, false, null, null), device).also { diskEntries[id] = it }

        val bot = UsbDisks.findBotInterface(device)
        if (bot == null) {
            entry.state = DiskState(id, name, 0, usb?.hasPermission(device) == true, null, UsbDisks.UAS_ONLY_MESSAGE)
            publishDisks(); return
        }
        if (usb == null || !usb.hasPermission(device)) {
            entry.state = DiskState(id, name, 0, false, null, null)
            publishDisks(); return
        }
        val conn = usb.openDevice(device)
        if (conn == null) {
            entry.state = DiskState(id, name, 0, true, null, "Impossible d'ouvrir le périphérique USB")
            publishDisks(); return
        }
        if (!conn.claimInterface(bot.intf, true)) {
            conn.close()
            entry.state = DiskState(id, name, 0, true, null, "Interface USB déjà utilisée par une autre application")
            publishDisks(); return
        }
        conn.setInterface(bot.intf)
        // L'ouverture (jusqu'à 20 s pour un disque lent à démarrer) puis l'analyse prennent du temps :
        // l'interface affiche « Analyse du disque… » en attendant.
        entry.state = DiskState(id, name, 0, true, null, null)
        publishDisks()
        val h = engine.openUsb(conn.fileDescriptor, bot.intf.id, bot.epIn.address, bot.epOut.address)
        if (h < 0) {
            conn.releaseInterface(bot.intf); conn.close()
            entry.state = DiskState(id, name, 0, true, null, EngineError(h.toInt()).message)
            publishDisks(); return
        }
        entry.handle = h; entry.connection = conn; entry.intfId = bot.intf.id
        entry.state = scanState(id, name, h, true)
        publishDisks()
    }

    private fun scanState(id: String, name: String, handle: Long, perm: Boolean): DiskState =
        try {
            val scan = ScanResult.parse(engine.scan(handle))
            DiskState(id, name, scan.sizeBytes, perm, scan, null)
        } catch (e: EngineError) {
            DiskState(id, name, 0, perm, null, e.message)
        } catch (e: JSONException) {
            DiskState(id, name, 0, perm, null, "Réponse du moteur illisible")
        }

    /** Éjecte les volumes du disque (force), ferme la pile native, puis seulement la connexion USB. */
    private fun detach(diskId: String) {
        val entry = diskEntries.remove(diskId)
        val vols = mountOrder.filter { mounts[it]?.state?.diskId == diskId }
        if (vols.any { hasUnsyncedWrites(it) }) detachWarner(MSG_DETACH_DURING_WRITE)
        for (vid in vols) unmountLocked(vid)
        if (entry != null) release(entry)
        publishDisks(); publishVolumes()
    }

    /** Ferme la pile native du disque, puis seulement son interface USB. */
    private fun release(entry: DiskEntry) {
        if (entry.handle > 0) engine.closeDisk(entry.handle)
        entry.handle = 0
        entry.connection?.let { c ->
            try {
                val intf = entry.device?.let { d -> (0 until d.interfaceCount).map { d.getInterface(it) }.firstOrNull { it.id == entry.intfId } }
                if (intf != null) c.releaseInterface(intf)
            } finally { c.close() }
        }
        entry.connection = null
    }

    /** Rouvre un disque libéré par une éjection (exécuteur). */
    private fun reopen(entry: DiskEntry) {
        if (entry.handle > 0) return
        val path = entry.imagePath
        if (path != null) {
            val h = engine.openImage(path.toByteArray(Charsets.UTF_8), entry.imageReadOnly)
            if (h > 0) entry.handle = h
        } else if (entry.device != null) {
            attach(entry.device)
        }
    }

    // ---------------------------------------------------------------- volumes

    /** `writable` : écriture demandée (le moteur peut la refuser : [VolumeState.readOnly] et sa raison). */
    suspend fun unlock(diskId: String, partIndex: Int, pass: ByteArray, writable: Boolean = false): VolumeState {
        try {
            return withContext(dispatcher) { mountLocked(diskId, partIndex, pass, writable) }
        } finally {
            pass.fill(0)
        }
    }

    suspend fun mountPlain(diskId: String, partIndex: Int, writable: Boolean = false): VolumeState =
        withContext(dispatcher) { mountLocked(diskId, partIndex, null, writable) }

    private fun mountLocked(diskId: String, partIndex: Int, pass: ByteArray?, writable: Boolean): VolumeState {
        try {
            val existing = mounts.values.firstOrNull { it.state.diskId == diskId && it.state.partIndex == partIndex }
            if (existing != null) {
                // Écriture demandée sur un volume ouvert en lecture seule par choix, ou lecture seule demandée
                // sur un volume en écriture : remontage. Une lecture seule imposée (disque protégé…) ne
                // changerait pas en remontant : le volume ouvert convient.
                val st = existing.state
                val wrongMode = if (writable) st.readOnly && st.roReason == RO_USER_REASON else !st.readOnly
                if (!wrongMode) return existing.state
                remountForMode(existing, writable)
            }
            val entry = diskEntries[diskId]
            if (entry != null && entry.handle <= 0 && entry.state.error == null) reopen(entry) // éjecté
            if (entry == null || entry.handle <= 0) throw EngineError(EngineError.FL_ENODEV)
            val part = entry.state.scan?.parts?.firstOrNull { it.index == partIndex }
                ?: throw EngineError(EngineError.FL_EINVAL)
            if (mounts.size >= MAX_VOLUMES) throw EngineError(EngineError.FL_EBUSY, MSG_TOO_MANY)
            part.luks?.let { luks ->
                if (luks.maxKdfMemoryKib * 1024 > 0.8 * availMem()) {
                    throw EngineError(EngineError.FL_ENOMEM, MSG_LOW_MEMORY)
                }
            }
            val vol = EngineError.check(engine.mount(entry.handle, partIndex, pass, writable))
            val stat = try {
                VolumeStat.parse(engine.statfs(vol))
            } catch (t: Throwable) {
                engine.unmount(vol, true); throw t // la poignée ne doit jamais fuir
            }
            val title = defaultTitle(stat.label, part.sizeBytes)
            val state = VolumeState(
                id = "v" + sessionToken + "-" + counter.incrementAndGet(), diskId = diskId, partIndex = partIndex, title = title,
                totalBytes = stat.totalBytes, freeBytes = stat.freeBytes,
                needsRecovery = stat.needsRecovery, roReason = stat.roReason, readOnly = stat.readOnly,
            )
            mounts[state.id] = MountEntry(state, vol)
            mountOrder.add(state.id)
            publishVolumes()
            return state
        } finally {
            pass?.fill(0) // le natif a déjà mis sa copie à zéro ; la nôtre aussi, sur tous les chemins
        }
    }

    /**
     * Volume ouvert dans l'autre mode que celui demandé : s'il n'a aucun fichier ouvert il est démonté
     * (synchronisé) puis remonté par l'appelant dans le mode demandé ; sinon erreur claire.
     */
    private fun remountForMode(m: MountEntry, writable: Boolean) {
        val rcSync = engine.sync(m.handle)
        if (rcSync < 0) throw EngineError(rcSync, msgModeChange(writable, EngineError(rcSync).message))
        val rc = engine.unmount(m.handle, false)
        if (rc == EngineError.FL_EBUSY) throw EngineError(rc, msgModeChange(writable, MSG_FILES_OPEN))
        forget(m.state.id) // démonté, ou détruit par le moteur sur erreur
        publishVolumes()
        if (rc < 0) throw EngineError(rc, msgModeChange(writable, EngineError(rc).message))
    }

    fun eject(volumeId: String) {
        val disk = mounts[volumeId]?.state?.diskId
        exec.execute { runSafely(disk) { ejectVolumes(listOf(volumeId)) } }
    }

    fun ejectAll() {
        exec.execute { runSafely(null) { ejectVolumes(mountOrder.toList()) } }
    }

    /**
     * Éjection (exécuteur) : synchronisation, démontage normal (journal arrêté, SYNCHRONIZE CACHE, clé
     * effacée), puis libération du disque s'il n'a plus de volume. Un échec (fichier encore ouvert,
     * disque qui refuse la synchronisation) garde le volume ouvert et publie « Éjection impossible : … ». Si le
     * démontage échoue autrement (EIO…), le moteur a déjà détruit le volume : il est oublié, le disque libéré,
     * et l'erreur s'accompagne d'un avertissement de perte possible de données.
     */
    private fun ejectVolumes(ids: List<String>) {
        _ejectError.value = null
        _ejecting.value = true
        try {
            for (id in ids) {
                val m = mounts[id] ?: continue
                pendingSync.remove(id)?.invoke()
                val rcSync = engine.sync(m.handle)
                if (rcSync < 0) { // volume intact côté natif : gardé
                    _ejectError.value = msgEjectFailed(EngineError(rcSync).message ?: "erreur $rcSync")
                    continue
                }
                val rc = engine.unmount(m.handle, false)
                if (rc == EngineError.FL_EBUSY) { // fichier ouvert : volume intact, gardé
                    _ejectError.value = msgEjectFailed(EngineError(rc).message ?: "erreur $rc")
                    continue
                }
                // Toute autre erreur : le moteur a déjà détruit le volume (clé effacée, poignée invalide).
                // On l'oublie et on libère le disque comme après une éjection réussie.
                forget(id)
                if (rc < 0) {
                    val detail = EngineError(rc).message ?: "erreur $rc"
                    _ejectError.value = msgEjectFailed(detail) + " " + msgEjectIncomplete(detail)
                }
                val entry = diskEntries[m.state.diskId]
                if (entry != null && mounts.values.none { it.state.diskId == m.state.diskId }) release(entry)
            }
        } finally {
            _ejecting.value = false
            publishDisks(); publishVolumes()
        }
    }

    /** Retire un volume des tables (démonté, ou en train de l'être). */
    private fun forget(volumeId: String) {
        mounts.remove(volumeId)
        notifyVolumeGone(volumeId)
        mountOrder.remove(volumeId)
        pendingSync.remove(volumeId)?.invoke()
        writeGen.remove(volumeId); syncedGen.remove(volumeId)
    }

    /** Débranchement : démontage de force (le disque n'est plus là). */
    private fun unmountLocked(volumeId: String) {
        val m = mounts[volumeId] ?: return
        forget(volumeId)
        val rc = engine.unmount(m.handle, true)
        if (rc < 0) Log.w(TAG, "unmount($volumeId) : code $rc")
    }

    // ---------------------------------------------------------------- écriture

    /**
     * À appeler après chaque écriture sur le volume (provider) : la synchronisation est (re)programmée
     * [SYNC_DELAY_MS] après cette écriture-ci, sur l'exécuteur ; une rafale n'en déclenche qu'une.
     */
    fun noteWrite(volumeId: String) {
        if (!mounts.containsKey(volumeId)) return
        writeGen.merge(volumeId, 1L, Long::plus)
        val d = delayer ?: realDelayer
        synchronized(pendingSync) {
            pendingSync.remove(volumeId)?.invoke()
            pendingSync[volumeId] = d.schedule(SYNC_DELAY_MS) {
                exec.execute { runSafely(null) { syncLocked(volumeId) } }
            }
        }
    }

    /** Vrai si une écriture du volume n'a pas encore été synchronisée. */
    fun hasUnsyncedWrites(volumeId: String): Boolean = (writeGen[volumeId] ?: 0L) != (syncedGen[volumeId] ?: 0L)

    /** Génération d'écriture du volume, à lire AVANT une synchronisation faite hors du gestionnaire. */
    fun writeGeneration(volumeId: String): Long = writeGen[volumeId] ?: 0L

    /**
     * Une synchronisation réussie, commencée alors que la génération était [gen], met le volume à jour :
     * une écriture arrivée entre-temps (génération plus grande) reste signalée comme non synchronisée.
     */
    fun markSynced(volumeId: String, gen: Long) {
        if (!mounts.containsKey(volumeId)) return
        syncedGen.merge(volumeId, gen) { a, b -> maxOf(a, b) }
    }

    private fun syncLocked(volumeId: String) {
        val m = mounts[volumeId] ?: return
        val gen = writeGen[volumeId] ?: return
        pendingSync.remove(volumeId)
        val rc = engine.sync(m.handle)
        if (rc >= 0) syncedGen[volumeId] = gen else Log.w(TAG, "sync($volumeId) : code $rc")
        refreshLocked(volumeId)
    }

    /**
     * Relit l'état du volume auprès du moteur et le publie s'il a changé : le moteur peut passer le
     * volume en lecture seule en cours de session (erreur d'écriture, somme fausse), et la place libre
     * bouge. Appelé après chaque synchronisation programmée, et par le provider après un échec d'écriture.
     */
    fun refresh(volumeId: String) {
        exec.execute { runSafely(null) { refreshLocked(volumeId) } }
    }

    private fun refreshLocked(volumeId: String) {
        val m = mounts[volumeId] ?: return
        val st = try {
            VolumeStat.parse(engine.statfs(m.handle))
        } catch (e: Exception) {
            return // volume détruit côté natif, ou réponse illisible : l'éjection ou le débranchement le traitera
        }
        val s = m.state.copy(freeBytes = st.freeBytes, readOnly = st.readOnly, roReason = st.roReason)
        if (s == m.state) return
        mounts[volumeId] = MountEntry(s, m.handle)
        publishVolumes()
    }

    /** Titre d'un volume : son étiquette, sinon « Disque <taille> » (taille de la partition). */
    internal fun defaultTitle(label: String, sizeBytes: Long): String =
        label.ifBlank { "Disque " + humanSize(sizeBytes) }

    /**
     * Taille décimale en Go ou Mo, virgule française, une décimale (ex. « 512,1 Go ») : même règle que
     * Fr.humanSize côté Dart (division en double, puis arrondi exact à une décimale, moitié vers le haut).
     */
    fun humanSize(bytes: Long): String {
        if (bytes < 0) return "taille inconnue" // disque hostile : u64 > 2^63
        val isGo = bytes >= 1_000_000_000L
        val value = bytes / (if (isGo) 1e9 else 1e6)
        val text = BigDecimal(value).setScale(1, RoundingMode.HALF_UP).toPlainString().replace('.', ',')
        return "$text ${if (isGo) "Go" else "Mo"}"
    }

    fun volume(id: String): VolumeState? = mounts[id]?.state

    /** Poignée native du volume pour le provider ; 0 si le volume n'existe plus. */
    fun nativeVolume(id: String): Long = mounts[id]?.handle ?: 0L

    @VisibleForTesting
    fun addImageDiskForTests(path: String, readOnly: Boolean = true): String {
        val task = java.util.concurrent.FutureTask {
            val id = "image:" + imageCounter.incrementAndGet()
            val h = engine.openImage(path.toByteArray(Charsets.UTF_8), readOnly)
            val name = path.substringAfterLast('/')
            val entry = DiskEntry(DiskState(id, name, 0, true, null, null), null, imagePath = path, imageReadOnly = readOnly)
            if (h < 0) entry.state = entry.state.copy(error = EngineError(h.toInt()).message)
            else { entry.handle = h; entry.state = scanState(id, name, h, true) }
            diskEntries[id] = entry
            publishDisks()
            id
        }
        exec.execute(task)
        return task.get()
    }

    /** Équivalent d'un débranchement pour un disque de test. */
    @VisibleForTesting
    fun removeDiskForTests(diskId: String) {
        val task = java.util.concurrent.FutureTask { detach(diskId) }
        exec.execute(task)
        task.get()
    }

    // ---------------------------------------------------------------- publication

    private fun publishDisks() {
        _disks.value = diskEntries.values.map { it.state }
    }

    private fun publishVolumes() {
        val list = mountOrder.mapNotNull { mounts[it]?.state }
        _volumes.value = list
        ensureService()
        val ctx = appContext ?: return
        try {
            ctx.contentResolver.notifyChange(DocumentsContract.buildRootsUri(AUTHORITY), null)
        } catch (e: Exception) {
            Log.w(TAG, "notifyChange : ${e.javaClass.simpleName}")
        }
    }

    /**
     * Démarre le service au premier plan si des volumes sont ouverts et qu'il ne tourne pas. Idempotent.
     * Appelé à chaque publication des volumes et depuis MainActivity.onResume : un démarrage refusé
     * (application en arrière-plan) est ainsi retenté dès que c'est de nouveau possible.
     */
    fun ensureService() {
        if (_volumes.value.isEmpty() || serviceRunning()) return
        serviceStarter?.let { it(); return }
        val ctx = appContext ?: return
        try {
            ContextCompat.startForegroundService(ctx, Intent(ctx, VolumeService::class.java))
        } catch (e: Exception) { // ex. démarrage interdit en arrière-plan : les volumes restent ouverts
            Log.w(TAG, "service non démarré : ${e.javaClass.simpleName}")
        }
    }

    /** Les vues de dossiers ouvertes sur ce volume se rafraîchissent (leurs curseurs observent cet URI). */
    private fun notifyVolumeGone(volumeId: String) {
        val ctx = appContext ?: return
        try {
            ctx.contentResolver.notifyChange(
                DocumentsContract.buildChildDocumentsUri(AUTHORITY, DocumentIds.encode(volumeId, "/")), null,
            )
        } catch (e: Exception) {
            Log.w(TAG, "notifyChange : ${e.javaClass.simpleName}")
        }
    }

    /** Erreur inattendue : journalisée (classe seule) et publiée dans DiskState.error du disque concerné. */
    private inline fun runSafely(diskId: String?, block: () -> Unit) {
        try {
            block()
        } catch (e: Exception) {
            val msg = "Erreur inattendue : ${e.javaClass.simpleName}"
            Log.e(TAG, msg)
            val entry = diskId?.let { diskEntries[it] } ?: return
            entry.state = entry.state.copy(error = msg)
            publishDisks()
        }
    }
}

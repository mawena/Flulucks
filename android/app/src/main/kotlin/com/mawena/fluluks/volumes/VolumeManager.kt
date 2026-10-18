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
 */
object VolumeManager {
    private const val TAG = "VolumeManager"
    private const val MAX_VOLUMES = 8 // limite de montages de lwext4
    private const val ACTION_USB_PERMISSION = "com.mawena.fluluks.USB_PERMISSION"
    const val AUTHORITY = "com.mawena.fluluks.documents"

    const val MSG_LOW_MEMORY =
        "Mémoire insuffisante pour déverrouiller ce disque : fermez d'autres applications et réessayez"
    const val MSG_TOO_MANY = "Trop de volumes ouverts : éjectez-en un avant d'en ouvrir un autre"

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

    private class DiskEntry(
        var state: DiskState,
        val device: UsbDevice?,
        var handle: Long = 0,
        var connection: UsbDeviceConnection? = null,
        var intfId: Int = -1,
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
    ) {
        deviceList = devices
        engineOverride = engine
        executorOverride = executor
        this.availMem = availMem
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
        for (vid in mountOrder.toList()) {
            if (mounts[vid]?.state?.diskId == diskId) unmountLocked(vid)
        }
        if (entry != null) {
            if (entry.handle > 0) engine.closeDisk(entry.handle)
            entry.connection?.let { c ->
                try {
                    val intf = entry.device?.let { d -> (0 until d.interfaceCount).map { d.getInterface(it) }.firstOrNull { it.id == entry.intfId } }
                    if (intf != null) c.releaseInterface(intf)
                } finally { c.close() }
            }
        }
        publishDisks(); publishVolumes()
    }

    // ---------------------------------------------------------------- volumes

    suspend fun unlock(diskId: String, partIndex: Int, pass: ByteArray): VolumeState {
        try {
            return withContext(dispatcher) { mountLocked(diskId, partIndex, pass) }
        } finally {
            pass.fill(0)
        }
    }

    suspend fun mountPlain(diskId: String, partIndex: Int): VolumeState =
        withContext(dispatcher) { mountLocked(diskId, partIndex, null) }

    private fun mountLocked(diskId: String, partIndex: Int, pass: ByteArray?): VolumeState {
        try {
            mounts.values.firstOrNull { it.state.diskId == diskId && it.state.partIndex == partIndex }
                ?.let { return it.state }
            val entry = diskEntries[diskId]
            if (entry == null || entry.handle <= 0) throw EngineError(EngineError.FL_ENODEV)
            val part = entry.state.scan?.parts?.firstOrNull { it.index == partIndex }
                ?: throw EngineError(EngineError.FL_EINVAL)
            if (mounts.size >= MAX_VOLUMES) throw EngineError(EngineError.FL_EBUSY, MSG_TOO_MANY)
            part.luks?.let { luks ->
                if (luks.maxKdfMemoryKib * 1024 > 0.8 * availMem()) {
                    throw EngineError(EngineError.FL_ENOMEM, MSG_LOW_MEMORY)
                }
            }
            val vol = EngineError.check(engine.mount(entry.handle, partIndex, pass))
            val stat = try {
                VolumeStat.parse(engine.statfs(vol))
            } catch (t: Throwable) {
                engine.unmount(vol, true); throw t // la poignée ne doit jamais fuir
            }
            val title = defaultTitle(stat.label, part.sizeBytes)
            val state = VolumeState(
                id = "v" + sessionToken + "-" + counter.incrementAndGet(), diskId = diskId, partIndex = partIndex, title = title,
                totalBytes = stat.totalBytes, freeBytes = stat.freeBytes,
                needsRecovery = stat.needsRecovery, roReason = stat.roReason,
            )
            mounts[state.id] = MountEntry(state, vol)
            mountOrder.add(state.id)
            publishVolumes()
            return state
        } finally {
            pass?.fill(0) // le natif a déjà mis sa copie à zéro ; la nôtre aussi, sur tous les chemins
        }
    }

    fun eject(volumeId: String) {
        val disk = mounts[volumeId]?.state?.diskId
        exec.execute { runSafely(disk) { unmountLocked(volumeId); publishVolumes() } }
    }

    fun ejectAll() {
        exec.execute { runSafely(null) { mountOrder.toList().forEach { unmountLocked(it) }; publishVolumes() } }
    }

    private fun unmountLocked(volumeId: String) {
        val m = mounts.remove(volumeId) ?: return
        notifyVolumeGone(volumeId)
        mountOrder.remove(volumeId)
        val rc = engine.unmount(m.handle, true) // éjection = toujours de force
        if (rc < 0) Log.w(TAG, "unmount($volumeId) : code $rc")
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
    fun addImageDiskForTests(path: String): String {
        val task = java.util.concurrent.FutureTask {
            val id = "image:" + imageCounter.incrementAndGet()
            val h = engine.openImage(path.toByteArray(Charsets.UTF_8))
            val name = path.substringAfterLast('/')
            val entry = DiskEntry(DiskState(id, name, 0, true, null, null), null)
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

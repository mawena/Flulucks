package com.mawena.fluluks.bridge

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.provider.DocumentsContract
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.volumes.DiskState
import com.mawena.fluluks.volumes.VolumeManager
import com.mawena.fluluks.volumes.VolumeState
import io.flutter.plugin.common.BinaryMessenger
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

/**
 * Pont Pigeon. Toutes les réponses et tous les appels FlutterApi se font sur le fil principal
 * (scope en Dispatchers.Main) ; le travail du moteur reste sur l'exécuteur de VolumeManager.
 */
class HostApiImpl(
    private val context: Context,
    messenger: BinaryMessenger,
) : FluLuksHostApi {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val events = FluLuksEventsApi(messenger)

    /** À appeler une fois : enregistre l'API hôte et pousse chaque changement d'état vers Flutter. */
    fun attach(messenger: BinaryMessenger) {
        FluLuksHostApi.setUp(messenger, this)
        scope.launch {
            combine(VolumeManager.disks, VolumeManager.volumes) { d, v -> toMsgs(d, v) }
                .collect { (d, v) -> events.onStateChanged(d, v) { /* réponse ignorée */ } }
        }
        scope.launch {
            combine(VolumeManager.ejecting, VolumeManager.ejectError) { e, err -> e to err }
                .collect { (e, err) -> events.onEjectStateChanged(e, err) { /* réponse ignorée */ } }
        }
    }

    fun detach(messenger: BinaryMessenger) {
        FluLuksHostApi.setUp(messenger, null)
        scope.cancel()
    }

    override fun listDisks(): List<DiskMsg> = toMsgs(VolumeManager.disks.value, VolumeManager.volumes.value).first

    override fun listVolumes(): List<VolumeMsg> = VolumeManager.volumes.value.map(::volumeMsg)

    override fun requestPermission(diskId: String) = guarded { VolumeManager.requestPermission(diskId) }

    override fun unlock(
        diskId: String,
        partIndex: Long,
        passphrase: ByteArray,
        writable: Boolean,
        callback: (Result<VolumeMsg>) -> Unit,
    ) {
        scope.launch {
            val result = try {
                Result.success(volumeMsg(VolumeManager.unlock(diskId, partIndex.toInt(), passphrase, writable)))
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                Result.failure(toFlutterError(e))
            } finally {
                passphrase.fill(0)
            }
            callback(result)
        }
    }

    override fun mountPlain(diskId: String, partIndex: Long, writable: Boolean, callback: (Result<VolumeMsg>) -> Unit) {
        scope.launch {
            val result = try {
                Result.success(volumeMsg(VolumeManager.mountPlain(diskId, partIndex.toInt(), writable)))
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                Result.failure(toFlutterError(e))
            }
            callback(result)
        }
    }

    override fun eject(volumeId: String) = guarded { VolumeManager.eject(volumeId) }

    override fun openInFiles(volumeId: String) = guarded {
        if (VolumeManager.volumes.value.none { it.id == volumeId }) {
            throw FlutterError(EngineError.FL_ENOENT.toString(), "Ce volume n'est plus ouvert")
        }
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(
                DocumentsContract.buildRootUri(VolumeManager.AUTHORITY, volumeId),
                "vnd.android.document/root",
            )
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        try {
            context.startActivity(intent)
        } catch (e: ActivityNotFoundException) {
            throw FlutterError(EngineError.FL_ENODEV.toString(), "Aucune application Fichiers disponible")
        }
    }

    private val prefs get() = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** « Ouvrir les disques en lecture seule » : faux par défaut. */
    override fun getReadOnlyPreference(): Boolean = prefs.getBoolean(PREF_READ_ONLY, false)

    override fun setReadOnlyPreference(value: Boolean) = guarded {
        prefs.edit().putBoolean(PREF_READ_ONLY, value).apply()
    }

    // ---------------------------------------------------------------- gestionnaire de fichiers

    /**
     * Stockages : téléphone et cartes SD (place lue sur le système de fichiers), puis volumes ouverts (état
     * publié par VolumeManager, sans appel au moteur sur le fil principal).
     */
    override fun listSources(): List<SourceMsg> = guardedValue {
        val vols = VolumeManager.volumes.value.associateBy { it.id }
        Sources.list().map { src ->
            val v = vols[src.id]
            if (src is DiskSource && v != null) {
                SourceMsg(
                    id = v.id, stableKey = if (v.partUuid.isBlank()) "disk:${v.diskId}#${v.partIndex}" else "disk:${v.partUuid}",
                    kind = "disk", label = v.title, totalBytes = v.totalBytes, freeBytes = v.freeBytes,
                    readOnly = v.readOnly, roReason = v.roReason,
                )
            } else {
                val (free, total) = src.space()
                val ro = src.readOnlyReason
                SourceMsg(
                    id = src.id, stableKey = src.stableKey, kind = Sources.kind(src), label = src.label,
                    totalBytes = total, freeBytes = free, readOnly = ro != null, roReason = ro ?: "",
                )
            }
        }
    }

    override fun hasAllFilesAccess(): Boolean = Sources.hasAllFilesAccess()

    override fun requestAllFilesAccess() = guarded { Sources.requestAllFilesAccess() }

    private inline fun <T> guardedValue(block: () -> T): T {
        try {
            return block()
        } catch (e: FlutterError) {
            throw e
        } catch (e: Throwable) {
            throw toFlutterError(e)
        }
    }

    /** Toute exception d'une méthode synchrone devient une FlutterError française (jamais de trace). */
    private inline fun guarded(block: () -> Unit) {
        try {
            block()
        } catch (e: FlutterError) {
            throw e
        } catch (e: Throwable) {
            throw toFlutterError(e)
        }
    }

    private fun toFlutterError(e: Throwable): FlutterError =
        if (e is EngineError) FlutterError(e.code.toString(), e.message)
        else FlutterError(EngineError.FL_EIO.toString(), "Erreur inattendue : l'opération a échoué")

    companion object {
        const val MSG_NO_PARTITION = "Aucune partition reconnue"
        private const val PREFS = "fluluks"
        private const val PREF_READ_ONLY = "lecture_seule"

        internal fun toMsgs(disks: List<DiskState>, volumes: List<VolumeState>) =
            disks.map { diskMsg(it, volumes) } to volumes.map(::volumeMsg)

        private fun diskMsg(d: DiskState, volumes: List<VolumeState>) = DiskMsg(
            id = d.id,
            name = d.name,
            sizeBytes = d.sizeBytes,
            hasPermission = d.hasPermission,
            error = d.error ?: if (d.scan != null && d.scan.parts.isEmpty()) MSG_NO_PARTITION else null,
            partitions = d.scan?.parts?.map { p ->
                PartitionMsg(
                    index = p.index.toLong(),
                    sizeBytes = p.sizeBytes,
                    content = p.content,
                    label = p.label,
                    supported = p.supported,
                    encrypted = p.encrypted,
                    cipher = p.luks?.cipher,
                    volumeId = volumes.firstOrNull { it.diskId == d.id && it.partIndex == p.index }?.id,
                )
            } ?: emptyList(),
        )

        private fun volumeMsg(v: VolumeState) = VolumeMsg(
            id = v.id, diskId = v.diskId, partIndex = v.partIndex.toLong(), title = v.title,
            totalBytes = v.totalBytes, freeBytes = v.freeBytes,
            needsRecovery = v.needsRecovery, roReason = v.roReason, readOnly = v.readOnly,
        )
    }
}

package com.mawena.fluluks.bridge

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.storage.StorageManager
import android.os.storage.StorageVolume
import android.provider.DocumentsContract
import androidx.core.content.pm.PackageInfoCompat
import com.mawena.fluluks.engine.EngineError
import com.mawena.fluluks.engine.FileType
import com.mawena.fluluks.files.Archives
import com.mawena.fluluks.files.BatchRename
import com.mawena.fluluks.files.Details
import com.mawena.fluluks.files.DiskSource
import com.mawena.fluluks.files.Entry
import com.mawena.fluluks.files.FileContent
import com.mawena.fluluks.files.FileDetails
import com.mawena.fluluks.files.FileSource
import com.mawena.fluluks.files.Files
import com.mawena.fluluks.files.FilesException
import com.mawena.fluluks.files.QuickFilters
import com.mawena.fluluks.files.RenameRule
import com.mawena.fluluks.files.Search
import com.mawena.fluluks.files.SharedPrefStore
import com.mawena.fluluks.files.UiPrefs
import com.mawena.fluluks.files.UiSettings
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.files.SpaceAnalysis
import com.mawena.fluluks.files.SpaceReport
import com.mawena.fluluks.files.Thumbnails
import com.mawena.fluluks.files.TransferState
import com.mawena.fluluks.files.Trash
import com.mawena.fluluks.files.Transfers
import com.mawena.fluluks.files.ViewPrefs
import com.mawena.fluluks.files.ViewSettings
import com.mawena.fluluks.media.AndroidMusic
import com.mawena.fluluks.media.Music
import com.mawena.fluluks.media.VideoActivity
import com.mawena.fluluks.remote.AuthFailed
import com.mawena.fluluks.remote.HostKeyChanged
import com.mawena.fluluks.remote.HostKeyUnknown
import com.mawena.fluluks.remote.PrivateKeys
import com.mawena.fluluks.remote.RemoteIOException
import com.mawena.fluluks.remote.Remotes
import com.mawena.fluluks.remote.ServerConfig
import com.mawena.fluluks.volumes.AutoLock
import com.mawena.fluluks.volumes.DiskState
import com.mawena.fluluks.volumes.FormatOptions
import com.mawena.fluluks.volumes.VolumeManager
import com.mawena.fluluks.volumes.VolumeState
import io.flutter.plugin.common.BinaryMessenger
import java.io.FileNotFoundException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

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

    /** Pourcentage du calcul d'empreinte en cours (null sinon), poussé vers Flutter. */
    private val hashProgress = MutableStateFlow<Int?>(null)

    /** À appeler une fois : enregistre l'API hôte et pousse chaque changement d'état vers Flutter. */
    fun attach(messenger: BinaryMessenger) {
        FluLuksHostApi.setUp(messenger, this)
        if (Music.backend == null) Music.backend = AndroidMusic(context)
        scope.launch {
            combine(VolumeManager.disks, VolumeManager.volumes) { d, v -> toMsgs(d, v) }
                .collect { (d, v) ->
                    Music.checkSources() // musique d'un volume éjecté ou arraché : arrêtée
                    events.onStateChanged(d, v) { /* réponse ignorée */ }
                }
        }
        scope.launch {
            Music.state.collect { (p, message) ->
                val msg = p?.let { PlayerMsg(it.sourceId, it.title, it.index.toLong(), it.count.toLong(), it.playing, it.positionMs, it.durationMs) }
                events.onPlayerChanged(msg, message) { /* réponse ignorée */ }
            }
        }
        scope.launch {
            combine(VolumeManager.ejecting, VolumeManager.ejectError) { e, err -> e to err }
                .collect { (e, err) -> events.onEjectStateChanged(e, err) { /* réponse ignorée */ } }
        }
        scope.launch {
            VolumeManager.formatting.collect { pct -> events.onFormatProgress(pct?.toLong()) { /* réponse ignorée */ } }
        }
        scope.launch {
            Transfers.state.collect { t -> events.onTransferChanged(t?.let(::transferMsg)) { /* réponse ignorée */ } }
        }
        scope.launch {
            hashProgress.collect { pct -> events.onHashProgress(pct?.toLong()) { /* réponse ignorée */ } }
        }
        if (Build.VERSION.SDK_INT >= 30) {
            context.getSystemService(StorageManager::class.java)
                .registerStorageVolumeCallback(context.mainExecutor, storageCallback as StorageManager.StorageVolumeCallback)
        }
    }

    fun detach(messenger: BinaryMessenger) {
        FluLuksHostApi.setUp(messenger, null)
        if (Build.VERSION.SDK_INT >= 30) {
            context.getSystemService(StorageManager::class.java)
                .unregisterStorageVolumeCallback(storageCallback as StorageManager.StorageVolumeCallback)
        }
        scope.cancel()
    }

    /**
     * Carte SD insérée ou retirée : l'état des disques est renvoyé tel quel, Flutter relit alors les
     * stockages (retour à l'accueil si la carte affichée a disparu). Any : la classe n'existe qu'en API 30.
     */
    private val storageCallback: Any? = if (Build.VERSION.SDK_INT >= 30) {
        object : StorageManager.StorageVolumeCallback() {
            override fun onStateChanged(volume: StorageVolume) {
                Music.checkSources() // carte SD retirée
                val (d, v) = toMsgs(VolumeManager.disks.value, VolumeManager.volumes.value)
                events.onStateChanged(d, v) { /* réponse ignorée */ }
            }
        }
    } else {
        null
    }

    override fun listDisks(): List<DiskMsg> = toMsgs(VolumeManager.disks.value, VolumeManager.volumes.value).first

    override fun listVolumes(): List<VolumeMsg> = VolumeManager.volumes.value.map(::volumeMsg)

    override fun requestPermission(diskId: String) = guarded { VolumeManager.requestPermission(diskId) }

    override fun formatDisk(
        diskId: String,
        fs: String,
        encrypted: Boolean,
        label: String,
        passphrase: ByteArray?,
        callback: (Result<Unit>) -> Unit,
    ) {
        scope.launch {
            val result = try {
                VolumeManager.format(diskId, FormatOptions(fs, encrypted, label), passphrase)
                Result.success(Unit)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                Result.failure(toFlutterError(e))
            } finally {
                passphrase?.fill(0)
            }
            callback(result)
        }
    }

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
        Sources.list().map { src -> sourceMsg(src, vols[src.id]) }
    }

    private fun sourceMsg(src: FileSource, v: VolumeState?): SourceMsg =
        if (src is DiskSource && v != null) {
            SourceMsg(
                id = v.id, stableKey = if (v.partUuid.isBlank()) "disk:${v.diskId}#${v.partIndex}" else "disk:${v.partUuid}",
                kind = "disk", label = v.title, totalBytes = v.totalBytes, freeBytes = v.freeBytes,
                readOnly = v.readOnly, roReason = v.roReason, encrypted = v.encrypted,
            )
        } else {
            val (free, total) = src.space()
            val ro = src.readOnlyReason
            SourceMsg(
                id = src.id, stableKey = src.stableKey, kind = Sources.kind(src), label = src.label,
                totalBytes = total, freeBytes = free, readOnly = ro != null, roReason = ro ?: "", encrypted = false,
            )
        }

    private fun serverMsg(c: ServerConfig) = RemoteServerMsg(
        id = c.id, protocol = c.protocol, name = c.name, host = c.host, port = c.port.toLong(), user = c.user,
        startPath = c.startPath, remember = c.remember, hostKey = c.hostKey, keyInfo = c.keyInfo,
        hasPassword = c.hasPassword,
        connected = Remotes.isConnected(c.id),
    )

    override fun listServers(): List<RemoteServerMsg> = guardedValue { Remotes.servers().list().map(::serverMsg) }

    override fun saveServer(server: RemoteServerMsg, password: ByteArray?, keyPassphrase: ByteArray?): RemoteServerMsg =
        guardedValue {
            try {
                // fiche modifiée : la connexion ouverte avec l'ancienne est fermée
                if (server.id.isNotEmpty()) Remotes.disconnect(server.id)
                val c = ServerConfig(
                    server.id, server.protocol, server.name, server.host, server.port.toInt(), server.user, server.startPath,
                    server.remember, Remotes.servers().get(server.id)?.hostKey, Remotes.servers().get(server.id)?.keyInfo,
                )
                serverMsg(Remotes.servers().save(c, password, keyPassphrase))
            } catch (e: IllegalArgumentException) {
                throw FilesException(EngineError.FL_EINVAL, e.message ?: "Fiche invalide")
            } finally {
                password?.fill(0)
                keyPassphrase?.fill(0)
            }
        }

    override fun deleteServer(serverId: String) = guardedValue {
        Remotes.disconnect(serverId)
        Remotes.servers().delete(serverId)
    }

    override fun connectServer(serverId: String, password: ByteArray?, callback: (Result<SourceMsg>) -> Unit) =
        io(callback) {
            try {
                sourceMsg(Remotes.connect(serverId, password), null)
            } catch (e: HostKeyUnknown) {
                throw FlutterError("hostkey-unknown", e.message, e.fingerprint)
            } catch (e: HostKeyChanged) {
                throw FlutterError("hostkey-changed", e.message, null)
            } catch (e: AuthFailed) {
                throw FlutterError("auth", e.message, e.missing)
            } catch (e: FlutterError) {
                throw e
            } catch (e: Exception) {
                throw Files.toFilesException(e, "openDocument")
            } finally {
                password?.fill(0)
            }
        }

    override fun disconnectServer(serverId: String) = guardedValue {
        Remotes.disconnect(serverId)
        Music.checkSources()
    }

    override fun trustHostKey(serverId: String, fingerprint: String) = guardedValue { Remotes.trustHostKey(serverId, fingerprint) }

    override fun forgetHostKey(serverId: String) = guardedValue { Remotes.forgetHostKey(serverId) }

    override fun importPrivateKey(serverId: String, callback: (Result<String?>) -> Unit) =
        FilePicker.pick { uri ->
            if (uri == null) return@pick callback(Result.success(null))
            io(callback) {
                Files.guard("openDocument") {
                    val key = context.contentResolver.openInputStream(uri)?.use { readKeyFile(it) }
                        ?: throw RemoteIOException(PrivateKeys.MSG_UNREADABLE)
                    Remotes.importPrivateKey(serverId, key)
                }
            }
        }

    /** Fichier de clé : 64 Kio au plus (une clé RSA de 16 384 bits en fait moins de 13). */
    private fun readKeyFile(input: java.io.InputStream): ByteArray {
        val buf = ByteArray(MAX_KEY_FILE + 1)
        var n = 0
        while (n < buf.size) {
            val k = input.read(buf, n, buf.size - n)
            if (k < 0) break
            n += k
        }
        if (n > MAX_KEY_FILE) {
            buf.fill(0)
            throw RemoteIOException(PrivateKeys.MSG_UNREADABLE)
        }
        return buf.copyOf(n).also { buf.fill(0) }
    }

    override fun forgetPrivateKey(serverId: String) = guardedValue { Remotes.forgetPrivateKey(serverId) }

    override fun getAutoLockMinutes(): Long = AutoLock.readPref(context).toLong()

    override fun setAutoLockMinutes(minutes: Long) = guarded { AutoLock.writePref(context, minutes.toInt()) }

    override fun hasAllFilesAccess(): Boolean = Sources.hasAllFilesAccess()

    override fun requestAllFilesAccess() = guarded { Sources.requestAllFilesAccess() }

    override fun listDir(sourceId: String, path: String, showHidden: Boolean, callback: (Result<List<EntryMsg>>) -> Unit) =
        io(callback) { Files.list(sourceId, path, showHidden).map(::entryMsg) }

    override fun createFolder(sourceId: String, parent: String, name: String, callback: (Result<String>) -> Unit) =
        io(callback) { Files.createFolder(sourceId, parent, name) }

    override fun rename(sourceId: String, path: String, newName: String, callback: (Result<String>) -> Unit) =
        io(callback) { Files.rename(sourceId, path, newName) }

    override fun deleteEntries(sourceId: String, paths: List<String>, permanent: Boolean, callback: (Result<Unit>) -> Unit) =
        io(callback) { Files.delete(sourceId, paths, permanent) }

    /** Corbeille : les éléments de plus de 30 jours sont purgés à l'ouverture. */
    override fun listTrash(sourceId: String, callback: (Result<List<TrashItemMsg>>) -> Unit) =
        io(callback) {
            Files.guard("queryChildDocuments") {
                val src = Files.source(sourceId)
                Trash.purgeExpired(src)
                Trash.list(src).map { TrashItemMsg(it.id, it.name, it.originalPath, it.deletedAtMs, it.size, it.isDir) }
            }
        }

    override fun restoreTrash(sourceId: String, ids: List<String>, callback: (Result<Unit>) -> Unit) =
        io(callback) { Files.guard("moveDocument") { Trash.restore(Files.source(sourceId), ids) } }

    override fun purgeTrash(sourceId: String, ids: List<String>, callback: (Result<Unit>) -> Unit) =
        io(callback) { Files.guard("deleteDocument") { Trash.purge(Files.source(sourceId), ids) } }

    override fun emptyTrash(sourceId: String, callback: (Result<Unit>) -> Unit) =
        io(callback) { Files.guard("deleteDocument") { Trash.empty(Files.source(sourceId)) } }

    override fun openWith(sourceId: String, path: String, callback: (Result<Unit>) -> Unit) =
        startPrepared(callback) {
            val chooser = Files.viewIntent(sourceId, path)
            if (!Files.hasViewer(context, chooser)) throw FlutterError(EngineError.FL_ENOTSUP.toString(), Files.MSG_NO_VIEWER)
            chooser
        }

    override fun playVideo(sourceId: String, path: String, callback: (Result<Unit>) -> Unit) =
        startPrepared(callback) {
            Files.guard("openDocument") { Files.source(sourceId).stat(path) ?: throw java.io.FileNotFoundException("Document introuvable") }
            VideoActivity.intent(context, sourceId, path)
        }

    override fun playAudio(sourceId: String, paths: List<String>, startIndex: Long, callback: (Result<Unit>) -> Unit) =
        io(callback) { Files.guard("openDocument") { Music.play(sourceId, paths, startIndex.toInt()) } }

    override fun playerCommand(command: String, positionMs: Long?) = guarded { Music.command(command, positionMs) }

    override fun share(sourceId: String, paths: List<String>, callback: (Result<Unit>) -> Unit) =
        startPrepared(callback) { Files.shareIntent(sourceId, paths) }

    /**
     * Intent préparé hors du fil principal (stat du moteur, qui peut attendre une synchronisation en cours),
     * puis activité lancée sur le fil principal.
     */
    private fun startPrepared(callback: (Result<Unit>) -> Unit, build: () -> Intent) {
        scope.launch {
            val result = try {
                val intent = withContext(Dispatchers.IO) { build() }
                context.startActivity(intent)
                Result.success(Unit)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                Result.failure(toFlutterError(e))
            }
            callback(result)
        }
    }

    override fun startTransfer(srcId: String, paths: List<String>, dstId: String, dstDir: String, move: Boolean) = guarded {
        Transfers.start(srcId, paths, dstId, dstDir, move)
    }

    override fun startCompress(srcId: String, paths: List<String>, dstDir: String, name: String) = guarded {
        Archives.startCompress(srcId, paths, dstDir, name)
    }

    override fun startExtract(srcId: String, archive: String, dstId: String, dstDir: String, intoNewFolder: Boolean) = guarded {
        Archives.startExtract(srcId, archive, dstId, dstDir, intoNewFolder)
    }

    override fun cancelTransfer() = Transfers.cancel()

    override fun search(sourceId: String, path: String, query: String, showHidden: Boolean, callback: (Result<SearchResultMsg>) -> Unit) =
        io(callback) {
            val r = Search.run(sourceId, path, query, showHidden)
            SearchResultMsg(r.entries.map(::entryMsg), r.truncated)
        }

    override fun analyzeSpace(sourceId: String, path: String, callback: (Result<SpaceReportMsg>) -> Unit) =
        io(callback) { spaceMsg(SpaceAnalysis.run(sourceId, path)) }

    override fun spaceChildren(sourceId: String, path: String, callback: (Result<SpaceReportMsg>) -> Unit) =
        io(callback) { spaceMsg(SpaceAnalysis.children(sourceId, path)) }

    override fun cancelAnalysis() = SpaceAnalysis.cancel()

    override fun details(sourceId: String, path: String, callback: (Result<DetailsMsg>) -> Unit) =
        io(callback) {
            Files.guard("queryChildDocuments") {
                detailsMsg(Files.source(sourceId).details(path) ?: throw FileNotFoundException("Document introuvable"))
            }
        }

    override fun folderSize(sourceId: String, path: String, callback: (Result<FolderSizeMsg>) -> Unit) =
        io(callback) {
            Files.guard("queryChildDocuments") {
                val s = FileDetails.folderSize(Files.source(sourceId), path)
                FolderSizeMsg(size = s.size, files = s.files.toLong(), dirs = s.dirs.toLong(), truncated = s.truncated)
            }
        }

    override fun readImage(sourceId: String, path: String, callback: (Result<ByteArray>) -> Unit) =
        io(callback) { Files.guard("openDocument") { FileContent.readImage(Files.source(sourceId), path) } }

    override fun readText(sourceId: String, path: String, callback: (Result<TextFileMsg>) -> Unit) =
        io(callback) {
            Files.guard("openDocument") {
                val d = FileContent.readText(Files.source(sourceId), path)
                TextFileMsg(d.text, d.bom, d.crlf, d.mtime, d.size, d.readOnlyReason)
            }
        }

    override fun writeText(
        sourceId: String, path: String, text: String, bom: Boolean, crlf: Boolean, expectedMtime: Long, expectedSize: Long,
        force: Boolean, callback: (Result<TextSaveMsg>) -> Unit,
    ) = io(callback) {
        Files.guard("createDocument") {
            val r = FileContent.writeText(Files.source(sourceId), path, text, bom, crlf, expectedMtime, expectedSize, force)
            TextSaveMsg(r.saved, r.changed, r.mtime, r.size)
        }
    }

    override fun sha256(sourceId: String, path: String, callback: (Result<String?>) -> Unit) =
        io(callback) {
            try {
                Files.guard("openDocument") { FileDetails.sha256(Files.source(sourceId), path) { hashProgress.value = it } }
            } finally {
                hashProgress.value = null
            }
        }

    override fun cancelHash() = FileDetails.cancel()

    override fun quickFilter(filter: String, callback: (Result<List<FilterItemMsg>>) -> Unit) =
        io(callback) {
            Files.guard("queryChildDocuments") {
                QuickFilters.query(context, filter).map { FilterItemMsg(it.sourceId, it.path, it.name, it.size, it.mtime) }
            }
        }

    override fun previewRename(sourceId: String, paths: List<String>, rule: RenameRuleMsg, callback: (Result<List<RenamePreviewMsg>>) -> Unit) =
        io(callback) {
            Files.guard("renameDocument") {
                BatchRename.preview(Files.source(sourceId), paths, renameRule(rule)).map { RenamePreviewMsg(it.path, it.newName, it.conflict) }
            }
        }

    override fun applyRename(sourceId: String, paths: List<String>, rule: RenameRuleMsg, callback: (Result<Unit>) -> Unit) =
        io(callback) { Files.guard("renameDocument") { BatchRename.apply(Files.source(sourceId), paths, renameRule(rule)) } }

    override fun cancelSearch() = Search.cancel()

    override fun thumbnail(sourceId: String, path: String, sizePx: Long, callback: (Result<ByteArray?>) -> Unit) =
        io(callback, thumbnailDispatcher) { Thumbnails.get(sourceId, path, sizePx.toInt()) }

    /** Vignettes à part : un défilement rapide n'occupe jamais les fils de listDir, search, etc. */
    private val thumbnailDispatcher = Dispatchers.IO.limitedParallelism(Thumbnails.PARALLEL)

    private val viewPrefs by lazy { ViewPrefs(SharedPrefStore(context)) }

    override fun getViewPrefs(): ViewPrefsMsg = viewPrefs.get().let { ViewPrefsMsg(it.sort, it.ascending, it.grid, it.showHidden, it.builtInViewers) }

    override fun setViewPrefs(prefs: ViewPrefsMsg) = guarded {
        viewPrefs.set(ViewSettings(prefs.sort, prefs.ascending, prefs.grid, prefs.showHidden, prefs.builtInViewers))
    }

    override fun listFavorites(): List<FavoriteMsg> = guardedValue {
        viewPrefs.favorites().map { FavoriteMsg(it.key, it.path, it.label, it.sourceId) }
    }

    override fun addFavorite(sourceId: String, path: String, label: String) = guarded { viewPrefs.add(sourceId, path, label) }

    override fun removeFavorite(key: String, path: String) = guarded { viewPrefs.remove(key, path) }

    private val uiPrefs by lazy { UiPrefs(SharedPrefStore(context)) }

    override fun getUiPrefs(): UiPrefsMsg = uiPrefs.get().let { UiPrefsMsg(it.theme, it.haptics, it.welcomeSeen) }

    override fun setUiPrefs(prefs: UiPrefsMsg) = guarded { uiPrefs.set(UiSettings(prefs.theme, prefs.haptics, prefs.welcomeSeen)) }

    /** Version du paquet et appareil, pour « À propos ». */
    override fun getAppInfo(): AppInfoMsg = guardedValue {
        @Suppress("DEPRECATION") // getPackageInfo(String, PackageInfoFlags) n'existe qu'à partir de l'API 33
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        AppInfoMsg(
            versionName = info.versionName.orEmpty(),
            versionCode = PackageInfoCompat.getLongVersionCode(info),
            androidRelease = Build.VERSION.RELEASE.orEmpty(),
            device = Build.MODEL.orEmpty(),
        )
    }

    /** Travail du gestionnaire de fichiers hors du fil principal ; réponse sur le fil principal. */
    private fun <T> io(callback: (Result<T>) -> Unit, dispatcher: CoroutineDispatcher = Dispatchers.IO, block: () -> T) {
        scope.launch {
            val result = try {
                Result.success(withContext(dispatcher) { block() })
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                Result.failure(toFlutterError(e))
            }
            callback(result)
        }
    }

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

    companion object {
        /** Erreur rendue à Flutter : code fl_err et message français, jamais le détail d'une exception. */
        internal fun toFlutterError(e: Throwable): FlutterError = when (e) {
            is FlutterError -> e // déjà en français (« Aucune application… »)
            is EngineError -> FlutterError(e.code.toString(), e.message)
            is FilesException -> FlutterError(e.code.toString(), e.message)
            is ActivityNotFoundException -> FlutterError(EngineError.FL_ENOTSUP.toString(), Files.MSG_NO_VIEWER)
            else -> FlutterError(EngineError.FL_EIO.toString(), "Erreur inattendue : l'opération a échoué")
        }

        const val MSG_NO_PARTITION = "Aucune partition reconnue"
        private const val MAX_KEY_FILE = 64 * 1024
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

        internal fun entryMsg(e: Entry) = EntryMsg(name = e.name, path = e.path, size = e.size, mtime = e.mtime, type = typeName(e.type))

        private fun typeName(t: FileType) = when (t) {
            FileType.DIR -> "dir"
            FileType.FILE -> "file"
            FileType.SYMLINK -> "link"
            FileType.OTHER -> "other"
        }

        private fun renameRule(r: RenameRuleMsg) = RenameRule(
            mode = r.mode, base = r.base, start = r.start.toInt(), find = r.find, replace = r.replace, caseSensitive = r.caseSensitive,
        )

        private fun detailsMsg(d: Details) = DetailsMsg(
            name = d.name, path = d.path, type = typeName(d.type), size = d.size, mtime = d.mtime, mode = d.mode?.toLong(),
            uid = d.uid, gid = d.gid, permissions = d.permissions, fs = d.fs,
        )

        private fun spaceMsg(r: SpaceReport) = SpaceReportMsg(
            path = r.path, totalBytes = r.totalBytes, fileCount = r.fileCount.toLong(),
            children = r.children.map { SpaceItemMsg(it.name, it.path, it.isDir, it.size, it.fileCount.toLong()) },
            biggest = r.biggest.map(::entryMsg), truncated = r.truncated, otherCount = r.otherCount.toLong(),
            otherBytes = r.otherBytes,
        )

        internal fun transferMsg(t: TransferState) = TransferMsg(
            move = t.move, currentName = t.currentName, fileIndex = t.fileIndex.toLong(), fileCount = t.fileCount.toLong(),
            doneBytes = t.doneBytes, totalBytes = t.totalBytes, finished = t.finished, cancelled = t.cancelled, error = t.error,
            elapsedMs = t.elapsedMs, kind = t.kind,
        )

        private fun volumeMsg(v: VolumeState) = VolumeMsg(
            id = v.id, diskId = v.diskId, partIndex = v.partIndex.toLong(), title = v.title,
            totalBytes = v.totalBytes, freeBytes = v.freeBytes,
            needsRecovery = v.needsRecovery, roReason = v.roReason, readOnly = v.readOnly,
        )
    }
}

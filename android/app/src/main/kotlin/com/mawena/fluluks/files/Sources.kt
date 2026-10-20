package com.mawena.fluluks.files

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.storage.StorageManager
import android.provider.Settings
import androidx.core.content.ContextCompat
import com.mawena.fluluks.volumes.VolumeManager
import java.lang.ref.WeakReference

/** Les stockages du gestionnaire : téléphone, cartes SD montées, volumes ouverts des disques USB. */
object Sources {
    const val PHONE = "phone"
    private const val REQUEST_STORAGE = 1002

    @Volatile private var app: Context? = null
    @Volatile private var activity: WeakReference<Activity>? = null

    fun init(context: Context) {
        val a = context.applicationContext
        app = a
        Files.context = a
        if (context is Activity) activity = WeakReference(context)
    }

    /** Accès à tous les fichiers (API 30 et plus), ou permissions de stockage (API 26 à 29). */
    fun hasAllFilesAccess(): Boolean {
        val ctx = app ?: return false
        return if (Build.VERSION.SDK_INT >= 30) Environment.isExternalStorageManager()
        else ContextCompat.checkSelfPermission(ctx, Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
            PackageManager.PERMISSION_GRANTED
    }

    /** Ouvre l'écran système « Accès à tous les fichiers » de Portail, ou demande les permissions (API < 30). */
    fun requestAllFilesAccess() {
        val ctx = app ?: return
        if (Build.VERSION.SDK_INT >= 30) {
            val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + ctx.packageName))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            try {
                ctx.startActivity(intent)
            } catch (e: Exception) { // écran absent sur certains appareils : liste générale
                ctx.startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            }
        } else {
            activity?.get()?.requestPermissions(
                arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE),
                REQUEST_STORAGE,
            )
        }
    }

    fun list(): List<FileSource> {
        val out = ArrayList<FileSource>()
        val ctx = app
        if (ctx != null) {
            @Suppress("DEPRECATION")
            out += LocalSource(PHONE, PHONE, "Téléphone", Environment.getExternalStorageDirectory(), ::hasAllFilesAccess)
            if (Build.VERSION.SDK_INT >= 30) {
                val sm = ctx.getSystemService(StorageManager::class.java)
                for (sv in sm.storageVolumes) {
                    val dir = sv.directory ?: continue
                    val uuid = sv.uuid ?: continue
                    if (!sv.isRemovable || sv.state != Environment.MEDIA_MOUNTED) continue
                    val label = sv.getDescription(ctx)?.takeIf { it.isNotBlank() } ?: "Carte SD"
                    out += LocalSource("sd:$uuid", "sd:$uuid", label, dir, ::hasAllFilesAccess)
                }
            }
        }
        for (v in VolumeManager.volumes.value) out += DiskSource(v.id)
        return out
    }

    fun get(id: String): FileSource? = list().firstOrNull { it.id == id }

    /** Type affiché : `phone`, `sd` ou `disk`. */
    fun kind(s: FileSource): String = when {
        s.id == PHONE -> "phone"
        s is LocalSource -> "sd"
        else -> "disk"
    }
}

package com.mawena.fluluks.bridge

import android.app.Activity
import android.content.Intent
import android.net.Uri
import java.lang.ref.WeakReference

/**
 * Sélecteur de fichier d'Android (ACTION_OPEN_DOCUMENT) pour le pont, qui n'a que le contexte de l'application :
 * l'activité visible s'enregistre ([activity]) et rend le résultat par [deliver]. Une seule demande à la fois ; une
 * nouvelle annule la précédente (rendue null).
 */
object FilePicker {
    const val REQUEST = 4207

    @Volatile var activity: WeakReference<Activity>? = null
    private var pending: ((Uri?) -> Unit)? = null

    /** Fil principal. Sans activité visible : [onResult] reçoit null tout de suite. */
    fun pick(onResult: (Uri?) -> Unit) {
        val a = activity?.get() ?: return onResult(null)
        pending?.invoke(null)
        pending = onResult
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        a.startActivityForResult(intent, REQUEST)
    }

    /** Depuis onActivityResult ; vrai si la demande était la nôtre. */
    fun deliver(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode != REQUEST) return false
        val p = pending
        pending = null
        p?.invoke(if (resultCode == Activity.RESULT_OK) data?.data else null)
        return true
    }
}

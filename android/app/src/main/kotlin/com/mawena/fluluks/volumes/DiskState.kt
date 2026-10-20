package com.mawena.fluluks.volumes

import com.mawena.fluluks.engine.ScanResult

/** `id` = deviceName USB (ou identifiant synthétique pour une image de test). */
data class DiskState(
    val id: String,
    val name: String,
    val sizeBytes: Long,
    val hasPermission: Boolean,
    val scan: ScanResult?,
    val error: String?,
)

/** `id` = "v" + jeton de session + "-" + compteur. */
data class VolumeState(
    val id: String,
    val diskId: String,
    val partIndex: Int,
    val title: String,
    val totalBytes: Long,
    val freeBytes: Long,
    val needsRecovery: Boolean,
    val roReason: String,
    val readOnly: Boolean = true,
    /** UUID de la partition (clé stable des favoris) ; vide si le disque n'en donne pas. */
    val partUuid: String = "",
)

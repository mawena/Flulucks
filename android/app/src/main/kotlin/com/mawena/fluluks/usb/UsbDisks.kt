package com.mawena.fluluks.usb

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface

/** Interface « stockage de masse, SCSI, Bulk-Only » (8 / 6 / 0x50) et ses deux points d'accès bulk. */
class MassStorageInterface(val intf: UsbInterface, val epIn: UsbEndpoint, val epOut: UsbEndpoint)

object UsbDisks {
    const val CLASS_MASS_STORAGE = 8
    const val SUBCLASS_SCSI = 6
    const val PROTOCOL_BOT = 0x50
    const val UAS_ONLY_MESSAGE = "Ce boîtier USB n'est pas pris en charge (UAS seulement)"

    /** Vrai si le périphérique expose au moins une interface de classe 8 (candidat disque). */
    fun isMassStorageCandidate(device: UsbDevice): Boolean =
        (0 until device.interfaceCount).any { device.getInterface(it).interfaceClass == CLASS_MASS_STORAGE }

    /** 8/6/0x50 sur alternateSetting 0 avec un bulk IN et un bulk OUT ; null sinon (ex. UAS 0x62). */
    fun findBotInterface(device: UsbDevice): MassStorageInterface? {
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            if (intf.interfaceClass != CLASS_MASS_STORAGE ||
                intf.interfaceSubclass != SUBCLASS_SCSI ||
                intf.interfaceProtocol != PROTOCOL_BOT ||
                intf.alternateSetting != 0
            ) continue
            var epIn: UsbEndpoint? = null
            var epOut: UsbEndpoint? = null
            for (e in 0 until intf.endpointCount) {
                val ep = intf.getEndpoint(e)
                if (ep.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
                if (ep.direction == UsbConstants.USB_DIR_IN) { if (epIn == null) epIn = ep }
                else if (epOut == null) epOut = ep
            }
            if (epIn != null && epOut != null) return MassStorageInterface(intf, epIn, epOut)
        }
        return null
    }

    fun displayName(device: UsbDevice): String =
        (device.productName ?: "").trim().ifEmpty { "Disque USB" }
}

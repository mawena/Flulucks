package com.mawena.fluluks

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import com.mawena.fluluks.files.Sources
import com.mawena.fluluks.volumes.VolumeManager
import com.mawena.fluluks.bridge.HostApiImpl
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine

class MainActivity : FlutterActivity() {
    private var hostApi: HostApiImpl? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        val messenger = flutterEngine.dartExecutor.binaryMessenger
        hostApi = HostApiImpl(applicationContext, messenger).also { it.attach(messenger) }
    }

    override fun cleanUpFlutterEngine(flutterEngine: FlutterEngine) {
        hostApi?.detach(flutterEngine.dartExecutor.binaryMessenger)
        hostApi = null
        super.cleanUpFlutterEngine(flutterEngine)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        VolumeManager.init(this)
        Sources.init(this)
        handleUsbIntent(intent)
        requestNotificationPermission()
    }

    /** Un démarrage du service refusé en arrière-plan est retenté au retour au premier plan. */
    override fun onResume() {
        super.onResume()
        VolumeManager.ensureService()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleUsbIntent(intent)
    }

    private fun handleUsbIntent(intent: Intent?) {
        if (intent?.action != UsbManager.ACTION_USB_DEVICE_ATTACHED) return
        with(VolumeManager) { intent.usbDevice() }?.let { VolumeManager.onUsbAttached(it) }
    }

    /** POST_NOTIFICATIONS : demandée à l'exécution dès API 33 (la notification « Éjecter » en dépend). */
    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1001)
        }
    }
}

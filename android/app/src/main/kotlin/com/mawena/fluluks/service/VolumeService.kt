package com.mawena.fluluks.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import com.mawena.fluluks.MainActivity
import com.mawena.fluluks.R
import com.mawena.fluluks.volumes.AutoLock
import com.mawena.fluluks.volumes.VolumeManager
import com.mawena.fluluks.volumes.VolumeState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

/**
 * Service au premier plan tant qu'au moins un volume est ouvert. Il observe VolumeManager.volumes :
 * il met la notification à jour et s'arrête quand le dernier volume est fermé.
 * L'action « Éjecter » appelle VolumeManager.ejectAll() (exécuté hors du fil principal) ; pendant
 * l'éjection, la notification dit « Écriture des données en attente… », puis l'échec éventuel.
 */
class VolumeService : Service() {
    companion object {
        /** Vrai entre onCreate et onDestroy (utilisé par les tests instrumentés). */
        @Volatile @JvmStatic var running: Boolean = false
        const val ACTION_EJECT_ALL = "com.mawena.fluluks.EJECT_ALL"
        private const val CHANNEL_ID = "disques_ouverts"
        private const val WARN_CHANNEL_ID = "avertissements"
        private const val NOTIF_ID = 1
        private const val WARN_NOTIF_ID = 2
        private const val TAG = "VolumeService"

        fun notificationText(volumes: List<VolumeState>, ejecting: Boolean = false, ejectError: String? = null): String = when {
            ejecting -> VolumeManager.MSG_EJECTING
            ejectError != null -> ejectError
            volumes.size == 0 -> "Aucun disque ouvert"
            volumes.size == 1 -> "Disque ouvert : ${volumes[0].title}"
            else -> "${volumes.size} disques ouverts"
        }

        /** Avertissement (importance haute), ex. disque débranché pendant une écriture. */
        fun warn(context: Context, message: String) {
            try {
                val nm = context.getSystemService(NotificationManager::class.java)
                nm.createNotificationChannel(
                    NotificationChannel(WARN_CHANNEL_ID, "Avertissements", NotificationManager.IMPORTANCE_HIGH)
                )
                val n = Notification.Builder(context, WARN_CHANNEL_ID)
                    .setSmallIcon(R.drawable.ic_portail)
                    .setContentTitle("Portail")
                    .setContentText(message)
                    .setStyle(Notification.BigTextStyle().bigText(message))
                    .setAutoCancel(true)
                    .build()
                nm.notify(WARN_NOTIF_ID, n)
            } catch (e: RuntimeException) { // permission de notification refusée, etc.
                Log.w(TAG, "avertissement non affiché : ${e.javaClass.simpleName}")
            }
        }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var lastStartId = 0
    private var foregroundOk = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        VolumeManager.init(this)
        AutoLock.bind(this) { msg -> warn(this, msg) }
        running = true
        createChannel()
        val notif = buildNotification(VolumeManager.volumes.value)
        foregroundOk = startAsForeground(notif)
        scope.launch {
            combine(VolumeManager.ejecting, VolumeManager.ejectError) { e, err -> e to err }.collect {
                val list = VolumeManager.volumes.value
                if (list.isNotEmpty()) foregroundOk = startAsForeground(buildNotification(list))
            }
        }
        scope.launch {
            // verrouillage automatique : contrôle périodique tant que le service vit (un volume est ouvert)
            while (true) {
                delay(AutoLock.PERIOD_MS)
                AutoLock.check(SystemClock.elapsedRealtime())
            }
        }
        scope.launch {
            VolumeManager.volumes.collect { list ->
                if (list.isEmpty()) {
                    stopForeground(STOP_FOREGROUND_REMOVE)
                    if (lastStartId > 0) stopSelf(lastStartId) else stopSelf()
                } else {
                    // Idempotent (même id) : met à jour la notification et garantit l'état « premier plan »
                    // même si un redémarrage a croisé un stopSelf en attente.
                    foregroundOk = startAsForeground(buildNotification(list))
                }
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        lastStartId = startId
        if (intent?.action == ACTION_EJECT_ALL) VolumeManager.ejectAll()
        // Toute commande de démarrage doit être suivie de startForeground (startForegroundService).
        val list = VolumeManager.volumes.value
        foregroundOk = startAsForeground(buildNotification(list))
        if (!foregroundOk || list.isEmpty()) {
            // startForeground a été tenté avant l'arrêt ; stopSelf(startId) ne stoppe que si aucun
            // démarrage plus récent n'est arrivé.
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf(startId)
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        running = false
        scope.cancel()
        super.onDestroy()
    }

    /**
     * connectedDevice exige, dès API 34, une permission USB accordée ; pour un volume issu d'une image
     * (tests) il n'y en a pas et startForeground lève SecurityException. API < 34 : on réessaie sans
     * type. API >= 34 : on journalise et on continue sans service au premier plan (les volumes restent
     * ouverts, mais le processus n'est plus protégé) ; le service s'arrête.
     */
    private fun startAsForeground(n: Notification): Boolean {
        return try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            } else startForeground(NOTIF_ID, n)
            true
        } catch (e: SecurityException) {
            if (Build.VERSION.SDK_INT < 34) {
                try { startForeground(NOTIF_ID, n); true } catch (e2: RuntimeException) { fail(e2) }
            } else fail(e)
        } catch (e: RuntimeException) {
            fail(e)
        }
    }

    private fun fail(e: Exception): Boolean {
        Log.w(TAG, "service au premier plan impossible : ${e.javaClass.simpleName}")
        return false
    }

    private fun createChannel() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Disques ouverts", NotificationManager.IMPORTANCE_LOW)
        )
    }

    private fun buildNotification(volumes: List<VolumeState>): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val eject = PendingIntent.getService(
            this, 1, Intent(this, VolumeService::class.java).setAction(ACTION_EJECT_ALL),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_portail)
            .setContentTitle("Portail")
            .setContentText(notificationText(volumes, VolumeManager.ejecting.value, VolumeManager.ejectError.value))
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, "Éjecter", eject).build())
            .build()
    }
}

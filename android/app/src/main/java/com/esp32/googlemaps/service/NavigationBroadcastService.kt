package com.esp32.googlemaps.service

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Color
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.esp32.googlemaps.MainActivity
import com.esp32.googlemaps.R
import com.esp32.googlemaps.bluetooth.BluetoothController
import com.esp32.googlemaps.model.DevicePreferences
import com.esp32.googlemaps.model.Esp32Message
import com.esp32.googlemaps.model.NavigationPayload
import com.esp32.googlemaps.model.NavigationState
import com.esp32.googlemaps.util.PermissionHelper
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import timber.log.Timber
import kotlin.math.roundToInt

class NavigationBroadcastService : Service(), LocationListener, BluetoothController.Listener {

    inner class LocalBinder : Binder() {
        fun getService(): NavigationBroadcastService = this@NavigationBroadcastService
    }

    private val binder = LocalBinder()
    private val scope = CoroutineScope(Dispatchers.IO + Job())
    private var pingJob: Job? = null
    private var reconnectJob: Job? = null
    @Volatile private var reconnectAddress: String? = null
    @Volatile private var reconnectForceBle: Boolean = false
    @Volatile private var selectedIconOverrideBase64: String? = null

    lateinit var bluetoothController: BluetoothController
        private set

    private var currentNavState: NavigationState = NavigationState()
    private var currentGpsSpeed: Int = 0

    companion object {
        const val NOTIFICATION_ID = 2001
        const val CHANNEL_ID = "esp32_navigation_channel"

        const val ACTION_START = "ACTION_START"
        const val ACTION_STOP = "ACTION_STOP"
        const val ACTION_CONNECT = "ACTION_CONNECT"
        const val ACTION_DISCONNECT = "ACTION_DISCONNECT"
        const val ACTION_SEND_TEST = "ACTION_SEND_TEST"

        const val EXTRA_ADDRESS = "extra_address"
        const val EXTRA_FORCE_BLE = "extra_force_ble"

        const val BROADCAST_BT_STATUS = "com.esp32.googlemaps.BT_STATUS"
        const val BROADCAST_SPEED_UPDATE = "com.esp32.googlemaps.SPEED_UPDATE"
        const val EXTRA_STATUS = "status"
        const val EXTRA_DEVICE_NAME = "device_name"
        const val EXTRA_SPEED = "speed"

        private var instance: NavigationBroadcastService? = null

        fun updateNavigation(context: Context, state: NavigationState) {
            instance?.handleNavigationUpdate(state)
        }

        fun sendTestPayload(context: Context, state: NavigationState) {
            instance?.handleNavigationUpdate(state)
        }

        fun sendIconOnly(context: Context, iconBase64: String, iconLabel: String) {
            val service = instance ?: return
            service.sendSelectedIcon(iconBase64, iconLabel)
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        bluetoothController = BluetoothController(applicationContext)
        bluetoothController.listener = this
        createNotificationChannel()
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                startForegroundServiceCompat()
                startLocationUpdates()
                startPeriodicPing()
                if (reconnectAddress == null) {
                    val prefs = getSharedPreferences("esp32_maps_prefs", Context.MODE_PRIVATE)
                    val savedAddress = prefs.getString("last_address", null)
                    if (!savedAddress.isNullOrEmpty()) {
                        reconnectAddress = savedAddress
                        reconnectForceBle = prefs.getBoolean("last_is_ble", false)
                        startAutoReconnect()
                    }
                }
            }
            ACTION_STOP -> {
                ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
                stopLocationUpdates()
                stopPeriodicPing()
                stopAutoReconnect()
                bluetoothController.disconnect()
                stopSelf()
            }
            ACTION_CONNECT -> {
                val address = intent.getStringExtra(EXTRA_ADDRESS)
                val forceBle = intent.getBooleanExtra(EXTRA_FORCE_BLE, false)
                if (!address.isNullOrEmpty()) {
                    reconnectAddress = address
                    reconnectForceBle = forceBle
                    bluetoothController.connect(address, forceBle)
                    startAutoReconnect()
                }
            }
            ACTION_DISCONNECT -> {
                stopAutoReconnect()
                bluetoothController.disconnect()
            }
        }
        return START_STICKY
    }

    private fun startForegroundServiceCompat() {
        val notification = buildForegroundNotification("Waiting for navigation data...")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val serviceType = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            } else {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            }
            startForeground(NOTIFICATION_ID, notification, serviceType)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun buildForegroundNotification(contentText: String): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.service_foreground_title))
            .setContentText(contentText)
            .setSmallIcon(R.drawable.ic_navigation)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setColor(Color.parseColor("#00E5FF"))
            .build()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "ESP32 Navigation Service",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Shows active status for ESP32 Google Maps OLED streaming"
                enableLights(false)
                enableVibration(false)
            }
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }

    @SuppressLint("MissingPermission")
    private fun startLocationUpdates() {
        if (!PermissionHelper.hasLocationPermissions(this)) return
        try {
            val lm = getSystemService(Context.LOCATION_SERVICE) as LocationManager
            if (lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 1.0f, this)
            } else if (lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 2000L, 2.0f, this)
            }
        } catch (e: Exception) {
            Timber.e("Location subscription failed: ${e.message}")
        }
    }

    private fun stopLocationUpdates() {
        try {
            val lm = getSystemService(Context.LOCATION_SERVICE) as LocationManager
            lm.removeUpdates(this)
        } catch (_: Exception) {}
    }

    override fun onLocationChanged(location: Location) {
        // Speed in m/s converted to km/h
        val speedKmh = (location.speed * 3.6).roundToInt().coerceAtLeast(0)
        currentGpsSpeed = speedKmh
        currentNavState.speed = speedKmh

        LocalBroadcastManager.getInstance(applicationContext).sendBroadcast(
            Intent(BROADCAST_SPEED_UPDATE).apply {
                putExtra(EXTRA_SPEED, speedKmh)
            }
        )

        // Send speed update if connected and not currently navigating (speedometer mode)
        if (bluetoothController.isConnected() && !currentNavState.isNavigating) {
            sendJsonPayload()
        }
    }

    private fun sendSelectedIcon(iconBase64: String, iconLabel: String) {
        if (!bluetoothController.isConnected()) {
            Timber.w("Cannot send selected icon: Bluetooth is disconnected")
            return
        }
        val message = Esp32Message(
            navigation = NavigationPayload(
                nextRoad = iconLabel,
                iconBase64 = iconBase64
            ),
            speed = currentGpsSpeed,
            preferences = DevicePreferences(displayBacklight = false, speedLimit = 60),
            epochSeconds = System.currentTimeMillis() / 1000L
        )
        bluetoothController.send(Json.encodeToString(message))
    }

    fun handleNavigationUpdate(state: NavigationState) {
        currentNavState = state
        currentNavState.speed = currentGpsSpeed

        val statusText = if (state.isNavigating) {
            "${state.distanceToTurn} · ${state.nextRoad}"
        } else {
            "Ready · Connected to ${bluetoothController.currentDeviceName ?: "ESP32"}"
        }

        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIFICATION_ID, buildForegroundNotification(statusText))

        sendJsonPayload()
    }

    private fun sendJsonPayload() {
        if (!bluetoothController.isConnected()) return

        val navPayload = if (currentNavState.isNavigating) {
            NavigationPayload(
                nextRoad = currentNavState.nextRoad.ifEmpty { null },
                nextRoadSub = currentNavState.nextRoadSub.ifEmpty { null },
                nextRoadDistance = currentNavState.distanceToTurn.ifEmpty { null },
                eta = currentNavState.eta.ifEmpty { null },
                ete = currentNavState.ete.ifEmpty { null },
                distance = currentNavState.totalDistance.ifEmpty { null },
                iconBase64 = currentNavState.iconBase64.ifEmpty { null }
            )
        } else null

        val message = Esp32Message(
            navigation = navPayload,
            speed = currentGpsSpeed,
            preferences = DevicePreferences(displayBacklight = false, speedLimit = 60),
            epochSeconds = System.currentTimeMillis() / 1000L
        )

        val jsonString = Json.encodeToString(message)
        com.esp32.googlemaps.util.DebugLogger.log(
            "DISPATCH",
            "Streaming to ESP32: nav=${currentNavState.isNavigating} road='${currentNavState.nextRoad}' dist='${currentNavState.distanceToTurn}' speed=$currentGpsSpeed km/h",
            com.esp32.googlemaps.util.DebugLogger.Level.BT
        )
        bluetoothController.send(jsonString)
    }

    private fun startPeriodicPing() {
        pingJob?.cancel()
        pingJob = scope.launch {
            while (isActive) {
                delay(8000)
                if (bluetoothController.isConnected()) {
                    // Send heartbeat so ESP32 screen stays refreshed
                    sendJsonPayload()
                }
            }
        }
    }

    private fun stopPeriodicPing() {
        pingJob?.cancel()
        pingJob = null
    }

    private fun startAutoReconnect() {
        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            var retryDelayMs = 2_000L
            while (isActive) {
                val address = reconnectAddress ?: break
                if (!PermissionHelper.hasBluetoothPermissions(this@NavigationBroadcastService)) {
                    delay(10_000)
                    continue
                }
                delay(retryDelayMs)
                when (bluetoothController.state) {
                    BluetoothController.ConnectionState.DISCONNECTED -> {
                        Timber.i("Bluetooth disconnected; retrying saved device connection")
                        bluetoothController.connect(address, reconnectForceBle)
                        retryDelayMs = (retryDelayMs * 2).coerceAtMost(30_000L)
                    }
                    BluetoothController.ConnectionState.CONNECTED -> retryDelayMs = 2_000L
                    BluetoothController.ConnectionState.CONNECTING -> Unit
                }
            }
        }
    }

    private fun stopAutoReconnect() {
        reconnectAddress = null
        reconnectJob?.cancel()
        reconnectJob = null
    }

    // BluetoothController.Listener callbacks
    override fun onConnectionStateChanged(state: BluetoothController.ConnectionState, deviceName: String?) {
        LocalBroadcastManager.getInstance(applicationContext).sendBroadcast(
            Intent(BROADCAST_BT_STATUS).apply {
                putExtra(EXTRA_STATUS, state.name)
                putExtra(EXTRA_DEVICE_NAME, deviceName)
            }
        )

        if (state == BluetoothController.ConnectionState.CONNECTED) {
            sendJsonPayload()
        }
    }

    override fun onDataReceived(data: String) {
        Timber.d("ESP32 Echo: $data")
    }

    override fun onError(message: String) {
        Timber.e("Bluetooth Error: $message")
    }

    override fun onDestroy() {
        super.onDestroy()
        instance = null
        stopLocationUpdates()
        stopPeriodicPing()
        stopAutoReconnect()
        bluetoothController.disconnect()
    }
}

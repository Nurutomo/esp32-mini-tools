package com.esp32.googlemaps

import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.graphics.Color
import android.os.Bundle
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.esp32.googlemaps.bluetooth.BluetoothController
import com.esp32.googlemaps.databinding.ActivityMainBinding
import com.esp32.googlemaps.model.NavigationState
import com.esp32.googlemaps.service.GoogleMapsNotificationListener
import com.esp32.googlemaps.service.NavigationBroadcastService
import com.esp32.googlemaps.util.BitmapHelper
import com.esp32.googlemaps.util.PermissionHelper
import timber.log.Timber

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var prefs: SharedPreferences

    private var selectedDeviceAddress: String? = null
    private var selectedDeviceName: String? = null
    private var selectedDeviceIsBle: Boolean = false
    private var currentConnectionState = BluetoothController.ConnectionState.DISCONNECTED

    private val navReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            when (intent?.action) {
                GoogleMapsNotificationListener.ACTION_NAV_UPDATE -> {
                    val state = intent.getParcelableExtra<NavigationState>(GoogleMapsNotificationListener.EXTRA_NAV_STATE)
                    if (state != null) {
                        updateHudPreview(state)
                    }
                }
                NavigationBroadcastService.BROADCAST_BT_STATUS -> {
                    val status = intent.getStringExtra(NavigationBroadcastService.EXTRA_STATUS) ?: return
                    val devName = intent.getStringExtra(NavigationBroadcastService.EXTRA_DEVICE_NAME)
                    val connState = try {
                        BluetoothController.ConnectionState.valueOf(status)
                    } catch (_: Exception) {
                        BluetoothController.ConnectionState.DISCONNECTED
                    }
                    updateConnectionStateUi(connState, devName)
                }
                NavigationBroadcastService.BROADCAST_SPEED_UPDATE -> {
                    val speed = intent.getIntExtra(NavigationBroadcastService.EXTRA_SPEED, 0)
                    binding.previewSpeed.text = speed.toString()
                }
            }
        }
    }

    private val selectDeviceLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK && result.data != null) {
            val address = result.data?.getStringExtra("address") ?: return@registerForActivityResult
            val name = result.data?.getStringExtra("name") ?: "ESP32-OLED"
            val isBle = result.data?.getBooleanExtra("is_ble", false) ?: false

            saveSelectedDevice(address, name, isBle)
            updateDeviceUi()

            // Trigger connect automatically
            connectToSelectedDevice()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Timber.treeCount == 0) {
            Timber.plant(Timber.DebugTree())
        }

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        prefs = getSharedPreferences("esp32_maps_prefs", Context.MODE_PRIVATE)
        loadSavedDevice()

        setupListeners()
        updateDeviceUi()
        updateConnectionStateUi(BluetoothController.ConnectionState.DISCONNECTED, selectedDeviceName)

        // Start foreground service if not already active
        startNavigationService()
    }

    override fun onResume() {
        super.onResume()
        updatePermissionBadges()

        val filter = IntentFilter().apply {
            addAction(GoogleMapsNotificationListener.ACTION_NAV_UPDATE)
            addAction(NavigationBroadcastService.BROADCAST_BT_STATUS)
            addAction(NavigationBroadcastService.BROADCAST_SPEED_UPDATE)
        }
        LocalBroadcastManager.getInstance(this).registerReceiver(navReceiver, filter)
    }

    override fun onPause() {
        super.onPause()
        LocalBroadcastManager.getInstance(this).unregisterReceiver(navReceiver)
    }

    private fun setupListeners() {
        // Connect / Disconnect toggle button
        binding.btnConnect.setOnClickListener {
            if (currentConnectionState == BluetoothController.ConnectionState.CONNECTED) {
                disconnectDevice()
            } else {
                connectToSelectedDevice()
            }
        }

        // Pick Device button
        binding.btnSelectDevice.setOnClickListener {
            val intent = Intent(this, DeviceSelectionActivity::class.java)
            selectDeviceLauncher.launch(intent)
        }

        // Permission buttons
        binding.btnPermNotification.setOnClickListener {
            PermissionHelper.openNotificationListenerSettings(this)
        }
        binding.btnPermBluetooth.setOnClickListener {
            PermissionHelper.requestBluetoothPermissions(this)
        }
        binding.btnPermLocation.setOnClickListener {
            PermissionHelper.requestLocationPermissions(this)
        }

        // Quick Test Buttons (Desk Simulation)
        binding.btnTestLeft.setOnClickListener {
            runSyntheticTest(
                turnType = "left",
                road = "Grand Avenue",
                sub = "Toward Downtown",
                dist = "200 m",
                eta = "17:42",
                ete = "8 min",
                totalDist = "3.4 km"
            )
        }

        binding.btnTestRight.setOnClickListener {
            runSyntheticTest(
                turnType = "right",
                road = "Main Street",
                sub = "In 500 meters",
                dist = "500 m",
                eta = "17:48",
                ete = "14 min",
                totalDist = "6.1 km"
            )
        }

        binding.btnTestStraight.setOnClickListener {
            runSyntheticTest(
                turnType = "straight",
                road = "Interstate 80 East",
                sub = "Keep left at fork",
                dist = "1.8 km",
                eta = "18:05",
                ete = "31 min",
                totalDist = "38 km"
            )
        }

        binding.btnTestRoundabout.setOnClickListener {
            runSyntheticTest(
                turnType = "roundabout",
                road = "Traffic Circle",
                sub = "Take 2nd exit",
                dist = "150 m",
                eta = "17:35",
                ete = "3 min",
                totalDist = "1.1 km"
            )
        }

        binding.btnClearHud.setOnClickListener {
            val emptyState = NavigationState(isNavigating = false)
            updateHudPreview(emptyState)
            NavigationBroadcastService.sendTestPayload(this, emptyState)
            Toast.makeText(this, "HUD cleared", Toast.LENGTH_SHORT).show()
        }
    }

    private fun runSyntheticTest(
        turnType: String,
        road: String,
        sub: String,
        dist: String,
        eta: String,
        ete: String,
        totalDist: String
    ) {
        val bmp = when (turnType) {
            "left" -> BitmapHelper.createTurnLeftBitmap()
            "right" -> BitmapHelper.createTurnRightBitmap()
            "straight" -> BitmapHelper.createStraightBitmap()
            "roundabout" -> BitmapHelper.createRoundaboutBitmap()
            else -> BitmapHelper.createStraightBitmap()
        }
        val (base64, monoBmp) = BitmapHelper.bitmapToBase64(bmp)

        val testState = NavigationState(
            nextRoad = road,
            nextRoadSub = sub,
            distanceToTurn = dist,
            eta = eta,
            ete = ete,
            totalDistance = totalDist,
            iconBase64 = base64,
            speed = (35..55).random(),
            isNavigating = true,
            lastUpdatedMs = System.currentTimeMillis()
        ).apply {
            currentIconBitmap = monoBmp
        }

        updateHudPreview(testState)
        NavigationBroadcastService.sendTestPayload(this, testState)
        Toast.makeText(this, "Sent test $turnType maneuver to OLED", Toast.LENGTH_SHORT).show()
    }

    private fun updateHudPreview(state: NavigationState) {
        if (state.isNavigating) {
            binding.previewDistance.text = state.distanceToTurn.ifEmpty { "---" }
            binding.previewRoad.text = state.nextRoad.ifEmpty { "Navigating..." }
            binding.previewRoadSub.text = state.nextRoadSub.ifEmpty { "" }

            val etaParts = listOf(state.ete, state.totalDistance, state.eta).filter { it.isNotEmpty() }
            binding.previewEta.text = if (etaParts.isNotEmpty()) etaParts.joinToString(" · ") else "---"

            val iconBmp = state.currentIconBitmap ?: BitmapHelper.base64ToBitmap(state.iconBase64)
            if (iconBmp != null) {
                binding.previewManeuverIcon.setImageBitmap(iconBmp)
            } else {
                binding.previewManeuverIcon.setImageResource(R.drawable.ic_navigation)
            }
        } else {
            binding.previewDistance.text = "---"
            binding.previewEta.text = "--- · --- · ---"
            binding.previewRoad.text = "Waiting for Google Maps..."
            binding.previewRoadSub.text = "Start navigation on your phone"
            binding.previewManeuverIcon.setImageResource(R.drawable.ic_navigation)
        }
    }

    private fun startNavigationService() {
        val intent = Intent(this, NavigationBroadcastService::class.java).apply {
            action = NavigationBroadcastService.ACTION_START
        }
        startService(intent)
    }

    private fun connectToSelectedDevice() {
        val address = selectedDeviceAddress
        if (address.isNullOrEmpty()) {
            Toast.makeText(this, "Please select an ESP32 device first", Toast.LENGTH_SHORT).show()
            selectDeviceLauncher.launch(Intent(this, DeviceSelectionActivity::class.java))
            return
        }

        if (!PermissionHelper.hasBluetoothPermissions(this)) {
            PermissionHelper.requestBluetoothPermissions(this)
            return
        }

        val intent = Intent(this, NavigationBroadcastService::class.java).apply {
            action = NavigationBroadcastService.ACTION_CONNECT
            putExtra(NavigationBroadcastService.EXTRA_ADDRESS, address)
            putExtra(NavigationBroadcastService.EXTRA_FORCE_BLE, selectedDeviceIsBle)
        }
        startService(intent)
    }

    private fun disconnectDevice() {
        val intent = Intent(this, NavigationBroadcastService::class.java).apply {
            action = NavigationBroadcastService.ACTION_DISCONNECT
        }
        startService(intent)
    }

    private fun updateConnectionStateUi(state: BluetoothController.ConnectionState, deviceName: String?) {
        currentConnectionState = state
        when (state) {
            BluetoothController.ConnectionState.CONNECTED -> {
                binding.badgeStatus.text = getString(R.string.status_connected)
                binding.badgeStatus.setTextColor(getColor(R.color.status_green))
                binding.btnConnect.text = getString(R.string.disconnect)
                binding.btnConnect.backgroundTintList = getColorStateList(R.color.status_red)
                binding.txtSelectedDevice.text = "Device: ${deviceName ?: selectedDeviceName ?: "ESP32-OLED"}"
            }
            BluetoothController.ConnectionState.CONNECTING -> {
                binding.badgeStatus.text = getString(R.string.status_connecting)
                binding.badgeStatus.setTextColor(getColor(R.color.status_yellow))
                binding.btnConnect.text = "Connecting..."
                binding.btnConnect.backgroundTintList = getColorStateList(R.color.primary)
            }
            BluetoothController.ConnectionState.DISCONNECTED -> {
                binding.badgeStatus.text = getString(R.string.status_disconnected)
                binding.badgeStatus.setTextColor(getColor(R.color.status_red))
                binding.btnConnect.text = getString(R.string.connect)
                binding.btnConnect.backgroundTintList = getColorStateList(R.color.primary)
            }
        }
    }

    private fun updateDeviceUi() {
        val name = selectedDeviceName
        val addr = selectedDeviceAddress
        val isBle = selectedDeviceIsBle

        if (!addr.isNullOrEmpty()) {
            val proto = if (isBle) "BLE" else "Classic BT"
            binding.txtSelectedDevice.text = "Device: ${name ?: "ESP32-OLED"} ($proto)"
            binding.txtSelectedAddress.text = "Address: $addr"
        } else {
            binding.txtSelectedDevice.text = "Device: No device paired"
            binding.txtSelectedAddress.text = "Address: Tap 'Pick Device' to select"
        }
    }

    private fun updatePermissionBadges() {
        // 1. Notification Listener
        if (PermissionHelper.isNotificationServiceEnabled(this)) {
            binding.btnPermNotification.text = getString(R.string.permission_granted)
            binding.btnPermNotification.setTextColor(getColor(R.color.status_green))
            binding.btnPermNotification.isEnabled = false
        } else {
            binding.btnPermNotification.text = getString(R.string.grant_permission)
            binding.btnPermNotification.setTextColor(getColor(R.color.primary))
            binding.btnPermNotification.isEnabled = true
        }

        // 2. Bluetooth
        if (PermissionHelper.hasBluetoothPermissions(this)) {
            binding.btnPermBluetooth.text = getString(R.string.permission_granted)
            binding.btnPermBluetooth.setTextColor(getColor(R.color.status_green))
            binding.btnPermBluetooth.isEnabled = false
        } else {
            binding.btnPermBluetooth.text = getString(R.string.grant_permission)
            binding.btnPermBluetooth.setTextColor(getColor(R.color.primary))
            binding.btnPermBluetooth.isEnabled = true
        }

        // 3. Location
        if (PermissionHelper.hasLocationPermissions(this)) {
            binding.btnPermLocation.text = getString(R.string.permission_granted)
            binding.btnPermLocation.setTextColor(getColor(R.color.status_green))
            binding.btnPermLocation.isEnabled = false
        } else {
            binding.btnPermLocation.text = getString(R.string.grant_permission)
            binding.btnPermLocation.setTextColor(getColor(R.color.primary))
            binding.btnPermLocation.isEnabled = true
        }
    }

    private fun loadSavedDevice() {
        selectedDeviceAddress = prefs.getString("last_address", null)
        selectedDeviceName = prefs.getString("last_name", null)
        selectedDeviceIsBle = prefs.getBoolean("last_is_ble", false)
    }

    private fun saveSelectedDevice(address: String, name: String, isBle: Boolean) {
        selectedDeviceAddress = address
        selectedDeviceName = name
        selectedDeviceIsBle = isBle
        prefs.edit()
            .putString("last_address", address)
            .putString("last_name", name)
            .putBoolean("last_is_ble", isBle)
            .apply()
    }
}

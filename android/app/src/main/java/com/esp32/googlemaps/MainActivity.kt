package com.esp32.googlemaps

import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.graphics.Color
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.esp32.googlemaps.bluetooth.BluetoothController
import com.esp32.googlemaps.databinding.ActivityMainBinding
import com.esp32.googlemaps.model.NavigationState
import com.esp32.googlemaps.service.GoogleMapsNotificationListener
import com.esp32.googlemaps.service.NavigationBroadcastService
import com.esp32.googlemaps.util.BitmapHelper
import com.esp32.googlemaps.util.DebugLogger
import com.esp32.googlemaps.util.PermissionHelper
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import timber.log.Timber
import java.io.File

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
                    @Suppress("DEPRECATION")
                    val state = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(GoogleMapsNotificationListener.EXTRA_NAV_STATE, NavigationState::class.java)
                    } else {
                        intent.getParcelableExtra(GoogleMapsNotificationListener.EXTRA_NAV_STATE)
                    }
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

    private val debugLogListener = object : DebugLogger.LogListener {
        private val pendingLogs = mutableListOf<DebugLogger.LogEntry>()
        private var isFlushScheduled = false

        override fun onLogAdded(entry: DebugLogger.LogEntry) {
            synchronized(pendingLogs) {
                pendingLogs.add(entry)
            }
            if (!isFlushScheduled) {
                isFlushScheduled = true
                binding.txtDebugLogs.postDelayed({
                    val entriesToFlush = synchronized(pendingLogs) {
                        val list = ArrayList(pendingLogs)
                        pendingLogs.clear()
                        isFlushScheduled = false
                        list
                    }
                    if (entriesToFlush.isEmpty()) return@postDelayed

                    val currentText = binding.txtDebugLogs.text
                    if (currentText.isEmpty() || currentText == "Waiting for events..." || currentText.startsWith("Logs cleared")) {
                        binding.txtDebugLogs.text = entriesToFlush.joinToString("\n") { it.toString() }
                    } else {
                        val sb = StringBuilder()
                        for (e in entriesToFlush) {
                            sb.append("\n").append(e.toString())
                        }
                        binding.txtDebugLogs.append(sb.toString())

                        // Prune excess lines to prevent memory explosion & layout rendering lag (keep max ~15000 chars)
                        if (binding.txtDebugLogs.length() > 15000) {
                            val trimmed = binding.txtDebugLogs.text.takeLast(10000).substringAfter("\n")
                            binding.txtDebugLogs.text = trimmed
                        }
                    }

                    if (binding.cbAutoScrollLogs.isChecked) {
                        binding.scrollDebugLogs.post {
                            binding.scrollDebugLogs.scrollTo(0, binding.txtDebugLogs.bottom)
                        }
                    }
                }, 100)
            }
        }

        override fun onLogsCleared() {
            synchronized(pendingLogs) {
                pendingLogs.clear()
            }
            binding.txtDebugLogs.text = "Logs cleared. Waiting for events..."
        }

        override fun onStatusChanged() {
            updateListenerStatusBadge()
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

    private val enableBluetoothForConnectLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            Toast.makeText(this, "Bluetooth turned on. Connecting...", Toast.LENGTH_SHORT).show()
            connectToSelectedDevice()
        } else {
            Toast.makeText(this, "Bluetooth is required to connect to ESP32", Toast.LENGTH_LONG).show()
        }
    }

    private val enableBluetoothForScanLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val intent = Intent(this, DeviceSelectionActivity::class.java)
            selectDeviceLauncher.launch(intent)
        } else {
            Toast.makeText(this, "Bluetooth is required to scan for ESP32", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Timber.treeCount == 0) {
            Timber.plant(Timber.DebugTree())
            Timber.plant(DebugLogger.InAppTree())
        }

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        prefs = getSharedPreferences("esp32_maps_prefs", Context.MODE_PRIVATE)
        loadSavedDevice()

        setupListeners()
        updateDeviceUi()
        updateConnectionStateUi(BluetoothController.ConnectionState.DISCONNECTED, selectedDeviceName)
        updateListenerStatusBadge()
        initDebugLogView()

        DebugLogger.log("APP", "ESP32 Google Maps HUD launched", DebugLogger.Level.INFO)

        // Attempt rebind if permission already granted
        if (PermissionHelper.isNotificationServiceEnabled(this) && !GoogleMapsNotificationListener.isConnected()) {
            PermissionHelper.rebindNotificationListener(this)
        }

        // Start foreground service if not already active
        startNavigationService()

        // Handle Termux / CLI intent actions if launched via `am start`
        handleIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        when (intent?.getStringExtra("action")) {
            "dump_json" -> dumpNotificationsAndShowDialog()
            "rebind" -> {
                PermissionHelper.rebindNotificationListener(this)
                Toast.makeText(this, "Rebind requested from CLI", Toast.LENGTH_SHORT).show()
            }
            "scan" -> {
                lifecycleScope.launch {
                    withContext(Dispatchers.IO) {
                        GoogleMapsNotificationListener.scanActiveNotifications()
                    }
                    Toast.makeText(this@MainActivity, "Scan tray requested from CLI", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        updatePermissionBadges()
        updateListenerStatusBadge()
        DebugLogger.addListener(debugLogListener)

        val filter = IntentFilter().apply {
            addAction(GoogleMapsNotificationListener.ACTION_NAV_UPDATE)
            addAction(NavigationBroadcastService.BROADCAST_BT_STATUS)
            addAction(NavigationBroadcastService.BROADCAST_SPEED_UPDATE)
        }
        LocalBroadcastManager.getInstance(this).registerReceiver(navReceiver, filter)
    }

    override fun onPause() {
        super.onPause()
        DebugLogger.removeListener(debugLogListener)
        LocalBroadcastManager.getInstance(this).unregisterReceiver(navReceiver)
    }

    private fun initDebugLogView() {
        val existingLogs = DebugLogger.getLogs()
        if (existingLogs.isNotEmpty()) {
            binding.txtDebugLogs.text = existingLogs.joinToString("\n") { it.toString() }
            binding.scrollDebugLogs.post {
                binding.scrollDebugLogs.fullScroll(View.FOCUS_DOWN)
            }
        }
    }

    private fun updateListenerStatusBadge() {
        val connected = GoogleMapsNotificationListener.isConnected()
        val permGranted = PermissionHelper.isNotificationServiceEnabled(this)

        if (connected) {
            binding.badgeListenerStatus.text = "Listener: Connected"
            binding.badgeListenerStatus.setTextColor(ContextCompat.getColor(this, R.color.status_green))
        } else if (permGranted) {
            binding.badgeListenerStatus.text = "Listener: Permitted (Tap Rebind)"
            binding.badgeListenerStatus.setTextColor(ContextCompat.getColor(this, R.color.status_yellow))
        } else {
            binding.badgeListenerStatus.text = "Listener: Perm Missing"
            binding.badgeListenerStatus.setTextColor(ContextCompat.getColor(this, R.color.status_red))
        }
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
            val bm = getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            val adapter = bm?.adapter
            if (adapter != null && !adapter.isEnabled) {
                enableBluetoothForScanLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            } else {
                val intent = Intent(this, DeviceSelectionActivity::class.java)
                selectDeviceLauncher.launch(intent)
            }
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

        // Debug Console Controls
        binding.btnRebindListener.setOnClickListener {
            PermissionHelper.rebindNotificationListener(this)
            Toast.makeText(this, "Rebind requested & component toggled", Toast.LENGTH_SHORT).show()
            updateListenerStatusBadge()
        }

        binding.btnScanTray.setOnClickListener {
            binding.btnScanTray.isEnabled = false
            lifecycleScope.launch {
                withContext(Dispatchers.IO) {
                    GoogleMapsNotificationListener.scanActiveNotifications()
                }
                binding.btnScanTray.isEnabled = true
                Toast.makeText(this@MainActivity, "Scanned active notification tray", Toast.LENGTH_SHORT).show()
            }
        }

        binding.btnDumpJson.setOnClickListener {
            binding.btnDumpJson.isEnabled = false
            binding.btnDumpJson.text = "Dumping..."
            lifecycleScope.launch {
                val json = withContext(Dispatchers.IO) {
                    GoogleMapsNotificationListener.dumpActiveNotificationsJson(this@MainActivity)
                }
                binding.btnDumpJson.isEnabled = true
                binding.btnDumpJson.text = getString(R.string.btn_dump_json)
                showDumpJsonDialog(json)
            }
        }

        // Prevent outer ScrollView from intercepting touches when scrolling the log viewer
        binding.scrollDebugLogs.setOnTouchListener { v, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                    v.parent?.requestDisallowInterceptTouchEvent(true)
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    v.parent?.requestDisallowInterceptTouchEvent(false)
                }
            }
            false
        }

        binding.cbLogAllNotifs.setOnCheckedChangeListener { _, isChecked ->
            DebugLogger.logAllNotifications = isChecked
            val stateText = if (isChecked) "ENABLED" else "DISABLED"
            DebugLogger.log("CONSOLE", "Log all notifications: $stateText", DebugLogger.Level.INFO)
            Toast.makeText(this, "Log all notifications: $stateText", Toast.LENGTH_SHORT).show()
        }

        binding.btnClearLogs.setOnClickListener {
            DebugLogger.clear()
        }

        binding.btnCopyLogs.setOnClickListener {
            val logs = DebugLogger.getAllFormattedLogs()
            if (logs.isNotEmpty()) {
                val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                val clip = ClipData.newPlainText("ESP32_HUD_LOGS", logs)
                clipboard.setPrimaryClip(clip)
                Toast.makeText(this, "Copied ${logs.lines().size} lines to clipboard!", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "No logs to copy", Toast.LENGTH_SHORT).show()
            }
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

    private fun dumpNotificationsAndShowDialog() {
        lifecycleScope.launch {
            val json = withContext(Dispatchers.IO) {
                GoogleMapsNotificationListener.dumpActiveNotificationsJson(this@MainActivity)
            }
            showDumpJsonDialog(json)
        }
    }

    private fun showDumpJsonDialog(json: String) {

        // 1. Copy to clipboard immediately
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(ClipData.newPlainText("Notifications JSON", json))

        // 2. Save to Downloads directory
        var savedPath = ""
        try {
            val downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            if (!downloadDir.exists()) downloadDir.mkdirs()
            val targetFile = File(downloadDir, "notifications_dump.json")
            targetFile.writeText(json)
            savedPath = targetFile.absolutePath
        } catch (_: Exception) {
            val fallbackFile = File(getExternalFilesDir(null), "notifications_dump.json")
            fallbackFile.writeText(json)
            savedPath = fallbackFile.absolutePath
        }

        DebugLogger.log("DUMP", "Exported notifications JSON to $savedPath", DebugLogger.Level.NAV)
        Toast.makeText(this, "Copied JSON to clipboard & saved to $savedPath", Toast.LENGTH_LONG).show()

        // 3. Show dialog for viewing and inspection
        val msgView = TextView(this).apply {
            text = json
            setPadding(32, 24, 32, 24)
            setTextIsSelectable(true)
            typeface = Typeface.MONOSPACE
            textSize = 10f
            setTextColor(Color.parseColor("#80FFD4"))
            setBackgroundColor(Color.parseColor("#0A0A0E"))
        }

        val scrollContainer = ScrollView(this).apply {
            addView(msgView)
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 800)
        }

        MaterialAlertDialogBuilder(this)
            .setTitle("Active Notifications Dump (JSON)")
            .setView(scrollContainer)
            .setPositiveButton("Copy Again") { _, _ ->
                clipboard.setPrimaryClip(ClipData.newPlainText("Notifications JSON", json))
                Toast.makeText(this, "JSON copied to clipboard!", Toast.LENGTH_SHORT).show()
            }
            .setNeutralButton("Share") { _, _ ->
                val sendIntent = Intent().apply {
                    action = Intent.ACTION_SEND
                    putExtra(Intent.EXTRA_TEXT, json)
                    type = "text/plain"
                }
                startActivity(Intent.createChooser(sendIntent, "Share Notifications JSON"))
            }
            .setNegativeButton("Close", null)
            .show()
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
            val bm = getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            val adapter = bm?.adapter
            if (adapter != null && !adapter.isEnabled) {
                enableBluetoothForScanLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            } else {
                selectDeviceLauncher.launch(Intent(this, DeviceSelectionActivity::class.java))
            }
            return
        }

        if (!PermissionHelper.hasBluetoothPermissions(this)) {
            PermissionHelper.requestBluetoothPermissions(this)
            return
        }

        val bm = getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        val adapter = bm?.adapter
        if (adapter != null && !adapter.isEnabled) {
            enableBluetoothForConnectLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
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
                binding.badgeStatus.setTextColor(ContextCompat.getColor(this, R.color.status_green))
                binding.btnConnect.text = getString(R.string.disconnect)
                binding.btnConnect.backgroundTintList = ContextCompat.getColorStateList(this, R.color.status_red)
                binding.txtSelectedDevice.text = "Device: ${deviceName ?: selectedDeviceName ?: "ESP32-OLED"}"
            }
            BluetoothController.ConnectionState.CONNECTING -> {
                binding.badgeStatus.text = getString(R.string.status_connecting)
                binding.badgeStatus.setTextColor(ContextCompat.getColor(this, R.color.status_yellow))
                binding.btnConnect.text = "Connecting..."
                binding.btnConnect.backgroundTintList = ContextCompat.getColorStateList(this, R.color.primary)
            }
            BluetoothController.ConnectionState.DISCONNECTED -> {
                binding.badgeStatus.text = getString(R.string.status_disconnected)
                binding.badgeStatus.setTextColor(ContextCompat.getColor(this, R.color.status_red))
                binding.btnConnect.text = getString(R.string.connect)
                binding.btnConnect.backgroundTintList = ContextCompat.getColorStateList(this, R.color.primary)
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
            binding.btnPermNotification.setTextColor(ContextCompat.getColor(this, R.color.status_green))
            binding.btnPermNotification.isEnabled = false
        } else {
            binding.btnPermNotification.text = getString(R.string.grant_permission)
            binding.btnPermNotification.setTextColor(ContextCompat.getColor(this, R.color.primary))
            binding.btnPermNotification.isEnabled = true
        }

        // 2. Bluetooth
        if (PermissionHelper.hasBluetoothPermissions(this)) {
            binding.btnPermBluetooth.text = getString(R.string.permission_granted)
            binding.btnPermBluetooth.setTextColor(ContextCompat.getColor(this, R.color.status_green))
            binding.btnPermBluetooth.isEnabled = false
        } else {
            binding.btnPermBluetooth.text = getString(R.string.grant_permission)
            binding.btnPermBluetooth.setTextColor(ContextCompat.getColor(this, R.color.primary))
            binding.btnPermBluetooth.isEnabled = true
        }

        // 3. Location
        if (PermissionHelper.hasLocationPermissions(this)) {
            binding.btnPermLocation.text = getString(R.string.permission_granted)
            binding.btnPermLocation.setTextColor(ContextCompat.getColor(this, R.color.status_green))
            binding.btnPermLocation.isEnabled = false
        } else {
            binding.btnPermLocation.text = getString(R.string.grant_permission)
            binding.btnPermLocation.setTextColor(ContextCompat.getColor(this, R.color.primary))
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

package com.esp32.googlemaps

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import android.app.Activity
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.esp32.googlemaps.databinding.ActivityDeviceSelectionBinding
import com.esp32.googlemaps.model.BluetoothDeviceInfo
import com.esp32.googlemaps.util.PermissionHelper

@SuppressLint("MissingPermission")
class DeviceSelectionActivity : AppCompatActivity() {

    private lateinit var binding: ActivityDeviceSelectionBinding
    private val deviceList = mutableListOf<BluetoothDeviceInfo>()
    private lateinit var adapter: DeviceAdapter
    private val handler = Handler(Looper.getMainLooper())
    private var isScanning = false

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val bm = getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        bm?.adapter
    }

    private val enableBluetoothLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            Toast.makeText(this, "Bluetooth enabled, scanning for devices...", Toast.LENGTH_SHORT).show()
            startScan()
        } else {
            Toast.makeText(this, "Bluetooth must be turned on to discover devices", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityDeviceSelectionBinding.inflate(layoutInflater)
        setContentView(binding.root)

        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = getString(R.string.title_select_device)

        adapter = DeviceAdapter(deviceList) { selectedDevice ->
            val resultIntent = Intent().apply {
                putExtra("address", selectedDevice.address)
                putExtra("name", selectedDevice.name)
                putExtra("is_ble", selectedDevice.isBle)
            }
            setResult(RESULT_OK, resultIntent)
            finish()
        }

        binding.recyclerDevices.layoutManager = LinearLayoutManager(this)
        binding.recyclerDevices.adapter = adapter

        binding.btnRefreshScan.setOnClickListener {
            startScan()
        }

        startScan()
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }

    private fun startScan() {
        if (!PermissionHelper.hasBluetoothPermissions(this)) {
            PermissionHelper.requestBluetoothPermissions(this)
            return
        }

        val adapter = bluetoothAdapter
        if (adapter == null) {
            Toast.makeText(this, "Bluetooth is not supported on this device", Toast.LENGTH_SHORT).show()
            return
        }

        // Turn on Bluetooth first if disabled
        if (!adapter.isEnabled) {
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            enableBluetoothLauncher.launch(enableBtIntent)
            return
        }

        deviceList.clear()

        // 1. Load paired bonded devices (Classic BT & bonded BLE)
        adapter.bondedDevices?.forEach { device ->
            val name = device.name ?: "Unknown"
            val isBle = (device.type == BluetoothDevice.DEVICE_TYPE_LE)
            addDeviceIfNew(BluetoothDeviceInfo(name, device.address, isBle, true))
        }
        this.adapter.notifyDataSetChanged()

        // 2. Scan for BLE devices
        val scanner = adapter.bluetoothLeScanner
        if (scanner != null && !isScanning) {
            isScanning = true
            binding.progressBarScanning.visibility = View.VISIBLE
            scanner.startScan(leScanCallback)

            handler.postDelayed({
                stopScan()
            }, 8000)
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (PermissionHelper.hasBluetoothPermissions(this)) {
            startScan()
        } else {
            Toast.makeText(this, "Bluetooth permissions required to discover devices", Toast.LENGTH_SHORT).show()
        }
    }

    private fun stopScan() {
        if (isScanning) {
            isScanning = false
            binding.progressBarScanning.visibility = View.GONE
            try {
                bluetoothAdapter?.bluetoothLeScanner?.stopScan(leScanCallback)
            } catch (_: Exception) {}
        }
    }

    private val leScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = result.scanRecord?.deviceName ?: device.name ?: "BLE Device"
            addDeviceIfNew(BluetoothDeviceInfo(name, device.address, isBle = true, isBonded = false))
        }
    }

    private fun addDeviceIfNew(info: BluetoothDeviceInfo) {
        val existingIndex = deviceList.indexOfFirst { it.address == info.address }
        if (existingIndex == -1) {
            // Prioritize ESP32 devices at top
            if (info.name.contains("ESP32", ignoreCase = true) || info.name.contains("OLED", ignoreCase = true) || info.name.contains("CatDrive", ignoreCase = true)) {
                deviceList.add(0, info)
            } else {
                deviceList.add(info)
            }
            adapter.notifyDataSetChanged()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopScan()
    }

    inner class DeviceAdapter(
        private val list: List<BluetoothDeviceInfo>,
        private val onSelect: (BluetoothDeviceInfo) -> Unit
    ) : RecyclerView.Adapter<DeviceAdapter.ViewHolder>() {

        inner class ViewHolder(val view: View) : RecyclerView.ViewHolder(view) {
            val nameView: TextView = view.findViewById(R.id.txtDeviceName)
            val addressView: TextView = view.findViewById(R.id.txtDeviceAddress)
            val badgeView: TextView = view.findViewById(R.id.badgeProtocol)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val v = LayoutInflater.from(parent.context).inflate(R.layout.item_device, parent, false)
            return ViewHolder(v)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val item = list[position]
            holder.nameView.text = item.name
            holder.addressView.text = item.address
            holder.badgeView.text = if (item.isBle) "BLE" else "Classic BT"
            holder.badgeView.setTextColor(if (item.isBle) ContextCompat.getColor(this@DeviceSelectionActivity, R.color.primary) else ContextCompat.getColor(this@DeviceSelectionActivity, R.color.status_yellow))

            holder.view.setOnClickListener {
                onSelect(item)
            }
        }

        override fun getItemCount(): Int = list.size
    }
}

package com.esp32.googlemaps.bluetooth

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothSocket
import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import timber.log.Timber
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID

@SuppressLint("MissingPermission")
class BluetoothController(private val context: Context) {

    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED
    }

    interface Listener {
        fun onConnectionStateChanged(state: ConnectionState, deviceName: String?)
        fun onDataReceived(data: String)
        fun onError(message: String)
    }

    var listener: Listener? = null

    companion object {
        // Standard SPP UUID for Classic Bluetooth
        val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

        // Nordic UART Service (NUS) UUIDs for BLE Serial
        val NUS_SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val NUS_RX_CHAR_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E") // Write to ESP32
        val NUS_TX_CHAR_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E") // Notify from ESP32
    }

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val bm = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        bm?.adapter
    }

    private val scope = CoroutineScope(Dispatchers.IO + Job())
    private val sendMutex = kotlinx.coroutines.sync.Mutex()
    private var connectJob: Job? = null
    private var connectionTimeoutJob: Job? = null
    // Bumped on every teardown so a failed send from an old session cannot
    // disconnect the session that replaced it.
    @Volatile private var connectionId = 0

    // Classic BT variables
    private var classicSocket: BluetoothSocket? = null
    private var classicOutputStream: OutputStream? = null
    private var classicInputStream: InputStream? = null
    private var readJob: Job? = null

    // BLE variables
    private var bleGatt: BluetoothGatt? = null
    private var bleRxChar: BluetoothGattCharacteristic? = null
    private var negotiatedMtu = 23

    var state: ConnectionState = ConnectionState.DISCONNECTED
        private set(value) {
            field = value
            CoroutineScope(Dispatchers.Main).launch {
                listener?.onConnectionStateChanged(value, currentDeviceName)
            }
        }

    var currentDeviceName: String? = null
        private set

    var currentDeviceAddress: String? = null
        private set

    var isBleMode: Boolean = false
        private set

    fun isConnected(): Boolean = state == ConnectionState.CONNECTED

    fun connect(address: String, forceBle: Boolean = false) {
        val adapter = bluetoothAdapter ?: run {
            state = ConnectionState.DISCONNECTED
            listener?.onError("Bluetooth not available on this device")
            return
        }

        val device = try {
            adapter.getRemoteDevice(address)
        } catch (e: Exception) {
            state = ConnectionState.DISCONNECTED
            listener?.onError("Invalid Bluetooth address: $address")
            return
        }

        disconnect()
        state = ConnectionState.CONNECTING
        currentDeviceName = device.name ?: "ESP32-OLED"
        currentDeviceAddress = address

        val deviceType = device.type
        val useBle = forceBle || (deviceType == BluetoothDevice.DEVICE_TYPE_LE)
        isBleMode = useBle

        val connectMsg = "Connecting to ${device.name ?: "ESP32"} ($address) [mode=${if (useBle) "BLE" else "Classic BT"}]"
        Timber.d(connectMsg)
        com.esp32.googlemaps.util.DebugLogger.log("BT", connectMsg, com.esp32.googlemaps.util.DebugLogger.Level.BT)

        connectJob = scope.launch {
            if (useBle) {
                connectBle(device)
            } else {
                connectClassic(device)
            }
        }

        // Watchdog: a socket that never answers leaves us stuck in CONNECTING,
        // and the service reconnect loop would then never retry.
        connectionTimeoutJob?.cancel()
        connectionTimeoutJob = scope.launch {
            delay(15_000)
            if (state == ConnectionState.CONNECTING) {
                Timber.w("Connect attempt timed out; resetting")
                disconnect()
            }
        }
    }

    private suspend fun connectClassic(device: BluetoothDevice) = withContext(Dispatchers.IO) {
        try {
            bluetoothAdapter?.cancelDiscovery()
            val socket = device.createRfcommSocketToServiceRecord(SPP_UUID)
            classicSocket = socket
            socket.connect()

            classicOutputStream = socket.outputStream
            classicInputStream = socket.inputStream
            state = ConnectionState.CONNECTED
            Timber.i("Connected to Classic BT: ${device.name}")
            com.esp32.googlemaps.util.DebugLogger.log("BT", "Connected to Classic BT: ${device.name}", com.esp32.googlemaps.util.DebugLogger.Level.BT)

            startClassicReadLoop()
        } catch (e: Exception) {
            val failMsg = "Classic BT connect failed: ${e.message}. Attempting BLE fallback..."
            Timber.w(failMsg)
            com.esp32.googlemaps.util.DebugLogger.log("BT", failMsg, com.esp32.googlemaps.util.DebugLogger.Level.WARN)
            // Fallback to BLE if classic fails (useful for dual-mode chips or C3/S3)
            connectBle(device)
        }
    }

    private fun startClassicReadLoop() {
        readJob = scope.launch {
            val buffer = ByteArray(1024)
            val socket = classicSocket ?: return@launch
            val stream = classicInputStream ?: return@launch
            while (isConnected() && classicSocket === socket) {
                try {
                    val bytes = stream.read(buffer)
                    if (bytes < 0) {
                        // EOF: peer closed. Only tear down if this is still the live session.
                        if (classicSocket === socket) disconnect()
                        break
                    }
                    if (bytes > 0) {
                        val text = String(buffer, 0, bytes)
                        withContext(Dispatchers.Main) {
                            listener?.onDataReceived(text)
                        }
                    }
                } catch (e: Exception) {
                    if (classicSocket === socket && state == ConnectionState.CONNECTED) {
                        Timber.w("Classic read loop broken: ${e.message}")
                        disconnect()
                    }
                    break
                }
            }
        }
    }

    private fun connectBle(device: BluetoothDevice) {
        isBleMode = true
        try {
            bleGatt = device.connectGatt(context, false, bleCallbacks)
        } catch (e: Exception) {
            Timber.e("connectGatt failed: ${e.message}")
            disconnect()
        }
    }

    private val bleCallbacks = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            Timber.d("BLE onConnectionStateChange status=$status newState=$newState")
            if (gatt !== bleGatt) return // Ignore callbacks from a closed/replaced connection.
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                // Larger MTU keeps a full navigation frame in one characteristic write.
                gatt.requestMtu(517)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Timber.w("BLE disconnected (status $status)")
                disconnect()
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (gatt !== bleGatt) return
            Timber.d("BLE onMtuChanged mtu=$mtu status=$status")
            negotiatedMtu = if (status == BluetoothGatt.GATT_SUCCESS) mtu else 23
            gatt.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (gatt !== bleGatt) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val nusService = gatt.getService(NUS_SERVICE_UUID)
                if (nusService != null) {
                    bleRxChar = nusService.getCharacteristic(NUS_RX_CHAR_UUID)
                    state = ConnectionState.CONNECTED
                    Timber.i("NUS Service discovered, BLE connected!")
                    com.esp32.googlemaps.util.DebugLogger.log("BT", "BLE connected! MTU=$negotiatedMtu", com.esp32.googlemaps.util.DebugLogger.Level.BT)
                } else {
                    Timber.e("Nordic UART Service not found on device!")
                    com.esp32.googlemaps.util.DebugLogger.log("BT", "Nordic UART Service not found on device!", com.esp32.googlemaps.util.DebugLogger.Level.ERROR)
                    disconnect()
                    CoroutineScope(Dispatchers.Main).launch {
                        listener?.onError("Nordic UART Service not found on ESP32 BLE")
                    }
                }
            } else {
                Timber.e("Service discovery failed with status $status")
                disconnect()
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            val data = characteristic.value
            if (data != null && data.isNotEmpty()) {
                val text = String(data)
                CoroutineScope(Dispatchers.Main).launch {
                    listener?.onDataReceived(text)
                }
            }
        }
    }

    fun send(message: String) = sendBytes((message.trimEnd() + "\r\n").toByteArray(Charsets.UTF_8))

    fun sendBytes(data: ByteArray) {
        if (!isConnected()) {
            Timber.w("Cannot send data: Not connected")
            return
        }

        val session = connectionId
        scope.launch {
            try {
                if (isBleMode && bleGatt != null && bleRxChar != null) {
                    sendBleChunks(data)
                    com.esp32.googlemaps.util.DebugLogger.log("BT_TX", "Sent ${data.size} bytes (BLE chunks)", com.esp32.googlemaps.util.DebugLogger.Level.BT)
                } else if (classicOutputStream != null) {
                    classicOutputStream?.write(data)
                    classicOutputStream?.flush()
                    com.esp32.googlemaps.util.DebugLogger.log("BT_TX", "Sent ${data.size} bytes (Classic SPP)", com.esp32.googlemaps.util.DebugLogger.Level.BT)
                }
            } catch (e: Exception) {
                Timber.e("Failed to send data: ${e.message}")
                com.esp32.googlemaps.util.DebugLogger.log("BT_ERR", "Send error: ${e.message}", com.esp32.googlemaps.util.DebugLogger.Level.ERROR)
                if (connectionId == session) disconnect()
            }
        }
    }

    private suspend fun sendBleChunks(data: ByteArray) = sendMutex.withLock {
        val gatt = bleGatt ?: return@withLock
        val rx = bleRxChar ?: return@withLock

        // Max payload per chunk = MTU - 3 bytes GATT overhead
        val chunkSize = (negotiatedMtu - 3).coerceAtLeast(20)
        var offset = 0

        while (offset < data.size) {
            // Abort if the connection was replaced or torn down mid-frame; sending
            // the rest would land on a dead GATT and leave a truncated frame.
            if (bleGatt !== gatt || state != ConnectionState.CONNECTED) {
                throw IOException("BLE disconnected mid-transmission")
            }
            val length = (data.size - offset).coerceAtMost(chunkSize)
            val chunk = ByteArray(length)
            System.arraycopy(data, offset, chunk, 0, length)

            rx.value = chunk
            rx.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            if (!gatt.writeCharacteristic(rx)) {
                throw IOException("BLE write rejected at offset $offset")
            }

            offset += length
            if (offset < data.size) {
                delay(20) // Give BLE link-layer time to transmit chunk
            }
        }
    }

    fun disconnect() {
        connectionId++
        connectJob?.cancel()
        connectionTimeoutJob?.cancel()
        readJob?.cancel()

        try {
            classicInputStream?.close()
            classicOutputStream?.close()
            classicSocket?.close()
        } catch (_: Exception) {}

        try {
            bleGatt?.disconnect()
            bleGatt?.close()
        } catch (_: Exception) {}

        classicSocket = null
        classicOutputStream = null
        classicInputStream = null
        bleGatt = null
        bleRxChar = null

        state = ConnectionState.DISCONNECTED
    }
}

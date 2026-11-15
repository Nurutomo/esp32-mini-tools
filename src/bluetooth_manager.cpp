#include "bluetooth_manager.h"
#include "navigation_state.h"
#include <string.h>
#include <string>

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  #include <BluetoothSerial.h>
#else
  #include <NimBLEDevice.h>
#endif

namespace {

// Nordic UART Service UUIDs
constexpr char kNordicServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNordicRxUuid[]      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // RX: Phone writes to ESP32
constexpr char kNordicTxUuid[]      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // TX: ESP32 notifies Phone

// Legacy custom UUIDs for backwards compatibility
constexpr char kLegacyServiceUuid[] = "8b5d0001-6e8f-4c2a-9b73-6d414c454001";
constexpr char kLegacyCharUuid[]    = "8b5d0002-6e8f-4c2a-9b73-6d414c454001";

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
BluetoothSerial gClassicSerial;
#else
NimBLECharacteristic* gpTxCharacteristic = nullptr;
NimBLEScan* gpScanner = nullptr;
BluetoothDeviceInfo gNearbyDevices[kMaxNearbyBluetoothDevices]{};
uint8_t gNearbyDeviceCount = 0;
#endif

portMUX_TYPE gBtMux = portMUX_INITIALIZER_UNLOCKED;
char gLastMessage[kMaxBtMessageBytes + 1] = "Ready";
volatile bool gConnected = false;
PacketReceivedCallback gpPacketCallback = nullptr;

// Shared receive buffer for both transports. BLE writes land here from the
// NimBLE host task, so every access is guarded by gBtMux.
// A single maximum-sized frame is sufficient; overflow drops the entire write.
constexpr size_t kRxCapacity = kMaxPacketBytes + kNavBinHeaderSize;
uint8_t gRxBuf[kRxCapacity];
size_t gRxLen = 0;
uint32_t gLastRxMs = 0;

// Drop any partially received frame. A disconnect mid-frame must not let the
// next connection's first bytes complete the previous connection's frame.
void resetRxBuffer() {
  portENTER_CRITICAL(&gBtMux);
  gRxLen = 0;
  portEXIT_CRITICAL(&gBtMux);
}

void appendRxBytes(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) return;
  portENTER_CRITICAL(&gBtMux);
  if (gRxLen > 0 && (millis() - gLastRxMs > 500)) gRxLen = 0;
  gLastRxMs = millis();
  if (len > kRxCapacity - gRxLen) {
    // Never retain a truncated frame header/prefix; drop this write and reset.
    gRxLen = 0;
    portEXIT_CRITICAL(&gBtMux);
    return;
  }
  memcpy(gRxBuf + gRxLen, data, len);
  gRxLen += len;
  portEXIT_CRITICAL(&gBtMux);
}

void dispatchPacket(const char* packet, size_t len) {
  if (gpPacketCallback != nullptr) {
    gpPacketCallback(packet, len);
  }
}

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t* /* param */) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    gConnected = true;
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BT Classic] Client connected.");
    }
    gClassicSerial.print("ESP32-OLED Ready!\r\n");
  } else if (event == ESP_SPP_CLOSE_EVT) {
    gConnected = false;
    resetRxBuffer();
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BT Classic] Client disconnected.");
    }
  }
}
#else
class ScanCallbacks final : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (device == nullptr || gNearbyDeviceCount >= kMaxNearbyBluetoothDevices) return;
    BluetoothDeviceInfo& result = gNearbyDevices[gNearbyDeviceCount++];
    const std::string name = device->getName();
    const char* label = name.empty() ? "(unnamed)" : name.c_str();
    strncpy(result.name, label, sizeof(result.name) - 1);
    result.name[sizeof(result.name) - 1] = '\0';
    result.rssi = device->getRSSI();
  }
};

class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    gConnected = true;
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BLE] Client connected.");
    }
  }
  void onDisconnect(NimBLEServer* /* server */, NimBLEConnInfo& /* connInfo */, int reason) override {
    gConnected = false;
    resetRxBuffer();
    if (Serial.availableForWrite() > 0) {
      Serial.printf("[BLE] Client disconnected (0x%02X). Restarting advertising...\n", reason);
    }
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& /* connInfo */) override {
    if (Serial.availableForWrite() > 0) {
      Serial.printf("[BLE] MTU negotiated: %u\n", MTU);
    }
  }
};

class TextCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /* connInfo */) override {
    const std::string& value = characteristic->getValue();
    if (!value.empty()) appendRxBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }
};
#endif

} // namespace

void initBluetooth(const char* deviceName, PacketReceivedCallback onPacket) {
  gpPacketCallback = onPacket;

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  strncpy(gLastMessage, "Ready (Classic BT)", sizeof(gLastMessage) - 1);
  gClassicSerial.register_callback(sppCallback);
  gClassicSerial.enableSSP(false, false);
  bool started = gClassicSerial.begin(deviceName);
  Serial.printf("[BT] Classic Bluetooth %s as '%s'\n", started ? "started" : "FAILED", deviceName);
#else
  strncpy(gLastMessage, "Ready (BLE)", sizeof(gLastMessage) - 1);
  NimBLEDevice::init(deviceName);
  gpScanner = NimBLEDevice::getScan();
  static ScanCallbacks scanCallbacks;
  gpScanner->setScanCallbacks(&scanCallbacks, false);
  gpScanner->setActiveScan(true);
  NimBLEServer* server = NimBLEDevice::createServer();
  static ServerCallbacks serverCallbacks;
  server->setCallbacks(&serverCallbacks);

  // 1. Nordic UART Service
  NimBLEService* nusService = server->createService(kNordicServiceUuid);
  NimBLECharacteristic* rxChar = nusService->createCharacteristic(
      kNordicRxUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  static TextCallbacks textCallbacks;
  rxChar->setCallbacks(&textCallbacks);

  gpTxCharacteristic = nusService->createCharacteristic(
      kNordicTxUuid,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  gpTxCharacteristic->setValue("Ready\r\n");

  // 2. Legacy custom service for backward compatibility
  NimBLEService* legacyService = server->createService(kLegacyServiceUuid);
  NimBLECharacteristic* legacyChar = legacyService->createCharacteristic(
      kLegacyCharUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ);
  legacyChar->setCallbacks(&textCallbacks);
  legacyChar->setValue("Ready");

  server->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setName(deviceName);
  advertising->addServiceUUID(kNordicServiceUuid);
  bool advStarted = advertising->start();
  Serial.printf("[BLE] Advertised as '%s' (%s)\n  NUS Service: %s\n",
                deviceName, advStarted ? "STARTED" : "FAILED", kNordicServiceUuid);
#endif
}

void updateBluetooth() {
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  uint8_t incoming[128];
  size_t count = 0;
  while (gClassicSerial.available() && count < sizeof(incoming)) {
    const int value = gClassicSerial.read();
    if (value >= 0) incoming[count++] = static_cast<uint8_t>(value);
  }
  if (count > 0) appendRxBytes(incoming, count);
#endif

  // Process at most one frame per loop turn so display and input stay serviced.
  {
    uint8_t packet[kMaxPacketBytes + 1];
    size_t packetLen = 0;
    bool binary = false;
    const uint32_t now = millis();

    portENTER_CRITICAL(&gBtMux);
    if (gRxLen >= 2 && gRxBuf[0] == kNavBinMagic0 && gRxBuf[1] == kNavBinMagic1) {
      if (gRxLen >= kNavBinHeaderSize) {
        const size_t payloadLen = static_cast<size_t>(gRxBuf[3]) |
                                  (static_cast<size_t>(gRxBuf[4]) << 8);
        const size_t frameLen = kNavBinHeaderSize + payloadLen;
        if ((gRxBuf[2] != kNavBinTypeNav && gRxBuf[2] != kNavBinTypeHuffman) ||
            payloadLen > kNavBinMaxPayload) {
          memmove(gRxBuf, gRxBuf + 1, --gRxLen);
        } else if (gRxLen >= frameLen) {
          packetLen = frameLen;
          memcpy(packet, gRxBuf, packetLen);
          memmove(gRxBuf, gRxBuf + packetLen, gRxLen - packetLen);
          gRxLen -= packetLen;
          binary = true;
        } else if (now - gLastRxMs >= 500) {
          // Incomplete frame timed out; drop it instead of pinning later writes.
          gRxLen = 0;
        }
      } else if (now - gLastRxMs >= 500) {
        gRxLen = 0;
      }
    } else if (gRxLen > 0) {
      size_t start = 0;
      while (start < gRxLen && (gRxBuf[start] == '\r' || gRxBuf[start] == '\n' || gRxBuf[start] == ' ')) ++start;
      size_t newline = start;
      while (newline < gRxLen && gRxBuf[newline] != '\n') ++newline;
      if (newline < gRxLen) {
        packetLen = newline - start;
        if (packetLen > kMaxPacketBytes) packetLen = 0;
        if (packetLen > 0) memcpy(packet, gRxBuf + start, packetLen);
        const size_t consumed = newline + 1;
        memmove(gRxBuf, gRxBuf + consumed, gRxLen - consumed);
        gRxLen -= consumed;
      } else if (now - gLastRxMs >= 150) {
        packetLen = gRxLen - start;
        if (packetLen > kMaxPacketBytes) packetLen = 0;
        if (packetLen > 0) memcpy(packet, gRxBuf + start, packetLen);
        gRxLen = 0;
      }
    }
    portEXIT_CRITICAL(&gBtMux);

    if (packetLen == 0) return;
    if (binary) {
      dispatchPacket(reinterpret_cast<const char*>(packet), packetLen);
      return;
    }
    while (packetLen > 0 && (packet[packetLen - 1] == '\r' || packet[packetLen - 1] == ' ')) --packetLen;
    packet[packetLen] = '\0';
    if (packetLen == 0) return;
    dispatchPacket(reinterpret_cast<const char*>(packet), packetLen);
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
    gClassicSerial.print("OK\r\n");
#else
    sendBluetoothResponse("OK\r\n");
#endif
  }
}

void startBluetoothScan() {
#if !(defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32))
  if (gpScanner == nullptr || gpScanner->isScanning()) return;
  gNearbyDeviceCount = 0;
  gpScanner->start(5, false, true);
#endif
}

uint8_t getNearbyBluetoothDevices(BluetoothDeviceInfo* devices, uint8_t capacity) {
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  (void)devices;
  (void)capacity;
  return 0;
#else
  if (devices == nullptr) return 0;
  const uint8_t count = gNearbyDeviceCount < capacity ? gNearbyDeviceCount : capacity;
  memcpy(devices, gNearbyDevices, count * sizeof(BluetoothDeviceInfo));
  return count;
#endif
}

bool isBluetoothConnected() {
  return gConnected;
}

const char* getBluetoothStatus() {
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  return gConnected ? "BT Connected" : "BT Ready (SPP)";
#else
  return gConnected ? "BLE Connected" : "BLE Advertising";
#endif
}

const char* getLastTextMessage() {
  return gLastMessage;
}

void setLastTextMessage(const char* msg, size_t len) {
  const size_t count = len < kMaxBtMessageBytes ? len : kMaxBtMessageBytes;
  portENTER_CRITICAL(&gBtMux);
  memcpy(gLastMessage, msg, count);
  gLastMessage[count] = '\0';
  portEXIT_CRITICAL(&gBtMux);

  if (Serial.availableForWrite() > 0) {
    Serial.printf("[BT RX] %s\n", gLastMessage);
  }
}

bool sendBluetoothResponse(const char* text) {
  if (!gConnected || text == nullptr) return false;
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  gClassicSerial.print(text);
  return true;
#else
  if (gpTxCharacteristic != nullptr) {
    gpTxCharacteristic->setValue(std::string(text));
    gpTxCharacteristic->notify();
    return true;
  }
  return false;
#endif
}

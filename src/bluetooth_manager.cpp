#include "bluetooth_manager.h"
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
static std::string gBleRxAccumulator = "";
static uint32_t gLastBleRxMs = 0;
#endif

portMUX_TYPE gBtMux = portMUX_INITIALIZER_UNLOCKED;
char gLastMessage[kMaxBtMessageBytes + 1] = "Ready";
volatile bool gConnected = false;
PacketReceivedCallback gpPacketCallback = nullptr;

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
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BT Classic] Client disconnected.");
    }
  }
}
#else
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

void handleBleRxBytes(const uint8_t* data, size_t len) {
  if (len == 0 || data == nullptr) return;
  const uint32_t now = millis();

  if (!gBleRxAccumulator.empty() && (now - gLastBleRxMs > 500)) {
    gBleRxAccumulator.clear();
  }
  gLastBleRxMs = now;

  gBleRxAccumulator.append(reinterpret_cast<const char*>(data), len);

  size_t nlPos = gBleRxAccumulator.find('\n');
  while (nlPos != std::string::npos) {
    std::string packet = gBleRxAccumulator.substr(0, nlPos);
    gBleRxAccumulator.erase(0, nlPos + 1);

    while (!packet.empty() && (packet.back() == '\r' || packet.back() == ' ')) {
      packet.pop_back();
    }
    while (!packet.empty() && (packet.front() == '\r' || packet.front() == ' ')) {
      packet.erase(0, 1);
    }

    if (!packet.empty()) {
      dispatchPacket(packet.c_str(), packet.size());
      sendBluetoothResponse("OK\r\n");
    }

    nlPos = gBleRxAccumulator.find('\n');
  }

  if (gBleRxAccumulator.size() > kMaxPacketBytes * 2) {
    gBleRxAccumulator.clear();
  }
}

class TextCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /* connInfo */) override {
    std::string val = characteristic->getValue();
    if (!val.empty()) {
      handleBleRxBytes(reinterpret_cast<const uint8_t*>(val.data()), val.size());
    }
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
  const uint32_t now = millis();

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  static char rxBuf[kMaxPacketBytes + 1];
  static size_t rxLen = 0;
  static uint32_t lastByteMs = 0;

  while (gClassicSerial.available()) {
    char c = (char)gClassicSerial.read();
    lastByteMs = now;
    if (c == '\r' || c == '\n') {
      if (rxLen > 0) {
        rxBuf[rxLen] = '\0';
        dispatchPacket(rxBuf, rxLen);
        gClassicSerial.print("OK\r\n");
        rxLen = 0;
      }
    } else if (rxLen < kMaxPacketBytes) {
      rxBuf[rxLen++] = c;
    }
  }

  // Handle line without newline (flush after 150ms idle)
  if (rxLen > 0 && (now - lastByteMs >= 150)) {
    rxBuf[rxLen] = '\0';
    dispatchPacket(rxBuf, rxLen);
    gClassicSerial.print("OK\r\n");
    rxLen = 0;
  }
#else
  // Flush un-terminated BLE packet after 150ms idle
  if (!gBleRxAccumulator.empty() && (now - gLastBleRxMs >= 150)) {
    std::string packet = gBleRxAccumulator;
    gBleRxAccumulator.clear();
    while (!packet.empty() && (packet.back() == '\r' || packet.back() == '\n' || packet.back() == ' ')) {
      packet.pop_back();
    }
    if (!packet.empty()) {
      dispatchPacket(packet.c_str(), packet.size());
      sendBluetoothResponse("OK\r\n");
    }
  }
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

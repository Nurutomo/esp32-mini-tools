#include <Arduino.h>
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  #include <BluetoothSerial.h>
#else
  #include <NimBLEDevice.h>
#endif
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include "config.h"

namespace {

// Bluetooth Constants
constexpr char kDeviceName[] = "ESP32-OLED";
constexpr size_t kMaxBtBytes = 64;
constexpr size_t kMaxBtPacketBytes = 1024;
constexpr size_t kIconWidth = 32;
constexpr size_t kIconHeight = 32;
constexpr size_t kIconBufferSize = (kIconWidth * kIconHeight) / 8; // 128 bytes
constexpr uint32_t kNavTimeoutMs = 15000; // 15 seconds without updates reverts HUD to status screen

// Nordic UART Service (standard BLE Serial profile recognized by mobile apps like "Serial Bluetooth Terminal", nRF Connect, etc.)
constexpr char kNordicServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNordicRxUuid[]      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // RX: Phone writes to ESP32
constexpr char kNordicTxUuid[]      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // TX: ESP32 notifies Phone

// Legacy custom UUIDs for backwards compatibility
constexpr char kLegacyServiceUuid[] = "8b5d0001-6e8f-4c2a-9b73-6d414c454001";
constexpr char kLegacyCharUuid[]    = "8b5d0002-6e8f-4c2a-9b73-6d414c454001";

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
BluetoothSerial bluetoothSerial;
#else
NimBLECharacteristic* pTxCharacteristic = nullptr;
#endif

// LED Logic:
// ESP32-C3 SuperMini onboard blue LED is active-low (LOW = ON, HIGH = OFF).
// Standard ESP32 DevKit onboard LED on GPIO 2 is active-high (HIGH = ON, LOW = OFF).
constexpr uint8_t LED_ON  = LED_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t LED_OFF = LED_ACTIVE_LOW ? HIGH : LOW;

// Blink delay in milliseconds (500ms ON, 500ms OFF = 1 Hz)
constexpr uint32_t kBlinkDelayMs = 500;

// OLED instance
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledReady = false;
uint8_t oledAddress = 0;

// Bluetooth State
portMUX_TYPE btMux = portMUX_INITIALIZER_UNLOCKED;
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
char btMessage[kMaxBtBytes + 1] = "Ready (Classic BT)";
#else
char btMessage[kMaxBtBytes + 1] = "Ready (BLE)";
#endif
volatile bool btConnected = false;
volatile bool btNewMessage = false;

// Navigation state from Google Maps Android app
struct NavigationInfo {
  bool active = false;
  uint32_t lastUpdateMs = 0;
  char nextRoad[64] = "";
  char nextRoadSub[64] = "";
  char distance[32] = "";
  char eta[32] = "";
  char ete[32] = "";
  char totalDistance[32] = "";
  int speed = -1;
  bool hasIcon = false;
  uint8_t iconBitmap[kIconBufferSize];
};

NavigationInfo navInfo;

void turnLedOff() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
}

void turnLedOn() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON);
}

// Ensure the LED is turned off before I2C communication during initial scan.
// Once OLED is active, blinking is disabled and GPIO 8/9 remain in I2C open-drain mode.
void prepareForI2C() {
  if (oledReady) return;
  turnLedOff();

#if defined(CONFIG_IDF_TARGET_ESP32C3)
  // Re-establish I2C open-drain master configuration
  Wire.end();
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
#endif
}

void setBtMessage(const char* msg, size_t len) {
  const size_t count = len < kMaxBtBytes ? len : kMaxBtBytes;
  portENTER_CRITICAL(&btMux);
  memcpy(btMessage, msg, count);
  btMessage[count] = '\0';
  btNewMessage = true;
  portEXIT_CRITICAL(&btMux);
  if (Serial.availableForWrite() > 0) {
    Serial.printf("[BT RX] %s\n", btMessage);
  }
}

// Parse incoming packet (supports both JSON Navigation format and raw text)
void processIncomingPacket(const char* packet, size_t len) {
  if (len == 0) return;

  // Check if packet looks like JSON
  if (packet[0] == '{' && (strstr(packet, "\"navigation\"") != nullptr || strstr(packet, "\"speed\"") != nullptr)) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, packet, len);
    if (!err) {
      portENTER_CRITICAL(&btMux);
      navInfo.lastUpdateMs = millis();

      if (doc["navigation"].is<JsonObject>()) {
        JsonObject nav = doc["navigation"];
        const char* road = nav["next_road"] | "";
        const char* sub = nav["next_road_sub"] | "";
        const char* dist = nav["next_road_distance"] | "";
        const char* eta = nav["eta"] | "";
        const char* ete = nav["ete"] | "";
        const char* totDist = nav["distance"] | "";
        const char* iconB64 = nav["icon"] | "";

        strncpy(navInfo.nextRoad, road, sizeof(navInfo.nextRoad) - 1);
        navInfo.nextRoad[sizeof(navInfo.nextRoad) - 1] = '\0';

        strncpy(navInfo.nextRoadSub, sub, sizeof(navInfo.nextRoadSub) - 1);
        navInfo.nextRoadSub[sizeof(navInfo.nextRoadSub) - 1] = '\0';

        strncpy(navInfo.distance, dist, sizeof(navInfo.distance) - 1);
        navInfo.distance[sizeof(navInfo.distance) - 1] = '\0';

        strncpy(navInfo.eta, eta, sizeof(navInfo.eta) - 1);
        navInfo.eta[sizeof(navInfo.eta) - 1] = '\0';

        strncpy(navInfo.ete, ete, sizeof(navInfo.ete) - 1);
        navInfo.ete[sizeof(navInfo.ete) - 1] = '\0';

        strncpy(navInfo.totalDistance, totDist, sizeof(navInfo.totalDistance) - 1);
        navInfo.totalDistance[sizeof(navInfo.totalDistance) - 1] = '\0';

        if (strlen(iconB64) > 0) {
          size_t olen = 0;
          int ret = mbedtls_base64_decode(navInfo.iconBitmap, sizeof(navInfo.iconBitmap),
                                          &olen, (const unsigned char*)iconB64, strlen(iconB64));
          navInfo.hasIcon = (ret == 0 && olen == sizeof(navInfo.iconBitmap));
        } else {
          navInfo.hasIcon = false;
        }

        navInfo.active = (strlen(navInfo.nextRoad) > 0 || strlen(navInfo.distance) > 0);
      }

      if (doc["speed"].is<int>()) {
        navInfo.speed = doc["speed"].as<int>();
      }

      btNewMessage = true;
      portEXIT_CRITICAL(&btMux);

      if (Serial.availableForWrite() > 0) {
        Serial.printf("[NAV RX] Road: '%s' | Dist: '%s' | Speed: %d km/h\n",
                      navInfo.nextRoad, navInfo.distance, navInfo.speed);
      }
      return;
    }
  }

  // Fallback to standard text message handling
  setBtMessage(packet, len);
}

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t* /* param */) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    btConnected = true;
    btNewMessage = true;
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BT Classic] Client connected.");
    }
    bluetoothSerial.println("ESP32-OLED Ready!\r\n");
  } else if (event == ESP_SPP_CLOSE_EVT) {
    btConnected = false;
    btNewMessage = true;
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BT Classic] Client disconnected.");
    }
  }
}
#else
class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    btConnected = true;
    btNewMessage = true;
    // Request balanced connection parameters: min 30ms, max 60ms, latency 0, supervision timeout 1800ms
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BLE] Client connected.");
    }
  }
  void onDisconnect(NimBLEServer* /* server */, NimBLEConnInfo& /* connInfo */, int reason) override {
    btConnected = false;
    btNewMessage = true;
    if (Serial.availableForWrite() > 0) {
      Serial.printf("[BLE] Client disconnected (reason: 0x%02X). Restarting advertising...\n", reason);
    }
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& /* connInfo */) override {
    if (Serial.availableForWrite() > 0) {
      Serial.printf("[BLE] MTU negotiated: %u\n", MTU);
    }
  }
};

static std::string bleRxAccumulator = "";
static uint32_t lastBleRxMs = 0;

void handleBleRxChunk(const uint8_t* data, size_t len) {
  if (len == 0) return;
  const uint32_t now = millis();

  if (!bleRxAccumulator.empty() && (now - lastBleRxMs > 500)) {
    bleRxAccumulator.clear();
  }
  lastBleRxMs = now;

  bleRxAccumulator.append(reinterpret_cast<const char*>(data), len);

  size_t nlPos = bleRxAccumulator.find('\n');
  while (nlPos != std::string::npos) {
    std::string packet = bleRxAccumulator.substr(0, nlPos);
    bleRxAccumulator.erase(0, nlPos + 1);

    while (!packet.empty() && (packet.back() == '\r' || packet.back() == ' ')) {
      packet.pop_back();
    }
    while (!packet.empty() && (packet.front() == '\r' || packet.front() == ' ')) {
      packet.erase(0, 1);
    }

    if (!packet.empty()) {
      processIncomingPacket(packet.c_str(), packet.size());
      if (pTxCharacteristic != nullptr && btConnected) {
        pTxCharacteristic->setValue("OK\r\n");
        pTxCharacteristic->notify();
      }
    }

    nlPos = bleRxAccumulator.find('\n');
  }

  if (bleRxAccumulator.size() > kMaxBtPacketBytes * 2) {
    bleRxAccumulator.clear();
  }
}

class TextCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /* connInfo */) override {
    std::string val = characteristic->getValue();
    if (!val.empty()) {
      handleBleRxChunk(reinterpret_cast<const uint8_t*>(val.data()), val.size());
    }
  }
};
#endif

uint8_t scanOledAddress() {
  prepareForI2C();

  // Probe standard SSD1306 addresses first (0x3C, 0x3D)
  const uint8_t commonAddresses[] = { 0x3C, 0x3D };
  for (uint8_t addr : commonAddresses) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (display.begin(SSD1306_SWITCHCAPVCC, addr)) {
        return addr;
      }
    }
  }

  // Scan range 0x08 - 0x77
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (addr == 0x3C || addr == 0x3D) continue;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (display.begin(SSD1306_SWITCHCAPVCC, addr)) {
        return addr;
      }
    }
  }
  return 0;
}

// Render Google Maps Navigation HUD on SSD1306 128x64 OLED
void renderNavHud() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // 1. Maneuver Icon (32x32 at top-left 0, 0)
  if (navInfo.hasIcon) {
    display.drawBitmap(0, 0, navInfo.iconBitmap, 32, 32, SSD1306_WHITE);
  } else {
    display.drawRoundRect(2, 2, 28, 28, 4, SSD1306_WHITE);
    display.drawLine(16, 6, 16, 26, SSD1306_WHITE);
    display.drawLine(16, 6, 10, 13, SSD1306_WHITE);
    display.drawLine(16, 6, 22, 13, SSD1306_WHITE);
  }

  // Vertical separator next to maneuver icon
  display.drawLine(33, 0, 33, 31, SSD1306_WHITE);

  // 2. Distance to turn
  if (strlen(navInfo.distance) > 0) {
    if (strlen(navInfo.distance) <= 6) {
      display.setTextSize(2);
      display.setCursor(37, 1);
      display.print(navInfo.distance);
    } else {
      display.setTextSize(1);
      display.setCursor(37, 4);
      display.print(navInfo.distance);
    }
  }

  // 3. Trip info (ETA / ETE / Remaining Distance)
  display.setTextSize(1);
  display.setCursor(37, 20);
  if (strlen(navInfo.ete) > 0 && strlen(navInfo.totalDistance) > 0) {
    display.printf("%s (%s)", navInfo.ete, navInfo.totalDistance);
  } else if (strlen(navInfo.eta) > 0) {
    display.printf("ETA %s", navInfo.eta);
  } else if (strlen(navInfo.ete) > 0) {
    display.printf("%s rem", navInfo.ete);
  }

  // 4. Real-time GPS Speedometer in top right corner
  if (navInfo.speed >= 0) {
    display.setTextSize(1);
    display.setCursor(102, 1);
    display.printf("%3d", navInfo.speed);
    display.setCursor(102, 11);
    display.print("km/h");
  }

  // Horizontal separator between top HUD and bottom street info
  display.drawLine(0, 32, 127, 32, SSD1306_WHITE);

  // 5. Main street / highway name
  display.setTextSize(1);
  display.setCursor(0, 36);
  if (strlen(navInfo.nextRoad) > 0) {
    display.print(navInfo.nextRoad);
  } else {
    display.print("Proceed to route");
  }

  // 6. Sub-road / maneuver instruction
  display.setCursor(0, 50);
  if (strlen(navInfo.nextRoadSub) > 0) {
    display.print(navInfo.nextRoadSub);
  } else if (strlen(navInfo.eta) > 0 && strlen(navInfo.totalDistance) > 0) {
    display.printf("ETA: %s | %s", navInfo.eta, navInfo.totalDistance);
  }

  display.display();
}

void updateOled(const char* condition, uint32_t cycle) {
  if (!oledReady) return;

  // Turn off LED during I2C communications
  prepareForI2C();

  // If navigation is active and has received data within timeout, render HUD!
  if (navInfo.active && (millis() - navInfo.lastUpdateMs < kNavTimeoutMs)) {
    renderNavHud();
    return;
  }

  char currentBt[kMaxBtBytes + 1];
  portENTER_CRITICAL(&btMux);
  memcpy(currentBt, btMessage, sizeof(currentBt));
  portEXIT_CRITICAL(&btMux);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Line 1: Header
  display.println("ESP32 Studio");
  display.println("---------------------");

  // Line 2: Chip & Condition
  display.printf("Status : %s\n", condition);

  // Line 3: Bluetooth message received
  display.printf("BT Msg : %s\n", currentBt);

  // Line 4: Speedometer (if available) or Cycle diagnostics
  if (navInfo.speed >= 0 && (millis() - navInfo.lastUpdateMs < 30000)) {
    display.printf("Speed  : %d km/h\n", navInfo.speed);
  } else {
    display.printf("Cycle  : #%lu\n", cycle);
  }
  display.printf("Uptime : %lu s\n", millis() / 1000);

  display.display();
}

}  // namespace

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT > 0)
  // Disable blocking TX timeout on USB CDC so prints never stall loop() when disconnected
  Serial.setTxTimeoutMs(0);
#endif
  delay(1000);

  // Initialize LED in OFF state
  turnLedOff();

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       ESP32 Blink, OLED & Bluetooth      ");
  Serial.printf ("  Chip Model   : %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf ("  LED Pin      : GPIO %d\n", LED_PIN);
  Serial.printf ("  Active Logic : %s\n", LED_ACTIVE_LOW ? "Active-LOW" : "Active-HIGH");
  Serial.printf ("  I2C Pins     : SDA=%d, SCL=%d\n", OLED_SDA_PIN, OLED_SCL_PIN);
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  Serial.println("  Bluetooth    : Bluetooth Classic (SPP)");
#else
  Serial.println("  Bluetooth    : BLE (NimBLE Nordic UART)");
#endif
  Serial.printf ("  Device Name  : %s\n", kDeviceName);
  Serial.println("==========================================");

  // Probe and initialize OLED display (graceful skip if absent)
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oledAddress = scanOledAddress();

  if (oledAddress != 0) {
    oledReady = true;
    Serial.printf("[I2C] SSD1306 OLED initialized at 0x%02X\n", oledAddress);
    updateOled("INITIALIZED", 0);
  } else {
    oledReady = false;
    Serial.println("[I2C] OLED not detected; falling back to LED blink.");
  }

  // Initialize Bluetooth transport
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  bluetoothSerial.register_callback(sppCallback);
  bluetoothSerial.enableSSP(false, false); // Enable Simple Secure Pairing ("Just Works")
  bool btStarted = bluetoothSerial.begin(kDeviceName);
  Serial.printf("[BT] Classic Bluetooth %s as '%s'\n", btStarted ? "started" : "FAILED", kDeviceName);
#else
  NimBLEDevice::init(kDeviceName);
  NimBLEServer* server = NimBLEDevice::createServer();
  static ServerCallbacks serverCallbacks;
  server->setCallbacks(&serverCallbacks);

  // 1. Nordic UART Service (standard BLE Serial for Serial Bluetooth Terminal / nRF Connect / Web / Android App)
  NimBLEService* nusService = server->createService(kNordicServiceUuid);
  NimBLECharacteristic* rxChar = nusService->createCharacteristic(
      kNordicRxUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  static TextCallbacks textCallbacks;
  rxChar->setCallbacks(&textCallbacks);

  pTxCharacteristic = nusService->createCharacteristic(
      kNordicTxUuid,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  pTxCharacteristic->setValue("Ready\r\n");

  // 2. Legacy custom service for backward compatibility
  NimBLEService* legacyService = server->createService(kLegacyServiceUuid);
  NimBLECharacteristic* legacyChar = legacyService->createCharacteristic(
      kLegacyCharUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ);
  legacyChar->setCallbacks(&textCallbacks);
  legacyChar->setValue("Ready");

  // Start server so services are registered with host
  server->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setName(kDeviceName);
  advertising->addServiceUUID(kNordicServiceUuid);
  bool advStarted = advertising->start();
  Serial.printf("[BLE] Advertised as '%s' (%s)\n  NUS Service: %s\n  RX (Write) : %s\n  TX (Notify): %s\n",
                kDeviceName, advStarted ? "STARTED" : "FAILED",
                kNordicServiceUuid, kNordicRxUuid, kNordicTxUuid);
#endif
}

void loop() {
  const uint32_t now = millis();

  // Check Bluetooth Classic incoming data (non-blocking chunk & line handling)
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  static char btClassicRxBuf[kMaxBtPacketBytes + 1];
  static size_t btClassicRxLen = 0;
  static uint32_t lastBtRxByteMs = 0;

  while (bluetoothSerial.available()) {
    char c = (char)bluetoothSerial.read();
    lastBtRxByteMs = now;
    if (c == '\r' || c == '\n') {
      if (btClassicRxLen > 0) {
        btClassicRxBuf[btClassicRxLen] = '\0';
        processIncomingPacket(btClassicRxBuf, btClassicRxLen);
        bluetoothSerial.print("OK\r\n");
        btClassicRxLen = 0;
      }
    } else if (btClassicRxLen < kMaxBtPacketBytes) {
      btClassicRxBuf[btClassicRxLen++] = c;
    }
  }

  // Handle line without newline (flush after 150ms idle)
  if (btClassicRxLen > 0 && (now - lastBtRxByteMs >= 150)) {
    btClassicRxBuf[btClassicRxLen] = '\0';
    processIncomingPacket(btClassicRxBuf, btClassicRxLen);
    bluetoothSerial.print("OK\r\n");
    btClassicRxLen = 0;
  }
#else
  // Flush un-terminated BLE packet after 150ms idle
  if (!bleRxAccumulator.empty() && (now - lastBleRxMs >= 150)) {
    std::string packet = bleRxAccumulator;
    bleRxAccumulator.clear();
    while (!packet.empty() && (packet.back() == '\r' || packet.back() == '\n' || packet.back() == ' ')) {
      packet.pop_back();
    }
    if (!packet.empty()) {
      processIncomingPacket(packet.c_str(), packet.size());
      if (pTxCharacteristic != nullptr && btConnected) {
        pTxCharacteristic->setValue("OK\r\n");
        pTxCharacteristic->notify();
      }
    }
  }
#endif

  // If OLED display is present: update display periodically and DO NOT blink LED
  if (oledReady) {
    static uint32_t lastOledUpdateMs = 0;
    static uint32_t oledCycle = 0;
    constexpr uint32_t kOledIntervalMs = 500; // Periodic display update every 500ms

    if (btNewMessage || (now - lastOledUpdateMs >= kOledIntervalMs)) {
      lastOledUpdateMs = now;
      btNewMessage = false;
      oledCycle++;

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
      const char* status = btConnected ? "BT Connected" : "BT Ready (SPP)";
#else
      const char* status = btConnected ? "BLE Connected" : "BLE Advertising";
#endif
      updateOled(status, oledCycle);

      // Periodically log status to Serial Monitor every 2 seconds
      static uint32_t lastSerialLogMs = 0;
      if (now - lastSerialLogMs >= 2000) {
        lastSerialLogMs = now;
        if (Serial.availableForWrite() > 0) {
          if (navInfo.active && (now - navInfo.lastUpdateMs < kNavTimeoutMs)) {
            Serial.printf("[%lu ms] Cycle #%lu: NAV '%s' | Dist: %s | Speed: %d km/h\n",
                          now, oledCycle, navInfo.nextRoad, navInfo.distance, navInfo.speed);
          } else {
            Serial.printf("[%lu ms] Cycle #%lu: %s | BT Note: %s\n", now, oledCycle, status, btMessage);
          }
        }
      }
    }
  } else {
    // If no OLED is detected: fallback to blinking the onboard LED periodically
    static uint32_t lastBlinkMs = 0;
    static bool ledState = false;
    static uint32_t blinkCycle = 0;

    if (now - lastBlinkMs >= kBlinkDelayMs) {
      lastBlinkMs = now;
      ledState = !ledState;
      if (ledState) {
        blinkCycle++;
        turnLedOn();
        if (Serial.availableForWrite() > 0) {
          Serial.printf("[%lu ms] Cycle #%lu: LED ON\n", now, blinkCycle);
        }
      } else {
        turnLedOff();
        if (Serial.availableForWrite() > 0) {
          Serial.printf("[%lu ms] Cycle #%lu: LED OFF\n", now, blinkCycle);
        }
      }
    }
  }

  delay(10); // Yield to FreeRTOS watchdog & background tasks
}

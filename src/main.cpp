#include <Arduino.h>
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  #include <BluetoothSerial.h>
#else
  #include <NimBLEDevice.h>
#endif
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

namespace {

// Bluetooth Constants
constexpr char kDeviceName[] = "ESP32-OLED";
constexpr size_t kMaxBtBytes = 32;

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

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t* /* param */) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    btConnected = true;
    btNewMessage = true;
    if (Serial.availableForWrite() > 0) {
      Serial.println("[BT Classic] Client connected.");
    }
    bluetoothSerial.println("ESP32-OLED Ready!\r\nType text to display on screen.");
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
};

class TextCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /* connInfo */) override {
    std::string val = characteristic->getValue();
    // Trim trailing whitespace & newlines (\r, \n)
    while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ')) {
      val.pop_back();
    }
    size_t start = 0;
    while (start < val.size() && (val[start] == '\r' || val[start] == '\n' || val[start] == ' ')) {
      start++;
    }
    if (start > 0) {
      val = val.substr(start);
    }

    if (!val.empty()) {
      setBtMessage(val.data(), val.size());
      if (pTxCharacteristic != nullptr && btConnected) {
        std::string reply = "OK: " + val + "\r\n";
        pTxCharacteristic->setValue(reply);
        pTxCharacteristic->notify();
      }
    }
  }
};
#endif

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

void updateOled(const char* condition, uint32_t cycle) {
  if (!oledReady) return;

  // Turn off LED during I2C communications
  prepareForI2C();

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

  // Line 4: Diagnostics
  display.printf("Cycle  : #%lu\n", cycle);
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
  Serial.println("  Bluetooth    : Bluetooth Classic");
#else
  Serial.println("  Bluetooth    : BLE (NimBLE)");
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

  // 1. Nordic UART Service (standard BLE Serial for Serial Bluetooth Terminal / nRF Connect / Web)
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

  // In NimBLE 2.x, start server so services are registered with host
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
  static char btClassicRxBuf[kMaxBtBytes + 1];
  static size_t btClassicRxLen = 0;
  static uint32_t lastBtRxByteMs = 0;

  while (bluetoothSerial.available()) {
    char c = (char)bluetoothSerial.read();
    lastBtRxByteMs = now;
    if (c == '\r' || c == '\n') {
      if (btClassicRxLen > 0) {
        btClassicRxBuf[btClassicRxLen] = '\0';
        setBtMessage(btClassicRxBuf, btClassicRxLen);
        bluetoothSerial.printf("OK: %s\r\n", btClassicRxBuf);
        btClassicRxLen = 0;
      }
    } else if (btClassicRxLen < kMaxBtBytes) {
      btClassicRxBuf[btClassicRxLen++] = c;
    }
  }

  // Handle line without newline (flush after 100ms idle)
  if (btClassicRxLen > 0 && (now - lastBtRxByteMs >= 100)) {
    btClassicRxBuf[btClassicRxLen] = '\0';
    setBtMessage(btClassicRxBuf, btClassicRxLen);
    bluetoothSerial.printf("OK: %s\r\n", btClassicRxBuf);
    btClassicRxLen = 0;
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
          Serial.printf("[%lu ms] Cycle #%lu: %s | BT Note: %s\n", now, oledCycle, status, btMessage);
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

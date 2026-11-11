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
constexpr char kServiceUuid[] = "8b5d0001-6e8f-4c2a-9b73-6d414c454001";
constexpr char kTextCharacteristicUuid[] = "8b5d0002-6e8f-4c2a-9b73-6d414c454001";
constexpr size_t kMaxBtBytes = 32;

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
BluetoothSerial bluetoothSerial;
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
char btMessage[kMaxBtBytes + 1] = "Ready (BLE)";
volatile bool btConnected = false;
volatile bool btNewMessage = false;

void setBtMessage(const char* msg, size_t len) {
  const size_t count = len < kMaxBtBytes ? len : kMaxBtBytes;
  portENTER_CRITICAL(&btMux);
  memcpy(btMessage, msg, count);
  btMessage[count] = '\0';
  btNewMessage = true;
  portEXIT_CRITICAL(&btMux);
  Serial.printf("[BT RX] %s\n", btMessage);
}

#if !defined(USE_CLASSIC_BT) || !defined(CONFIG_IDF_TARGET_ESP32)
class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /* server */, NimBLEConnInfo& /* connInfo */) override {
    btConnected = true;
    Serial.println("[BLE] Client connected.");
  }
  void onDisconnect(NimBLEServer* /* server */, NimBLEConnInfo& /* connInfo */, int /* reason */) override {
    btConnected = false;
    Serial.println("[BLE] Client disconnected. Restarting advertising...");
    NimBLEDevice::startAdvertising();
  }
};

class TextCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /* connInfo */) override {
    const std::string val = characteristic->getValue();
    setBtMessage(val.data(), val.size());
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

// Ensure the LED is always turned off before any I2C communication.
// On ESP32-C3 SuperMini, GPIO 8 is shared between the onboard LED and I2C SDA.
// Driving the LED LOW grounds the SDA line, causing I2C bus lockup.
void prepareForI2C() {
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

void updateOled(const char* condition, uint32_t cycle, bool isLedOn) {
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
  display.printf("LED    : %s\n", isLedOn ? "ON" : "OFF");

  // Line 3: Bluetooth status / note
  display.printf("BT Note: %s\n", currentBt);

  // Line 4: Diagnostics
  display.printf("Cycle  : #%lu\n", cycle);
  display.printf("Uptime : %lu s\n", millis() / 1000);

  display.display();
}

}  // namespace

void setup() {
  Serial.begin(115200);
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
    updateOled("INIT", 0, false);
  } else {
    oledReady = false;
    Serial.println("[I2C] OLED not detected; skipping display.");
  }

  // Initialize Bluetooth transport
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  setBtMessage("Ready (Classic BT)", 18);
  bluetoothSerial.begin(kDeviceName);
  Serial.printf("[BT] Classic Bluetooth ready as '%s'\n", kDeviceName);
#else
  setBtMessage("Ready (BLE)", 11);
  NimBLEDevice::init(kDeviceName);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = server->createService(kServiceUuid);
  NimBLECharacteristic* textChar = service->createCharacteristic(
      kTextCharacteristicUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ);
  textChar->setCallbacks(new TextCallbacks());
  textChar->setValue("Ready");

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setName(kDeviceName);
  advertising->start();
  Serial.printf("[BLE] Advertised as '%s'\n  Service: %s\n  Write  : %s\n",
                kDeviceName, kServiceUuid, kTextCharacteristicUuid);
#endif
}

void loop() {
  static uint32_t cycle = 0;
  cycle++;

  // Check Bluetooth Classic incoming data if enabled
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  if (bluetoothSerial.available()) {
    String str = bluetoothSerial.readStringUntil('\n');
    str.trim();
    if (str.length() > 0) {
      setBtMessage(str.c_str(), str.length());
    }
  }
#endif

  // --- LED ON Phase ---
  // Update OLED condition while LED is safely OFF during I2C communication
  updateOled("LED ON", cycle, true);

  // Turn LED ON for the ON interval
  turnLedOn();
  Serial.printf("[%lu ms] Cycle #%lu: LED ON\n", millis(), cycle);
  delay(kBlinkDelayMs);

  // --- LED OFF Phase ---
  // Turn LED OFF
  turnLedOff();
  Serial.printf("[%lu ms] Cycle #%lu: LED OFF\n", millis(), cycle);

  // Update OLED condition (LED is OFF during I2C communication)
  updateOled("LED OFF", cycle, false);
  delay(kBlinkDelayMs);
}

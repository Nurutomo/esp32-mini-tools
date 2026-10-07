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
constexpr char kDeviceName[] = "ESP32-OLED";
constexpr char kServiceUuid[] = "8b5d0001-6e8f-4c2a-9b73-6d414c454001";
constexpr char kTextCharacteristicUuid[] = "8b5d0002-6e8f-4c2a-9b73-6d414c454001";
constexpr size_t kMaxTextBytes = 20;
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
BluetoothSerial bluetoothSerial;
#endif

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool displayReady = false;
portMUX_TYPE textMux = portMUX_INITIALIZER_UNLOCKED;
char pendingText[kMaxTextBytes + 1] = "Ready - connect BLE";
volatile bool textPending = true;

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
void queueText(const String& value) {
  const size_t count = value.length() < kMaxTextBytes ? value.length() : kMaxTextBytes;
  portENTER_CRITICAL(&textMux);
  memcpy(pendingText, value.c_str(), count);
  pendingText[count] = '\0';
  textPending = true;
  portEXIT_CRITICAL(&textMux);
}
#else
void queueText(const std::string& value) {
  const size_t count = value.size() < kMaxTextBytes ? value.size() : kMaxTextBytes;
  portENTER_CRITICAL(&textMux);
  memcpy(pendingText, value.data(), count);
  pendingText[count] = '\0';
  textPending = true;
  portEXIT_CRITICAL(&textMux);
}

class TextCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic,
               NimBLEConnInfo& /* connectionInfo */) override {
    queueText(characteristic->getValue());
  }
};
#endif

uint8_t findOledAddress() {
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device found at 0x%02X\\n", address);
      if (display.begin(SSD1306_SWITCHCAPVCC, address)) {
        return address;
      }
    }
  }
  return 0;
}

void renderText(const char* text) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(true);
  display.setCursor(0, 0);
  display.println("BLE message:");
  display.println();
  display.println(text);
  display.display();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  const uint8_t oledAddress = findOledAddress();
  if (oledAddress == 0) {
    Serial.println("SSD1306 not found; check wiring and I2C pins");
  } else {
    displayReady = true;
    Serial.printf("SSD1306 initialized at 0x%02X\\n", oledAddress);
    renderText("Starting Bluetooth...");
  }

#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  bluetoothSerial.begin(kDeviceName);
  Serial.printf("Bluetooth Classic ready: %s\\n", kDeviceName);
#else
  NimBLEDevice::init(kDeviceName);
  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* service = server->createService(kServiceUuid);
  NimBLECharacteristic* textCharacteristic = service->createCharacteristic(
      kTextCharacteristicUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  textCharacteristic->setCallbacks(new TextCallbacks());
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->start();
  Serial.printf("BLE ready: %s\\nService: %s\\nWrite: %s\\n",
                kDeviceName, kServiceUuid, kTextCharacteristicUuid);
#endif
}

void loop() {
#if defined(USE_CLASSIC_BT) && defined(CONFIG_IDF_TARGET_ESP32)
  if (bluetoothSerial.available()) {
    String value = bluetoothSerial.readStringUntil('\n');
    queueText(value);
  }
#endif
  char localText[kMaxTextBytes + 1];
  bool shouldRender = false;
  portENTER_CRITICAL(&textMux);
  if (textPending) {
    memcpy(localText, pendingText, sizeof(localText));
    textPending = false;
    shouldRender = true;
  }
  portEXIT_CRITICAL(&textMux);

  if (shouldRender && displayReady) {
    renderText(localText);
  }
  delay(20);
}

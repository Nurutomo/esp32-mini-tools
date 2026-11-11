#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// Active logic for the LED:
// ESP32-C3 SuperMini onboard blue LED is active-low (LOW = ON, HIGH = OFF).
// Standard ESP32 DevKit onboard LED on GPIO 2 is active-high (HIGH = ON, LOW = OFF).
constexpr uint8_t LED_ON  = LED_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t LED_OFF = LED_ACTIVE_LOW ? HIGH : LOW;

// Blink delay in milliseconds (500ms ON, 500ms OFF)
constexpr uint32_t kBlinkDelayMs = 500;

// OLED instance
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledReady = false;
uint8_t oledAddress = 0;

void turnLedOff() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
}

void turnLedOn() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON);
}

void prepareForI2C() {
  // Always turn off the LED before I2C communication to avoid bus conflicts
  turnLedOff();

#if defined(CONFIG_IDF_TARGET_ESP32C3)
  // On ESP32-C3 SuperMini, GPIO 8 is shared between the onboard LED and I2C SDA.
  // Re-establish I2C bus master mode on the pins
  Wire.end();
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
#endif
}

uint8_t scanOledAddress() {
  prepareForI2C();

  // Scan standard SSD1306 I2C addresses (0x3C and 0x3D) first, then all 0x08-0x77
  const uint8_t commonAddresses[] = { 0x3C, 0x3D };
  for (uint8_t addr : commonAddresses) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (display.begin(SSD1306_SWITCHCAPVCC, addr)) {
        return addr;
      }
    }
  }

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

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP32 Mini Tools");
  display.println("---------------------");
  display.printf("Chip     : %s\n", ESP.getChipModel());
  display.printf("Condition: %s\n", condition);
  display.printf("LED State: %s\n", isLedOn ? "ON" : "OFF");
  display.printf("Cycle    : #%lu\n", cycle);
  display.printf("Uptime   : %lu s\n", millis() / 1000);
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Turn off the LED initially
  turnLedOff();

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       ESP32 LED Blink & OLED Display     ");
  Serial.printf ("  Chip Model   : %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf ("  LED Pin      : GPIO %d\n", LED_PIN);
  Serial.printf ("  Active Logic : %s\n", LED_ACTIVE_LOW ? "Active-LOW" : "Active-HIGH");
  Serial.printf ("  I2C Pins     : SDA=%d, SCL=%d\n", OLED_SDA_PIN, OLED_SCL_PIN);
  Serial.println("==========================================");

  // Initialize I2C and probe for OLED
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oledAddress = scanOledAddress();

  if (oledAddress != 0) {
    oledReady = true;
    Serial.printf("[I2C] SSD1306 OLED found and initialized at address 0x%02X\n", oledAddress);
    updateOled("INITIALIZED", 0, false);
  } else {
    oledReady = false;
    Serial.println("[I2C] OLED not detected on I2C bus; skipping display.");
  }
}

void loop() {
  static uint32_t cycle = 0;
  cycle++;

  // --- LED ON Phase ---
  // Update OLED condition first (while LED is turned off during I2C communication)
  updateOled("BLINK: LED ON", cycle, true);

  // Now turn LED ON for the ON delay period
  turnLedOn();
  Serial.printf("[%lu ms] Cycle #%lu: LED ON\n", millis(), cycle);
  delay(kBlinkDelayMs);

  // --- LED OFF Phase ---
  // Turn LED OFF
  turnLedOff();
  Serial.printf("[%lu ms] Cycle #%lu: LED OFF\n", millis(), cycle);

  // Update OLED condition (LED is OFF during I2C communication)
  updateOled("BLINK: LED OFF", cycle, false);
  delay(kBlinkDelayMs);
}

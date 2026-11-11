#include <Arduino.h>
#include "config.h"

// Active logic for the LED:
// ESP32-C3 SuperMini onboard blue LED is active-low (LOW = ON, HIGH = OFF).
// Standard ESP32 DevKit onboard LED on GPIO 2 is active-high (HIGH = ON, LOW = OFF).
constexpr uint8_t LED_ON  = LED_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t LED_OFF = LED_ACTIVE_LOW ? HIGH : LOW;

// Blink delay in milliseconds (500ms ON, 500ms OFF = 1 Hz blink)
constexpr uint32_t kBlinkDelayMs = 500;

void setup() {
  Serial.begin(115200);
  delay(1000);  // Allow USB/Serial connection to stabilize

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       ESP32 LED Blink Program            ");
  Serial.printf ("  Chip Model   : %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf ("  LED Pin      : GPIO %d\n", LED_PIN);
  Serial.printf ("  Active Logic : %s\n", LED_ACTIVE_LOW ? "Active-LOW" : "Active-HIGH");
  Serial.println("==========================================");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
}

void loop() {
  static uint32_t cycle = 0;
  cycle++;

  // Turn LED ON
  digitalWrite(LED_PIN, LED_ON);
  Serial.printf("[%lu ms] Cycle #%lu: LED ON\n", millis(), cycle);
  delay(kBlinkDelayMs);

  // Turn LED OFF
  digitalWrite(LED_PIN, LED_OFF);
  Serial.printf("[%lu ms] Cycle #%lu: LED OFF\n", millis(), cycle);
  delay(kBlinkDelayMs);
}

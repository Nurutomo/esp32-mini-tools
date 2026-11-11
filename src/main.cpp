#include <Arduino.h>
#include "config.h"
#include "navigation_state.h"
#include "display_manager.h"
#include "bluetooth_manager.h"

namespace {

constexpr char kDeviceName[] = "ESP32-OLED";
constexpr uint32_t kBlinkDelayMs = 500;
constexpr uint32_t kDisplayIntervalMs = 500;

// Packet handler dispatched by Bluetooth Manager
void onBluetoothPacket(const char* packet, size_t len) {
  if (processNavigationJson(packet, len)) {
    const NavigationData& nav = getNavigationData();
    if (Serial.availableForWrite() > 0) {
      Serial.printf("[NAV RX] Road: '%s' | Dist: '%s' | Speed: %d km/h\n",
                    nav.nextRoad, nav.distance, nav.speed);
    }
  } else {
    setLastTextMessage(packet, len);
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT > 0)
  Serial.setTxTimeoutMs(0);
#endif
  delay(1000);

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

  // Initialize display
  if (initDisplay(OLED_SDA_PIN, OLED_SCL_PIN)) {
    Serial.printf("[I2C] SSD1306 OLED initialized at 0x%02X\n", getDisplayAddress());
    updateDisplay("INITIALIZED", getLastTextMessage(), 0);
  } else {
    Serial.println("[I2C] OLED not detected; falling back to LED blink.");
  }

  // Initialize Bluetooth
  initBluetooth(kDeviceName, onBluetoothPacket);
}

void loop() {
  const uint32_t now = millis();

  // Process incoming Bluetooth data
  updateBluetooth();

  // Display or LED update
  if (isDisplayReady()) {
    static uint32_t lastDisplayMs = 0;
    static uint32_t cycleCount = 0;

    if (now - lastDisplayMs >= kDisplayIntervalMs) {
      lastDisplayMs = now;
      cycleCount++;

      updateDisplay(getBluetoothStatus(), getLastTextMessage(), cycleCount);

      // Diagnostic logging every 2 seconds
      static uint32_t lastLogMs = 0;
      if (now - lastLogMs >= 2000) {
        lastLogMs = now;
        if (Serial.availableForWrite() > 0) {
          if (isNavigationActive()) {
            const NavigationData& nav = getNavigationData();
            Serial.printf("[%lu ms] Cycle #%lu: NAV '%s' | Dist: %s | Speed: %d km/h\n",
                          now, cycleCount, nav.nextRoad, nav.distance, nav.speed);
          } else {
            Serial.printf("[%lu ms] Cycle #%lu: %s | BT Note: %s\n",
                          now, cycleCount, getBluetoothStatus(), getLastTextMessage());
          }
        }
      }
    }
  } else {
    // Fallback: Blink onboard LED if no OLED display detected
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

  delay(10); // FreeRTOS yield
}

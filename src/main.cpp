#include <Arduino.h>
#include "config.h"
#include "navigation_state.h"
#include "display_manager.h"
#include "bluetooth_manager.h"

namespace {

constexpr char kDeviceName[] = "ESP32-OLED";
constexpr uint32_t kBlinkDelayMs = 500;
constexpr uint32_t kDisplayIntervalMs = 500;
constexpr uint8_t kDefaultAnalogInputPin = 0;
constexpr uint32_t kAnalogReadIntervalMs = 50;
constexpr size_t kSerialCommandSize = 48;

volatile uint8_t gRequestedAnalogPin = kDefaultAnalogInputPin;
volatile uint8_t gActiveAnalogPin = kDefaultAnalogInputPin;
volatile uint16_t gAnalogInputValue = 0;
constexpr uint16_t kButtonTolerance = 180;
constexpr uint32_t kSelectLongPressMs = 800;
constexpr uint8_t kMenuItemCount = 3;
uint8_t gSelectedMenuItem = 0;
bool gMenuOpen = false;
bool gButtonMonitorOpen = false;
bool gButton2Down = false;
bool gButton2LongHandled = false;
uint32_t gButton2DownMs = 0;
uint32_t gLastButtonActionMs = 0;
char gSerialCommand[kSerialCommandSize];
size_t gSerialCommandLength = 0;

void analogInputTask(void* parameter) {
  (void)parameter;
  uint8_t configuredPin = 0xFF;
  for (;;) {
    const uint8_t requestedPin = gRequestedAnalogPin;
    if (requestedPin != configuredPin) {
      pinMode(requestedPin, INPUT);
      configuredPin = requestedPin;
      gActiveAnalogPin = configuredPin;
    }
    gAnalogInputValue = static_cast<uint16_t>(analogRead(configuredPin));
    vTaskDelay(pdMS_TO_TICKS(kAnalogReadIntervalMs));
  }
}

bool isAdcCapablePin(long pin) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  return pin >= 0 && pin <= 5;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return pin >= 1 && pin <= 20;
#else
  return pin >= 0 && pin <= 39;
#endif
}

void processSerialCommand(const char* command) {
  if (strncmp(command, "apin ", 5) == 0) {
    char* end = nullptr;
    const long pin = strtol(command + 5, &end, 10);
    if (end == command + 5 || *end != '\0' || !isAdcCapablePin(pin)) {
      Serial.println("Usage: apin <ADC-capable GPIO> (valid pins vary by board)");
      return;
    }
    gRequestedAnalogPin = static_cast<uint8_t>(pin);
    Serial.printf("Analog input pin requested: GPIO %ld\n", pin);
    return;
  }
  if (strcmp(command, "apin?") == 0 || strcmp(command, "help") == 0) {
    Serial.printf("Analog input: GPIO %u, value %u\n", gActiveAnalogPin, gAnalogInputValue);
    Serial.println("Change with: apin <GPIO number> (valid ADC GPIO depends on board)");
    return;
  }
  if (command[0] != '\0') {
    Serial.println("Unknown command. Use 'apin <GPIO>' or 'apin?'.");
  }
}

bool nearAdcValue(uint16_t value, uint16_t target) {
  return value >= target - kButtonTolerance && value <= target + kButtonTolerance;
}

uint8_t detectButtonMask(uint16_t value) {
  if (nearAdcValue(value, 4095)) return 0x01;
  if (nearAdcValue(value, 2700)) return 0x06;
  if (nearAdcValue(value, 2270)) return 0x02;
  if (nearAdcValue(value, 1410)) return 0x04;
  return 0;
}

const char* buttonLabel(uint8_t mask) {
  switch (mask) {
    case 0x01: return "BTN 1";
    case 0x02: return "BTN 2";
    case 0x04: return "BTN 3";
    case 0x06: return "BTN 2+3";
    default: return "NONE";
  }
}

void pollUiButtons(uint8_t buttonMask, uint32_t now) {
  if (buttonMask == 0x02 && !gButton2Down) {
    gButton2Down = true;
    gButton2LongHandled = false;
    gButton2DownMs = now;
  }

  if (gButton2Down && !gButton2LongHandled && now - gButton2DownMs >= kSelectLongPressMs) {
    // Long press always goes back one level; from the HUD it opens no menu.
    if (gButtonMonitorOpen) gButtonMonitorOpen = false;
    else gMenuOpen = false;
    gButton2LongHandled = true;
    gLastButtonActionMs = now;
  }

  if (buttonMask != 0x02 && gButton2Down) {
    gButton2Down = false;
    if (!gButton2LongHandled && now - gLastButtonActionMs >= 180) {
      if (gButtonMonitorOpen) {
        // A short select on the monitor returns to the menu.
        gButtonMonitorOpen = false;
        gMenuOpen = true;
      } else if (gMenuOpen) {
        if (gSelectedMenuItem == 0) gMenuOpen = false;
        else if (gSelectedMenuItem == 1) gButtonMonitorOpen = true;
      } else {
        gMenuOpen = true;
      }
      gLastButtonActionMs = now;
    }
  }

  if (!gMenuOpen || gButtonMonitorOpen || now - gLastButtonActionMs < 180) return;
  if (buttonMask == 0x01) {
    if (gSelectedMenuItem > 0) --gSelectedMenuItem;
    gLastButtonActionMs = now;
  } else if (buttonMask == 0x04) {
    if (gSelectedMenuItem + 1 < kMenuItemCount) ++gSelectedMenuItem;
    gLastButtonActionMs = now;
  }
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (gSerialCommandLength > 0) {
        gSerialCommand[gSerialCommandLength] = '\0';
        processSerialCommand(gSerialCommand);
        gSerialCommandLength = 0;
      }
    } else if (gSerialCommandLength < sizeof(gSerialCommand) - 1) {
      gSerialCommand[gSerialCommandLength++] = c;
    } else {
      gSerialCommandLength = 0;
      Serial.println("Serial command too long; discarded.");
    }
  }
}

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

  pinMode(kDefaultAnalogInputPin, INPUT);
  xTaskCreate(
      analogInputTask,
      "analog-input",
      2048,
      nullptr,
      1,
      nullptr);

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

  // Probe and initialize a connected OLED once at startup. Further probes in
  // isDisplayReady() will reconnect a display hot-plugged later.
  if (initDisplay(OLED_SDA_PIN, OLED_SCL_PIN)) {
    Serial.printf("[I2C] SSD1306 OLED initialized at 0x%02X\n", getDisplayAddress());
    updateDisplay("INITIALIZED", getLastTextMessage(), 0);
  } else {
    Serial.println("[I2C] OLED not detected; will retry I2C hotplug detection.");
  }

  // Initialize Bluetooth
  initBluetooth(kDeviceName, onBluetoothPacket);
  Serial.printf("Analog input pin: GPIO %u. Type 'apin?' for value/help, 'apin <GPIO>' to change.\n",
                gActiveAnalogPin);
}

void loop() {
  const uint32_t now = millis();

  // Process incoming Bluetooth data and serial configuration commands.
  updateBluetooth();
  pollSerialCommands();

  // The analog-input task samples independently; decode the resistor ladder for UI input.
  const uint16_t analogValue = gAnalogInputValue;
  const uint8_t buttons = detectButtonMask(analogValue);
  pollUiButtons(buttons, now);

  // Probe/reconnect OLED periodically, or blink LED while it is absent.
  if (isDisplayReady()) {
    static uint32_t lastDisplayMs = 0;
    static uint32_t cycleCount = 0;

    if (now - lastDisplayMs >= kDisplayIntervalMs) {
      lastDisplayMs = now;
      cycleCount++;

      if (gButtonMonitorOpen) {
        renderButtonMonitor(analogValue, gActiveAnalogPin, buttonLabel(buttons));
      } else if (gMenuOpen) {
        renderMenuScreen(gSelectedMenuItem, "Main menu", nullptr);
      } else {
        updateDisplay(getBluetoothStatus(), getLastTextMessage(), cycleCount);
      }

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

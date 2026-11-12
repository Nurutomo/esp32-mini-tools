#include <Arduino.h>
#include "config.h"
#include "navigation_state.h"
#include "display_manager.h"
#include "bluetooth_manager.h"
#include "ui_manager.h"
#include "ui.h"
#include "ui/buzzer.h"
#include "ui/settings.h"

namespace {

constexpr char kDeviceName[] = "ESP32-OLED";
constexpr uint32_t kBlinkDelayMs = 250;
constexpr uint32_t kDisplayIntervalMs = 25;
constexpr uint8_t kDefaultAnalogInputPin = 0;
constexpr uint8_t kDefaultBuzzerPin = 10;
constexpr uint32_t kAnalogReadIntervalMs = 25;
constexpr size_t kSerialCommandSize = 48;

volatile uint8_t gRequestedAnalogPin = kDefaultAnalogInputPin;
volatile uint8_t gActiveAnalogPin = kDefaultAnalogInputPin;
volatile uint16_t gAnalogInputValue = 0;
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

bool isValidOutputPin(long pin) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  return pin >= 0 && pin <= 21;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return pin >= 0 && pin <= 48;
#else
  return pin >= 0 && pin <= 33;
#endif
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
  if (strcmp(command, "save") == 0) {
    const ui::State& state = ui::state();
    if (ui::settings::save(state.pins)) Serial.println("Settings saved to flash");
    else Serial.println("Failed to save settings");
    return;
  }
  if (strcmp(command, "beep") == 0) {
    ui::buzzer::beep(2000, 150);
    Serial.println("Buzzer test beep");
    return;
  }
  if (strncmp(command, "bpin ", 5) == 0) {
    char* end = nullptr;
    const long pin = strtol(command + 5, &end, 10);
    if (end == command + 5 || *end != '\0' || !isValidOutputPin(pin) ||
        pin == gActiveAnalogPin || pin == OLED_SDA_PIN || pin == OLED_SCL_PIN || pin == LED_PIN) {
      Serial.println("Usage: bpin <available output GPIO>");
      return;
    }
    ui::setBuzzerPin(static_cast<uint8_t>(pin));
    Serial.printf("Buzzer pin set to GPIO %ld\n", pin);
    return;
  }
  if (strncmp(command, "apin ", 5) == 0) {
    char* end = nullptr;
    const long pin = strtol(command + 5, &end, 10);
    if (end == command + 5 || *end != '\0' || !isAdcCapablePin(pin)) {
      Serial.println("Usage: apin <ADC-capable GPIO> (valid pins vary by board)");
      return;
    }
    if (pin == ui::buzzer::pin() || pin == OLED_SDA_PIN || pin == OLED_SCL_PIN || pin == LED_PIN) {
      Serial.println("ADC pin conflicts with buzzer, OLED, or LED GPIO");
      return;
    }
    gRequestedAnalogPin = static_cast<uint8_t>(pin);
    ui::setAnalogButtonPin(static_cast<uint8_t>(pin));
    Serial.printf("Analog input pin requested: GPIO %ld\n", pin);
    return;
  }
  if (strcmp(command, "apin?") == 0 || strcmp(command, "help") == 0) {
    Serial.printf("Analog input: GPIO %u, value %u\n", gActiveAnalogPin, gAnalogInputValue);
    Serial.printf("Buzzer output: GPIO %u\n", ui::buzzer::pin());
    Serial.println("Commands: apin <GPIO>, bpin <GPIO>, save, beep");
    return;
  }
  if (command[0] != '\0') {
    Serial.println("Unknown command. Use 'apin <GPIO>' or 'apin?'.");
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

  ui::begin();
  gRequestedAnalogPin = ui::state().pins.analogButton;
  gActiveAnalogPin = ui::state().pins.analogButton;
  pinMode(gActiveAnalogPin, INPUT);
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

  // Initialize Bluetooth after UI configuration has loaded.
  initBluetooth(kDeviceName, onBluetoothPacket);
  Serial.printf("Analog input: GPIO %u. Buzzer: GPIO %u. Commands: apin <GPIO>, bpin <GPIO>, apin?\n",
                gActiveAnalogPin, ui::buzzer::pin());
}

void loop() {
  const uint32_t now = millis();

  // Process incoming Bluetooth data and serial configuration commands.
  updateBluetooth();
  pollSerialCommands();
  ui::buzzer::update(now);

  // The analog-input task samples independently; decode the resistor ladder for UI input.
  const uint16_t analogValue = gAnalogInputValue;
  ui::update(analogValue, now);
  const ui::State& uiState = ui::state();

  // Probe/reconnect OLED periodically, or blink LED while it is absent.
  if (isDisplayReady()) {
    static uint32_t lastDisplayMs = 0;
    static uint32_t cycleCount = 0;

    if (now - lastDisplayMs >= kDisplayIntervalMs) {
      lastDisplayMs = now;
      cycleCount++;

      ui::renderScreen(uiState, analogValue, gActiveAnalogPin, getBluetoothStatus(),
                       getLastTextMessage(), getNavigationData().speed, cycleCount);

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

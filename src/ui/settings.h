#pragma once

#include <stdint.h>
#include "config.h"
#include "wifi_manager.h"
#include "settings_model.h"

namespace ui::settings {

constexpr uint8_t kDefaultAnalogButtonPin = 0;
constexpr uint8_t kDefaultBuzzerPin = 10;

#if defined(CONFIG_IDF_TARGET_ESP32C3)
constexpr uint8_t kMaxGpio = 21;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint8_t kMaxGpio = 48;
#else
constexpr uint8_t kMaxGpio = 39;
#endif

struct Pins {
  uint8_t analogButton;
  uint8_t buzzer;
  bool buzzerEnabled = false;
};

// Everything the settings screen edits, in one persisted record.
struct Values {
  Pins pins{kDefaultAnalogButtonPin, kDefaultBuzzerPin};
  WifiCredentials wifi;
};

void load(Values& values);
bool save(const Values& values);
void render(const Values& values, uint8_t selectedOption, bool editing);

// Edit mode: what up/down does to the selected entry.
void edit(Values& values, uint8_t selectedOption, int8_t direction, uint8_t maxGpio);
// Short-select on a Text entry advances the cursor; returns false when the text
// is finished so the controller can leave edit mode.
bool advanceText(Values& values, uint8_t selectedOption);
// Short-select on a Pin/Toggle entry cycles its value once.
void toggle(Values& values, uint8_t selectedOption, uint8_t maxGpio);

const char* valueText(const Values& values, uint8_t selectedOption, char* buffer, size_t size);
const char* editText(const Values& values, uint8_t selectedOption);
uint8_t textCursor(uint8_t selectedOption);
void resetTextCursor(uint8_t selectedOption);

bool validAnalogPin(uint8_t pin, uint8_t maxGpio);
bool validBuzzerPin(uint8_t pin, uint8_t maxGpio, uint8_t analogPin, uint8_t oledSda,
                    uint8_t oledScl, uint8_t ledPin);

}  // namespace ui::settings

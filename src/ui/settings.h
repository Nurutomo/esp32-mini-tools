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
  WifiBook wifi;
};

void load(Values& values);
bool save(const Values& values);
void render(const Values& values, uint8_t selectedOption, bool editing);

// Edit mode: what up/down does to the selected entry. Text entries are typed
// on the keyboard screen instead, so this ignores them.
void edit(Values& values, uint8_t selectedOption, int8_t direction, uint8_t maxGpio);

// Text entry, always on the active Wi-Fi profile.
const char* textValue(const Values& values, uint8_t selectedOption);
bool textIsSecret(uint8_t selectedOption);
void textAppend(Values& values, uint8_t selectedOption, char ch);
void textErase(Values& values, uint8_t selectedOption);

// Saved profiles.
WifiCredentials* activeProfile(Values& values);
const WifiCredentials* activeProfile(const Values& values);
void cycleProfile(Values& values);
bool selectNetwork(Values& values, const char* ssid);
bool forgetActive(Values& values);

const char* valueText(const Values& values, uint8_t selectedOption, char* buffer, size_t size);

bool validAnalogPin(uint8_t pin, uint8_t maxGpio);
bool validBuzzerPin(uint8_t pin, uint8_t maxGpio, uint8_t analogPin, uint8_t oledSda,
                    uint8_t oledScl, uint8_t ledPin);

}  // namespace ui::settings

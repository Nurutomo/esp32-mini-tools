#include "settings.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "display_internal.h"

namespace ui::settings {

void render(const Values& values, uint8_t selectedOption, bool editing) {
  display::settings(values, selectedOption, editing);
}

namespace {

// Text-entry cursor per entry index. One byte each; the registry is small.
uint8_t gTextCursor[16];

char* textField(Values& values, model::Id id) {
  switch (id) {
    case model::Id::WifiNetwork: return values.wifi.ssid;
    case model::Id::WifiPassword: return values.wifi.password;
    case model::Id::WifiIdentity: return values.wifi.identity;
    case model::Id::WifiUsername: return values.wifi.username;
    default: return nullptr;
  }
}

size_t textCapacity(model::Id id) {
  switch (id) {
    case model::Id::WifiNetwork: return sizeof(WifiCredentials::ssid);
    case model::Id::WifiPassword: return sizeof(WifiCredentials::password);
    case model::Id::WifiIdentity: return sizeof(WifiCredentials::identity);
    case model::Id::WifiUsername: return sizeof(WifiCredentials::username);
    default: return 0;
  }
}

}  // namespace

void load(Values& values) {
  Preferences prefs;
  if (!prefs.begin("esp32ui", true)) return;
  values.pins.analogButton = prefs.getUChar("apin", kDefaultAnalogButtonPin);
  values.pins.buzzer = prefs.getUChar("bpin", kDefaultBuzzerPin);
  values.pins.buzzerEnabled = prefs.getBool("ben", false);
  prefs.getString("wssid", values.wifi.ssid, sizeof(values.wifi.ssid));
  prefs.getString("wpass", values.wifi.password, sizeof(values.wifi.password));
  prefs.getString("wid", values.wifi.identity, sizeof(values.wifi.identity));
  prefs.getString("wuser", values.wifi.username, sizeof(values.wifi.username));
  prefs.end();
}

bool validAnalogPin(uint8_t pin, uint8_t maxGpio) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  return pin <= 5 && pin <= maxGpio;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return pin >= 1 && pin <= 20 && pin <= maxGpio;
#else
  return pin <= 39 && pin <= maxGpio;
#endif
}

bool validBuzzerPin(uint8_t pin, uint8_t maxGpio, uint8_t analogPin, uint8_t oledSda,
                    uint8_t oledScl, uint8_t ledPin) {
  return pin <= maxGpio && pin != analogPin && pin != oledSda && pin != oledScl && pin != ledPin;
}

bool save(const Values& values) {
  const Pins& pins = values.pins;
  if (!validAnalogPin(pins.analogButton, kMaxGpio) ||
      !validBuzzerPin(pins.buzzer, kMaxGpio, pins.analogButton, OLED_SDA_PIN, OLED_SCL_PIN,
                      LED_PIN))
    return false;
  Preferences prefs;
  if (!prefs.begin("esp32ui", false)) return false;
  bool ok = prefs.putUChar("apin", pins.analogButton) == 1 &&
            prefs.putUChar("bpin", pins.buzzer) == 1 &&
            prefs.putBool("ben", pins.buzzerEnabled) == 1;
#if defined(USE_WIFI)
  ok = ok && prefs.putString("wssid", values.wifi.ssid) == 1 &&
       prefs.putString("wpass", values.wifi.password) == 1 &&
       prefs.putString("wid", values.wifi.identity) == 1 &&
       prefs.putString("wuser", values.wifi.username) == 1;
#endif
  prefs.end();
  return ok;
}

void edit(Values& values, uint8_t selectedOption, int8_t direction, uint8_t maxGpio) {
  if (direction == 0) return;
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr) return;

  switch (entry->kind) {
    case model::Kind::Pin: {
      uint8_t& pin = entry->id == model::Id::AnalogPin ? values.pins.analogButton
                                                       : values.pins.buzzer;
      const int16_t next = static_cast<int16_t>(pin) + direction;
      if (next < 0 || next > maxGpio) return;
      const uint8_t candidate = static_cast<uint8_t>(next);
      if (entry->id == model::Id::AnalogPin
              ? validAnalogPin(candidate, maxGpio)
              : validBuzzerPin(candidate, maxGpio, values.pins.analogButton, OLED_SDA_PIN,
                               OLED_SCL_PIN, LED_PIN)) pin = candidate;
      return;
    }
    case model::Kind::Toggle:
      values.pins.buzzerEnabled = direction > 0;
      return;
    case model::Kind::Text: {
      char* field = textField(values, entry->id);
      if (field == nullptr || selectedOption >= sizeof(gTextCursor)) return;
      model::cycleTextChar(field, textCapacity(entry->id), gTextCursor[selectedOption], direction);
      return;
    }
    case model::Kind::Action:
      return;
  }
}

void toggle(Values& values, uint8_t selectedOption, uint8_t maxGpio) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr) return;
  if (entry->kind == model::Kind::Toggle) {
    values.pins.buzzerEnabled = !values.pins.buzzerEnabled;
  } else if (entry->kind == model::Kind::Pin) {
    edit(values, selectedOption, 1, maxGpio);
  }
}

bool advanceText(Values& values, uint8_t selectedOption) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr || entry->kind != model::Kind::Text) return false;
  char* field = textField(values, entry->id);
  if (field == nullptr || selectedOption >= sizeof(gTextCursor)) return false;

  uint8_t& cursor = gTextCursor[selectedOption];
  const size_t len = strlen(field);
  if (cursor >= len) {
    cursor = 0;
    return false;  // Select on the end cell exits text edit mode.
  }
  ++cursor;  // The next position may be the end cell; it remains editable.
  return true;
}

uint8_t textCursor(uint8_t selectedOption) {
  return selectedOption < sizeof(gTextCursor) ? gTextCursor[selectedOption] : 0;
}

void resetTextCursor(uint8_t selectedOption) {
  if (selectedOption < sizeof(gTextCursor)) gTextCursor[selectedOption] = 0;
}

const char* editText(const Values& values, uint8_t selectedOption) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr || entry->kind != model::Kind::Text) return nullptr;
  switch (entry->id) {
    case model::Id::WifiNetwork: return values.wifi.ssid;
    case model::Id::WifiPassword: return values.wifi.password;
    case model::Id::WifiIdentity: return values.wifi.identity;
    case model::Id::WifiUsername: return values.wifi.username;
    default: return nullptr;
  }
}

const char* valueText(const Values& values, uint8_t selectedOption, char* buffer, size_t size) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr || buffer == nullptr || size == 0) return "";
  switch (entry->id) {
    case model::Id::AnalogPin: snprintf(buffer, size, "GPIO %u", values.pins.analogButton); break;
    case model::Id::BuzzerPin: snprintf(buffer, size, "GPIO %u", values.pins.buzzer); break;
    case model::Id::BuzzerEnabled: snprintf(buffer, size, "%s", values.pins.buzzerEnabled ? "ON" : "OFF"); break;
#if defined(USE_WIFI)
    case model::Id::WifiNetwork:
      snprintf(buffer, size, "%s", values.wifi.ssid[0] != '\0' ? values.wifi.ssid : "(scan)");
      break;
    case model::Id::WifiPassword:
      // Never reveal the passphrase on screen.
      snprintf(buffer, size, "%s", values.wifi.password[0] != '\0' ? "********" : "(empty)");
      break;
    case model::Id::WifiIdentity: snprintf(buffer, size, "%s", values.wifi.identity[0] != '\0' ? values.wifi.identity : "(empty)"); break;
    case model::Id::WifiUsername: snprintf(buffer, size, "%s", values.wifi.username[0] != '\0' ? values.wifi.username : "(empty)"); break;
    case model::Id::WifiConnect: snprintf(buffer, size, "%s", wifiStatusText()); break;
#endif
    case model::Id::Save: snprintf(buffer, size, "save settings"); break;
    default: buffer[0] = '\0'; break;
  }
  return buffer;
}

}  // namespace ui::settings

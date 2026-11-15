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

constexpr char kNvsNamespace[] = "esp32ui";
constexpr char kBookKey[] = "wifi";

char* textField(Values& values, model::Id id) {
  WifiCredentials* profile = activeProfile(values);
  if (profile == nullptr) return nullptr;
  switch (id) {
    case model::Id::WifiSsid: return profile->ssid;
    case model::Id::WifiPassword: return profile->password;
    case model::Id::WifiIdentity: return profile->identity;
    case model::Id::WifiUsername: return profile->username;
    default: return nullptr;
  }
}

const char* textField(const Values& values, model::Id id) {
  const WifiCredentials* profile = activeProfile(values);
  switch (id) {
    case model::Id::WifiSsid: return profile->ssid;
    case model::Id::WifiPassword: return profile->password;
    case model::Id::WifiIdentity: return profile->identity;
    case model::Id::WifiUsername: return profile->username;
    default: return nullptr;
  }
}

size_t textCapacity(model::Id id) {
  switch (id) {
    case model::Id::WifiSsid: return sizeof(WifiCredentials::ssid);
    case model::Id::WifiPassword: return sizeof(WifiCredentials::password);
    case model::Id::WifiIdentity: return sizeof(WifiCredentials::identity);
    case model::Id::WifiUsername: return sizeof(WifiCredentials::username);
    default: return 0;
  }
}

// Reject a corrupt blob rather than acting on uninitialised profile data.
bool validBook(const WifiBook& book) { return model::profiles::valid(book); }

}  // namespace

WifiCredentials* activeProfile(Values& values) {
  WifiBook& book = values.wifi;
  if (book.count == 0) {
    // An empty book still needs somewhere to type the first network.
    book.count = 1;
    book.active = 0;
  }
  if (book.active >= book.count) book.active = static_cast<uint8_t>(book.count - 1);
  return &book.list[book.active];
}

const WifiCredentials* activeProfile(const Values& values) {
  const WifiBook& book = values.wifi;
  return &book.list[book.active < kMaxWifiProfiles ? book.active : 0];
}

void cycleProfile(Values& values) { model::profiles::cycle(values.wifi); }

bool selectNetwork(Values& values, const char* ssid) {
  return model::profiles::select(values.wifi, ssid);
}

bool forgetActive(Values& values) { return model::profiles::forget(values.wifi); }

void load(Values& values) {
  values.wifi = WifiBook{};
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, true)) return;
  values.pins.analogButton = prefs.getUChar("apin", kDefaultAnalogButtonPin);
  values.pins.buzzer = prefs.getUChar("bpin", kDefaultBuzzerPin);
  values.pins.buzzerEnabled = prefs.getBool("ben", false);

  WifiBook book;
  const size_t size = prefs.getBytesLength(kBookKey);
  if (size == sizeof(book) && prefs.getBytes(kBookKey, &book, sizeof(book)) == sizeof(book) &&
      validBook(book)) {
    values.wifi = book;
  } else {
    values.wifi = WifiBook{};
    // Migrate the single-profile keys written by earlier firmware.
    WifiCredentials legacy;
    prefs.getString("wssid", legacy.ssid, sizeof(legacy.ssid));
    prefs.getString("wpass", legacy.password, sizeof(legacy.password));
    prefs.getString("wid", legacy.identity, sizeof(legacy.identity));
    prefs.getString("wuser", legacy.username, sizeof(legacy.username));
    if (legacy.ssid[0] != '\0' || legacy.password[0] != '\0' ||
        legacy.identity[0] != '\0' || legacy.username[0] != '\0') {
      values.wifi.list[0] = legacy;
      values.wifi.count = 1;
    }
    values.wifi.active = 0;
  }
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
#if defined(USE_WIFI)
  const WifiBook& book = values.wifi;
  if (!validBook(book)) return false;
#endif
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, false)) return false;
  bool ok = prefs.putUChar("apin", pins.analogButton) == 1 &&
            prefs.putUChar("bpin", pins.buzzer) == 1 &&
            prefs.putBool("ben", pins.buzzerEnabled) == 1;
#if defined(USE_WIFI)
  ok = ok && prefs.putBytes(kBookKey, &values.wifi, sizeof(values.wifi)) ==
                   sizeof(values.wifi);
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
    case model::Kind::Text:
    case model::Kind::Action:
      return;
  }
}

const char* textValue(const Values& values, uint8_t selectedOption) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr || entry->kind != model::Kind::Text) return nullptr;
  return textField(values, entry->id);
}

bool textIsSecret(uint8_t selectedOption) {
  const model::Entry* entry = model::entryAt(selectedOption);
  return entry != nullptr && entry->secret;
}

void textAppend(Values& values, uint8_t selectedOption, char ch) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr || entry->kind != model::Kind::Text) return;
  char* field = textField(values, entry->id);
  if (field == nullptr) return;
  model::keyboard::insertChar(field, textCapacity(entry->id), ch);
}

void textErase(Values& values, uint8_t selectedOption) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr || entry->kind != model::Kind::Text) return;
  char* field = textField(values, entry->id);
  if (field != nullptr) model::keyboard::eraseChar(field);
}

const char* valueText(const Values& values, uint8_t selectedOption, char* buffer, size_t size) {
  const model::Entry* entry = model::entryAt(selectedOption);
  if (entry == nullptr || buffer == nullptr || size == 0) return "";
#if defined(USE_WIFI)
  const WifiCredentials* profile = activeProfile(values);
#endif
  switch (entry->id) {
    case model::Id::AnalogPin: snprintf(buffer, size, "GPIO %u", values.pins.analogButton); break;
    case model::Id::BuzzerPin: snprintf(buffer, size, "GPIO %u", values.pins.buzzer); break;
    case model::Id::BuzzerEnabled: snprintf(buffer, size, "%s", values.pins.buzzerEnabled ? "ON" : "OFF"); break;
#if defined(USE_WIFI)
    case model::Id::WifiSsid:
      snprintf(buffer, size, "%s", profile->ssid[0] != '\0' ? profile->ssid : "(scan)");
      break;
    case model::Id::WifiPassword:
      // Never reveal the passphrase on screen.
      snprintf(buffer, size, "%s", profile->password[0] != '\0' ? "********" : "(empty)");
      break;
    case model::Id::WifiIdentity: snprintf(buffer, size, "%s", profile->identity[0] != '\0' ? profile->identity : "(empty)"); break;
    case model::Id::WifiUsername: snprintf(buffer, size, "%s", profile->username[0] != '\0' ? profile->username : "(empty)"); break;
    case model::Id::WifiSaved:
      if (values.wifi.count == 0) snprintf(buffer, size, "(none saved)");
      else snprintf(buffer, size, "%.14s %u/%u", profile->ssid, values.wifi.active + 1,
                    values.wifi.count);
      break;
    case model::Id::WifiForget: snprintf(buffer, size, "%u saved", values.wifi.count); break;
    case model::Id::WifiConnect: snprintf(buffer, size, "%s", wifiStatusText()); break;
    case model::Id::WifiNetwork: snprintf(buffer, size, "scan networks"); break;
#endif
    case model::Id::Save: snprintf(buffer, size, "save settings"); break;
    default: buffer[0] = '\0'; break;
  }
  return buffer;
}

}  // namespace ui::settings

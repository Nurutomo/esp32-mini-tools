#pragma once

#include <stdint.h>
#include <string.h>

// Settings registry and value-editing rules. Deliberately free of Arduino
// dependencies so scripts/test_settings_logic.cpp can compile it on the host.
namespace ui::settings::model {

enum class Kind : uint8_t { Pin, Toggle, Text, Action };

enum class Id : uint8_t {
  AnalogPin,
  BuzzerPin,
  BuzzerEnabled,
  WifiNetwork,
  WifiPassword,
  WifiIdentity,
  WifiUsername,
  WifiConnect,
  Save,
};

struct Entry {
  Id id;
  const char* label;
  Kind kind;
  bool secret;
};

inline const Entry* entries(uint8_t& count) {
  static constexpr Entry kAll[] = {
      {Id::AnalogPin, "Analog button", Kind::Pin, false},
      {Id::BuzzerPin, "Buzzer pin", Kind::Pin, false},
      {Id::BuzzerEnabled, "Buzzer", Kind::Toggle, false},
#if defined(USE_WIFI)
      {Id::WifiNetwork, "Wi-Fi network", Kind::Action, false},
      {Id::WifiPassword, "Wi-Fi password", Kind::Text, true},
      {Id::WifiIdentity, "eduroam identity", Kind::Text, false},
      {Id::WifiUsername, "eduroam username", Kind::Text, false},
      {Id::WifiConnect, "Connect Wi-Fi", Kind::Action, false},
#endif
      {Id::Save, "Save & exit", Kind::Action, false},
  };
  count = static_cast<uint8_t>(sizeof(kAll) / sizeof(kAll[0]));
  return kAll;
}

inline uint8_t itemCount() {
  uint8_t count = 0;
  entries(count);
  return count;
}

inline uint8_t indexOf(Id id) {
  uint8_t count = 0;
  const Entry* list = entries(count);
  for (uint8_t i = 0; i < count; ++i) {
    if (list[i].id == id) return i;
  }
  return count;
}

inline const Entry* entryAt(uint8_t index) {
  uint8_t count = 0;
  const Entry* list = entries(count);
  return index < count ? &list[index] : nullptr;
}

inline uint8_t clampIndex(int16_t index) {
  const uint8_t count = itemCount();
  if (count == 0) return 0;
  if (index < 0) return 0;
  return static_cast<uint8_t>(index) >= count ? static_cast<uint8_t>(count - 1)
                                              : static_cast<uint8_t>(index);
}

inline uint8_t nextIndex(uint8_t index) { return clampIndex(static_cast<int16_t>(index) + 1); }
inline uint8_t prevIndex(uint8_t index) { return clampIndex(static_cast<int16_t>(index) - 1); }

// Actions run and leave edit mode; everything else holds a value.
inline bool isEditable(uint8_t index) {
  const Entry* entry = entryAt(index);
  return entry != nullptr && entry->kind != Kind::Action;
}

// --- Text fields -------------------------------------------------------------

constexpr uint8_t kCharsetLen = 95;
inline uint8_t charsetIndex(char ch) {
  return ch >= 32 && ch <= 126 ? static_cast<uint8_t>(ch - 32) : 0;
}

inline char charsetAt(uint8_t index) {
  return static_cast<char>(32 + index % kCharsetLen);
}

// Cycle the character at `pos`. Down past space erases the character, which is
// the only backspace the three-button UI has. Up at the terminator appends.
inline void cycleTextChar(char* text, size_t capacity, size_t pos, int8_t direction) {
  if (text == nullptr || capacity == 0 || direction == 0) return;
  const size_t len = strlen(text);
  if (pos > len) pos = len;

  if (pos == len) {
    if (direction < 0 || len + 1 >= capacity) return;
    text[len] = charsetAt(1);  // first character after space
    text[len + 1] = '\0';
    return;
  }

  const uint8_t index = charsetIndex(text[pos]);
  if (direction > 0) {
    text[pos] = charsetAt(static_cast<uint8_t>((index + 1) % kCharsetLen));
  } else if (index == 0) {
    memmove(text + pos, text + pos + 1, len - pos);  // includes the terminator
  } else {
    text[pos] = charsetAt(static_cast<uint8_t>(index - 1));
  }
}

// --- Wi-Fi classification ----------------------------------------------------

enum class Security : uint8_t { Wpa2, Eduroam };

// Only an exact "eduroam" SSID takes the Enterprise path.
inline Security classifySsid(const char* ssid) {
  return ssid != nullptr && strcmp(ssid, "eduroam") == 0 ? Security::Eduroam
                                                         : Security::Wpa2;
}

// eduroam needs identity/username/password; ordinary WPA2 needs a passphrase.
inline bool credentialsComplete(Security security, const char* password,
                                const char* identity, const char* username) {
  if (security == Security::Eduroam) {
    return identity != nullptr && identity[0] != '\0' && username != nullptr &&
           username[0] != '\0' && password != nullptr && password[0] != '\0';
  }
  return password != nullptr && password[0] != '\0';
}

}  // namespace ui::settings::model

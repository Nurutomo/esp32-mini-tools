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
  WifiSsid,
  WifiPassword,
  WifiIdentity,
  WifiUsername,
  WifiSaved,
  WifiForget,
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
      {Id::WifiNetwork, "Wi-Fi scan", Kind::Action, false},
      {Id::WifiSsid, "SSID (manual)", Kind::Text, false},
      {Id::WifiPassword, "Wi-Fi password", Kind::Text, true},
      {Id::WifiIdentity, "eduroam identity", Kind::Text, false},
      {Id::WifiUsername, "eduroam username", Kind::Text, false},
      {Id::WifiSaved, "Saved network", Kind::Action, false},
      {Id::WifiForget, "Forget network", Kind::Action, false},
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

// Actions run immediately; Pin/Toggle edit in place; Text opens the keyboard.
inline bool isEditable(uint8_t index) {
  const Entry* entry = entryAt(index);
  return entry != nullptr && entry->kind != Kind::Action;
}

// --- On-screen keyboard ------------------------------------------------------
// A three-row character grid followed by SP, DEL, PAGE and OK.
namespace keyboard {

constexpr uint8_t kCols = 10;
constexpr uint8_t kRows = 3;
constexpr uint8_t kCharCells = kCols * kRows;
constexpr uint8_t kCells = kCharCells + 4;

enum class Action : uint8_t { Insert, Space, Backspace, Page, Done, Empty };
struct Cell { Action action; char ch; };

inline const char* pageChars(uint8_t page) {
  static constexpr const char* kPages[] = {
      "abcdefghijklmnopqrstuvwxyz0123",
      "456789ABCDEFGHIJKLMNOPQRSTUVWX",
      "YZ!\"#$%&'()*+,-./:;<=>?@[]^_{}",
      "\\|`~",
  };
  return kPages[page < 4 ? page : 0];
}

inline uint8_t pageCharCount(uint8_t page) {
  return static_cast<uint8_t>(strlen(pageChars(page)));
}

inline uint8_t pageCount() { return 4; }

inline const char* pageLabel(uint8_t page) {
  static constexpr const char* kLabels[] = {"a-z0", "4-X", "YZ#", "#4"};
  return kLabels[page < 4 ? page : 0];
}

inline Cell cellAt(uint8_t page, uint8_t index) {
  if (index >= kCells) return {Action::Empty, '\0'};
  if (index >= kCharCells) {
    static constexpr Action kControls[] = {Action::Space, Action::Backspace, Action::Page,
                                           Action::Done};
    return {kControls[index - kCharCells], '\0'};
  }
  return index < pageCharCount(page) ? Cell{Action::Insert, pageChars(page)[index]}
                                     : Cell{Action::Empty, '\0'};
}

inline uint8_t clampCell(uint8_t page, uint8_t index) {
  if (page >= pageCount()) page = 0;
  if (index >= kCharCells) return index < kCells ? index : kCharCells;
  return index < pageCharCount(page) ? index : 0;
}

inline uint8_t step(uint8_t page, uint8_t index, int8_t direction) {
  if (page >= pageCount()) page = 0;
  if (direction == 0) return clampCell(page, index);
  uint8_t next = clampCell(page, index);
  for (uint8_t guard = 0; guard < kCells; ++guard) {
    next = static_cast<uint8_t>((next + kCells + (direction > 0 ? 1 : -1)) % kCells);
    if (cellAt(page, next).action != Action::Empty) return next;
  }
  return next;
}

inline void insertChar(char* text, size_t capacity, char ch) {
  if (text == nullptr || ch == '\0') return;
  const size_t length = strlen(text);
  if (length + 1 >= capacity) return;
  text[length] = ch;
  text[length + 1] = '\0';
}

inline void eraseChar(char* text) {
  if (text == nullptr) return;
  const size_t length = strlen(text);
  if (length > 0) text[length - 1] = '\0';
}

}  // namespace keyboard

// --- Saved Wi-Fi profile rules ----------------------------------------------
namespace profiles {

template <typename Book>
inline bool valid(const Book& book) {
  constexpr uint8_t capacity = sizeof(book.list) / sizeof(book.list[0]);
  if (book.version != 1 || book.count > capacity || book.active >= capacity ||
      (book.count > 0 && book.active >= book.count) || (book.count == 0 && book.active != 0))
    return false;
  for (uint8_t i = 0; i < capacity; ++i) {
    const auto& profile = book.list[i];
    if (memchr(profile.ssid, '\0', sizeof(profile.ssid)) == nullptr ||
        memchr(profile.password, '\0', sizeof(profile.password)) == nullptr ||
        memchr(profile.identity, '\0', sizeof(profile.identity)) == nullptr ||
        memchr(profile.username, '\0', sizeof(profile.username)) == nullptr ||
        (i >= book.count && profile.ssid[0] != '\0'))
      return false;
  }
  return true;
}

template <typename Book>
inline void cycle(Book& book) {
  if (!valid(book) || book.count <= 1) return;
  for (uint8_t step = 1; step <= book.count; ++step) {
    const uint8_t candidate = static_cast<uint8_t>((book.active + step) % book.count);
    if (book.list[candidate].ssid[0] != '\0') {
      book.active = candidate;
      return;
    }
  }
  book.active = 0;
}

template <typename Book>
inline bool select(Book& book, const char* ssid) {
  constexpr size_t capacity = sizeof(book.list[0].ssid);
  if (!valid(book) || ssid == nullptr || ssid[0] == '\0' || strlen(ssid) >= capacity) return false;
  for (uint8_t i = 0; i < book.count; ++i) {
    if (book.list[i].ssid[0] != '\0' && strcmp(book.list[i].ssid, ssid) == 0) {
      book.active = i;
      return true;
    }
  }
  if (book.active < book.count && book.list[book.active].ssid[0] == '\0') {
    book.list[book.active] = typename Book::Profile{};
    strncpy(book.list[book.active].ssid, ssid, capacity - 1);
    return true;
  }
  // A profile with an empty SSID is only a temporary manual-entry slot.
  uint8_t empty = 0;
  while (empty < book.count && book.list[empty].ssid[0] != '\0') ++empty;
  if (empty < book.count) {
    for (uint8_t i = empty; i + 1 < book.count; ++i) book.list[i] = book.list[i + 1];
    book.list[book.count - 1] = typename Book::Profile{};
    --book.count;
    if (book.active > empty) --book.active;
  }
  if (book.count < sizeof(book.list) / sizeof(book.list[0])) {
    book.active = book.count++;
  } else {
    return false;
  }
  auto& profile = book.list[book.active];
  profile = typename Book::Profile{};
  strncpy(profile.ssid, ssid, capacity - 1);
  return true;
}

template <typename Book>
inline bool forget(Book& book) {
  if (!valid(book) || book.count == 0) return false;
  const uint8_t index = book.active;
  for (uint8_t i = index; i + 1 < book.count; ++i) book.list[i] = book.list[i + 1];
  book.list[book.count - 1] = typename Book::Profile{};
  --book.count;
  if (book.active >= book.count) book.active = book.count > 0 ? book.count - 1 : 0;
  return true;
}

}  // namespace profiles

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

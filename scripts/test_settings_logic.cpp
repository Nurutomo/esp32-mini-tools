// Host-side check for keyboard and saved-profile rules.
// Build: g++ -std=c++17 -Wall -Wextra -Werror scripts/test_settings_logic.cpp -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../include/wifi_manager.h"
#define USE_WIFI 1
#include "../src/ui/settings_model.h"

namespace model = ui::settings::model;

int main() {
  // Registry: Wi-Fi entries exist only with USE_WIFI, Save is always last.
  const uint8_t count = model::itemCount();
  assert(count == 12);
  assert(model::entryAt(count - 1)->id == model::Id::Save);
  assert(model::entryAt(count) == nullptr);
  assert(model::indexOf(model::Id::WifiPassword) == 5);
  assert(model::indexOf(model::Id::WifiSaved) == 8);

  assert(model::nextIndex(0) == 1);
  assert(model::prevIndex(0) == 0);
  assert(model::nextIndex(count - 1) == count - 1);
  assert(model::prevIndex(count - 1) == count - 2);
  assert(!model::isEditable(model::indexOf(model::Id::WifiConnect)));
  assert(!model::isEditable(model::indexOf(model::Id::Save)));
  assert(model::isEditable(model::indexOf(model::Id::AnalogPin)));
  assert(model::isEditable(model::indexOf(model::Id::WifiPassword)));

  // Four pages expose every non-space printable ASCII character exactly once.
  using namespace model::keyboard;
  assert(pageCount() == 4);
  assert(pageCharCount(0) == 30);
  assert(pageCharCount(1) == 30);
  assert(pageCharCount(2) == 30);
  assert(pageCharCount(3) == 4);
  bool seen[127] = {};
  for (uint8_t page = 0; page < pageCount(); ++page) {
    for (uint8_t i = 0; i < pageCharCount(page); ++i) {
      const Cell cell = cellAt(page, i);
      assert(cell.action == Action::Insert);
      assert(cell.ch >= 33 && cell.ch <= 126);
      assert(!seen[static_cast<uint8_t>(cell.ch)]);
      seen[static_cast<uint8_t>(cell.ch)] = true;
    }
  }
  for (uint8_t ch = 33; ch <= 126; ++ch) assert(seen[ch]);
  assert(cellAt(0, 30).action == Action::Space);
  assert(cellAt(0, 31).action == Action::Backspace);
  assert(cellAt(0, 32).action == Action::Page);
  assert(cellAt(0, 33).action == Action::Done);
  assert(cellAt(0, 34).action == Action::Empty);
  assert(clampCell(3, 29) == 0);
  assert(clampCell(2, 30) == 30);
  assert(step(3, 0, -1) == 33);  // wraps to OK, skipping empty cells
  assert(step(3, 30, -1) == 3);
  assert(step(0, 29, 1) == 30);
  assert(step(3, 3, 1) == 30);   // skip empty cells, land on SP

  char text[4] = "";
  insertChar(text, sizeof(text), 'x');
  insertChar(text, sizeof(text), ' ');
  insertChar(text, sizeof(text), 'y');
  insertChar(text, sizeof(text), 'z'); // capacity includes the NUL
  assert(strcmp(text, "x y") == 0);
  eraseChar(text);
  assert(strcmp(text, "x ") == 0);
  eraseChar(text);
  eraseChar(text);
  eraseChar(text);
  assert(strcmp(text, "") == 0);
  insertChar(nullptr, sizeof(text), 'x');
  insertChar(text, 0, 'x');

  // Only an exact "eduroam" takes the Enterprise path.
  assert(model::classifySsid("eduroam") == model::Security::Eduroam);
  assert(model::classifySsid("Eduroam") == model::Security::Wpa2);
  assert(model::classifySsid("eduroam-guest") == model::Security::Wpa2);
  assert(model::classifySsid(" eduroam") == model::Security::Wpa2);
  assert(model::classifySsid(nullptr) == model::Security::Wpa2);
  assert(model::credentialsComplete(model::Security::Wpa2, "pw", "", ""));
  assert(!model::credentialsComplete(model::Security::Wpa2, "", "", ""));
  assert(!model::credentialsComplete(model::Security::Eduroam, "pw", "", "user"));
  assert(!model::credentialsComplete(model::Security::Eduroam, "pw", "id", ""));
  assert(model::credentialsComplete(model::Security::Eduroam, "pw", "id", "user"));

  // Profile CRUD, capacity, and blob validation.
  WifiBook book;
  assert(model::profiles::valid(book));
  assert(model::profiles::select(book, "home"));
  strcpy(book.list[0].password, "secret");
  assert(model::profiles::select(book, "work"));
  assert(book.count == 2 && book.active == 1);
  assert(model::profiles::select(book, "home"));
  assert(book.active == 0 && book.count == 2);
  model::profiles::cycle(book);
  assert(book.active == 1);
  assert(model::profiles::forget(book));
  assert(book.count == 1 && book.active == 0);
  assert(strcmp(book.list[0].ssid, "home") == 0);
  assert(model::profiles::forget(book));
  assert(book.count == 0 && book.active == 0);
  assert(!model::profiles::forget(book));
  assert(model::profiles::select(book, "replacement"));
  assert(book.count == 1 && strcmp(book.list[0].ssid, "replacement") == 0);
  assert(model::profiles::valid(book));
  assert(model::profiles::forget(book));
  // An empty active slot, used for manual SSID entry, must be reusable after a scan pick.
  assert(model::profiles::select(book, "temporary"));
  book.list[book.active].ssid[0] = '\0';
  assert(model::profiles::select(book, "replacement"));
  assert(book.count == 1 && strcmp(book.list[0].ssid, "replacement") == 0);
  assert(model::profiles::forget(book));
  for (uint8_t i = 0; i < kMaxWifiProfiles; ++i) {
    char ssid[8];
    snprintf(ssid, sizeof(ssid), "net%u", i);
    assert(model::profiles::select(book, ssid));
  }
  assert(book.count == kMaxWifiProfiles);
  assert(!model::profiles::select(book, "overflow"));
  assert(model::profiles::valid(book));
  WifiBook bad = book;
  bad.version = 2;
  assert(!model::profiles::valid(bad));
  bad = book;
  bad.active = kMaxWifiProfiles;
  assert(!model::profiles::valid(bad));
  bad = book;
  memset(bad.list[0].ssid, 'x', sizeof(bad.list[0].ssid));  // unterminated
  assert(!model::profiles::valid(bad));

  printf("settings logic OK (%u entries)\n", count);
  return 0;
}

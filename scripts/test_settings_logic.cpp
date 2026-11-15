// Host-side check for the settings registry and text-editing rules.
// Build: g++ -std=c++17 -Wall -Wextra -Werror scripts/test_settings_logic.cpp -o /tmp/t
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define USE_WIFI 1
#include "../src/ui/settings_model.h"

namespace model = ui::settings::model;

int main() {
  // Registry: Wi-Fi entries exist only with USE_WIFI, Save is always last.
  const uint8_t count = model::itemCount();
  assert(count == 9);
  assert(model::entryAt(count - 1)->id == model::Id::Save);
  assert(model::entryAt(count) == nullptr);
  assert(model::indexOf(model::Id::WifiPassword) == 4);

  // Navigation clamps at both ends.
  assert(model::nextIndex(0) == 1);
  assert(model::prevIndex(0) == 0);
  assert(model::nextIndex(count - 1) == count - 1);
  assert(model::prevIndex(count - 1) == count - 2);

  // Actions are not editable; values are.
  assert(!model::isEditable(model::indexOf(model::Id::WifiConnect)));
  assert(!model::isEditable(model::indexOf(model::Id::Save)));
  assert(model::isEditable(model::indexOf(model::Id::AnalogPin)));
  assert(model::isEditable(model::indexOf(model::Id::WifiPassword)));

  // Only an exact "eduroam" takes the Enterprise path.
  assert(model::classifySsid("eduroam") == model::Security::Eduroam);
  assert(model::classifySsid("Eduroam") == model::Security::Wpa2);
  assert(model::classifySsid("eduroam-guest") == model::Security::Wpa2);
  assert(model::classifySsid(" eduroam") == model::Security::Wpa2);
  assert(model::classifySsid(nullptr) == model::Security::Wpa2);

  // Credential completeness differs by security type.
  assert(model::credentialsComplete(model::Security::Wpa2, "pw", "", ""));
  assert(!model::credentialsComplete(model::Security::Wpa2, "", "", ""));
  assert(!model::credentialsComplete(model::Security::Eduroam, "pw", "", "user"));
  assert(!model::credentialsComplete(model::Security::Eduroam, "pw", "id", ""));
  assert(model::credentialsComplete(model::Security::Eduroam, "pw", "id", "user"));

  // Character cycling: up advances, down at space erases, capacity is respected.
  char text[8] = "";
  model::cycleTextChar(text, sizeof(text), 0, 1);  // append past terminator
  assert(strcmp(text, "!") == 0);
  model::cycleTextChar(text, sizeof(text), 0, 1);
  assert(strcmp(text, "\"") == 0);
  model::cycleTextChar(text, sizeof(text), 0, -1);
  assert(strcmp(text, "!") == 0);
  model::cycleTextChar(text, sizeof(text), 0, -1);  // return to space
  assert(strcmp(text, " ") == 0);
  model::cycleTextChar(text, sizeof(text), 0, -1);  // space erases
  assert(strcmp(text, "") == 0);
  model::cycleTextChar(text, sizeof(text), 0, -1);  // nothing left to erase
  assert(strcmp(text, "") == 0);

  // Wrapping past the last printable character returns to space.
  char wrap[4] = "~";
  model::cycleTextChar(wrap, sizeof(wrap), 0, 1);
  assert(strcmp(wrap, " ") == 0);

  // A full field refuses to grow.
  char full[3] = "ab";
  model::cycleTextChar(full, sizeof(full), 2, 1);
  assert(strcmp(full, "ab") == 0);

  // Editing in the middle leaves the tail intact; space erases and shifts down.
  char middle[8] = "abc";
  model::cycleTextChar(middle, sizeof(middle), 1, 1);
  assert(strcmp(middle, "acc") == 0);
  model::cycleTextChar(middle, sizeof(middle), 1, -1);
  assert(strcmp(middle, "abc") == 0);
  char gap[8] = "a c";
  model::cycleTextChar(gap, sizeof(gap), 1, -1);
  assert(strcmp(gap, "ac") == 0);

  printf("settings logic OK (%u entries)\n", count);
  return 0;
}

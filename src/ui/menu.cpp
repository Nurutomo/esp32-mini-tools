#include "menu.h"

#include "display_internal.h"

namespace ui {
namespace menu {
namespace {

constexpr Item kItems[] = {
    {"Navigation", 0},
    {"Button monitor", 2},
    {"About", 3},
    {"Bluetooth info", 4},
    {"Settings", 5},
};

}  // namespace

const Item* items() { return kItems; }
uint8_t itemCount() { return sizeof(kItems) / sizeof(kItems[0]); }

void moveUp(uint8_t& selectedItem) {
  if (selectedItem > 0) --selectedItem;
}

void moveDown(uint8_t& selectedItem) {
  if (selectedItem + 1 < itemCount()) ++selectedItem;
}

void render(uint8_t selectedItem) {
  const Item* menuItems = items();
  const uint8_t count = itemCount();
  const char* labels[sizeof(kItems) / sizeof(kItems[0])];
  for (uint8_t i = 0; i < count; ++i) labels[i] = menuItems[i].label;
  display::menu(selectedItem, labels, count);
}

}  // namespace menu
}  // namespace ui

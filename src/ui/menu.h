#pragma once

#include <stdint.h>

namespace ui {
namespace menu {

struct Item {
  const char* label;
  uint8_t screen;
};

const Item* items();
uint8_t itemCount();
void moveUp(uint8_t& selectedItem);
void moveDown(uint8_t& selectedItem);
void render(uint8_t selectedItem);

}  // namespace menu
}  // namespace ui

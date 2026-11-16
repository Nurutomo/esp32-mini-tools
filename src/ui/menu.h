#pragma once

#include <stdint.h>
#include "ui_manager.h"

namespace ui {
namespace menu {

struct Item {
  const char* label;
  Screen screen;
};

const Item* items();
uint8_t itemCount();
void moveUp(uint8_t& selectedItem);
void moveDown(uint8_t& selectedItem);
void render(uint8_t selectedItem);

}  // namespace menu
}  // namespace ui

#pragma once

#include <stdint.h>

namespace ui {
namespace menu {

constexpr uint8_t kItemCount = 5;
void moveUp(uint8_t& selectedItem);
void moveDown(uint8_t& selectedItem);
void render(uint8_t selectedItem);

}  // namespace menu
}  // namespace ui

#include "menu.h"

#include "display_internal.h"

namespace ui {
namespace menu {

void moveUp(uint8_t& selectedItem) {
  if (selectedItem > 0) --selectedItem;
}

void moveDown(uint8_t& selectedItem) {
  if (selectedItem + 1 < kItemCount) ++selectedItem;
}

void render(uint8_t selectedItem) {
  display::menu(selectedItem);
}

}  // namespace menu
}  // namespace ui

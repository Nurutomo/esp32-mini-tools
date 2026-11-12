#include "ui_display.h"

#include "display_manager_internal.h"

namespace ui {
namespace display {

void renderMenu(uint8_t selectedItem) {
  renderMenuScreen(selectedItem, "Main menu", nullptr);
}

void renderButtonMonitor(uint16_t analogValue, uint8_t activePin, const char* label) {
  ::renderButtonMonitor(analogValue, activePin, label);
}

}  // namespace display
}  // namespace ui

#pragma once

#include <stdint.h>

namespace ui {
namespace display {

void renderMenu(uint8_t selectedItem);
void renderButtonMonitor(uint16_t analogValue, uint8_t activePin, const char* buttonLabel);

}  // namespace display
}  // namespace ui

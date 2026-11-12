#pragma once

#include <stdint.h>

namespace ui::display {

void renderButtonMonitor(uint16_t analogValue, uint8_t activePin, const char* label);

}  // namespace ui::display

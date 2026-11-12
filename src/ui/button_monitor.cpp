#include "display_internal.h"

namespace ui::display {

void renderButtonMonitor(uint16_t analogValue, uint8_t activePin, const char* label) {
  buttonMonitor(analogValue, activePin, label);
}

}  // namespace ui::display

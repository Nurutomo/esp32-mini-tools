#pragma once

#include "ui_manager.h"

namespace ui {

void renderScreen(const State& current, uint16_t analogValue, uint8_t activePin,
                  const char* status, const char* message, int speed, uint32_t cycle);

}  // namespace ui

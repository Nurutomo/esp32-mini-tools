#pragma once

#include <stdint.h>

namespace ui {

enum class Button : uint8_t;

Button readAnalogButton(uint16_t analogValue);
void resetAnalogButton();
const char* analogButtonLabel(Button button);
void resetAnalogButton();

}  // namespace ui

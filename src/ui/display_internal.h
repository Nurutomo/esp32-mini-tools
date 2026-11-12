#pragma once

#include <stdint.h>

namespace ui::settings { struct Pins; }
namespace ui::display {
void menu(uint8_t selectedItem);
void buttonMonitor(uint16_t analogValue, uint8_t pin, const char* label);
void text(const char* title, const char* body);
void settings(const ui::settings::Pins& pins, uint8_t selectedOption);
}

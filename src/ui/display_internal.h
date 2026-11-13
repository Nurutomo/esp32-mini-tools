#pragma once

#include <stdint.h>
#include "bluetooth_manager.h"

namespace ui::settings { struct Pins; }
namespace ui::display {
void menu(uint8_t selectedItem, const char* const* items, uint8_t itemCount);
void buttonMonitor(uint16_t analogValue, uint8_t pin, const char* label);
void text(const char* title, const char* body);
void text(const char* title, const char* body, const BluetoothDeviceInfo* devices, uint8_t count,
          uint8_t selectedDevice);
void settings(const ui::settings::Pins& pins, uint8_t selectedOption);
}

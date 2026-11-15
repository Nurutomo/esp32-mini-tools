#pragma once

#include <stdint.h>
#include "bluetooth_manager.h"
#if defined(USE_WIFI)
#include "wifi_manager.h"
#endif

namespace ui::settings { struct Values; }
namespace ui::display {
void menu(uint8_t selectedItem, const char* const* items, uint8_t itemCount);
void buttonMonitor(uint16_t analogValue, uint8_t pin, const char* label);
void text(const char* title, const char* body);
void text(const char* title, const char* body, const BluetoothDeviceInfo* devices, uint8_t count,
          uint8_t selectedDevice);
void settings(const ui::settings::Values& values, uint8_t selectedOption, bool editing);
void keyboard(const char* label, const char* text, bool secret, uint8_t page, uint8_t cell);
#if defined(USE_WIFI)
void wifiScan(uint8_t selected, const WifiNetwork* networks, uint8_t count, bool scanning);
#endif
}

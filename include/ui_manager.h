#pragma once

#include <stdint.h>
#include "ui/settings.h"

namespace ui {

enum class Button : uint8_t {
  None = 0,
  One = 0x01,
  Two = 0x02,
  Three = 0x04,
  TwoAndThree = 0x06,
};

enum class Screen : uint8_t {
  Navigation,
  Menu,
  ButtonMonitor,
  About,
  BluetoothInfo,
  Settings,
};

struct State {
  Screen screen = Screen::Navigation;
  uint8_t selectedItem = 0;
  uint8_t selectedBluetoothDevice = 0;
  uint8_t settingsOption = 0;
  settings::Pins pins{settings::kDefaultAnalogButtonPin, settings::kDefaultBuzzerPin};
};

void begin();
void update(uint16_t analogValue, uint32_t now);
void setAnalogButtonPin(uint8_t pin);
void setBuzzerPin(uint8_t pin);
const State& state();

}  // namespace ui

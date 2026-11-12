#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace ui {

enum class Button : uint8_t {
  None = 0,
  One = 0x01,
  Two = 0x02,
  Three = 0x04,
  TwoAndThree = 0x06,
};

struct State {
  bool menuOpen = false;
  bool buttonMonitorOpen = false;
  uint8_t selectedItem = 0;
};

Button detectButton(uint16_t analogValue);
const char* buttonLabel(Button button);
void begin();
void update(uint16_t analogValue, uint32_t now);
const State& state();

}  // namespace ui

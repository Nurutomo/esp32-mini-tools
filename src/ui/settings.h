#pragma once

#include <stdint.h>
#include "config.h"

namespace ui::settings {

constexpr uint8_t kDefaultAnalogButtonPin = 0;
constexpr uint8_t kDefaultBuzzerPin = 10;

struct Pins {
  uint8_t analogButton;
  uint8_t buzzer;
};

void load(Pins& pins, uint8_t defaultAnalogButton, uint8_t defaultBuzzer);
bool save(const Pins& pins);
void render(const Pins& pins, uint8_t selectedOption);
void edit(Pins& pins, uint8_t selectedOption, int8_t direction, uint8_t maxGpio);
bool validAnalogPin(uint8_t pin, uint8_t maxGpio);
bool validBuzzerPin(uint8_t pin, uint8_t maxGpio, uint8_t analogPin, uint8_t oledSda,
                    uint8_t oledScl, uint8_t ledPin);

}  // namespace ui::settings

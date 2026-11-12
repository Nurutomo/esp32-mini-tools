#pragma once

#include <stdint.h>

namespace ui::buzzer {

void begin(uint8_t pin);
void setPin(uint8_t pin);
void beep(uint16_t frequencyHz, uint16_t durationMs);
void update(uint32_t nowMs);
void stop();
uint8_t pin();

}  // namespace ui::buzzer

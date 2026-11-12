#include "buzzer.h"

#include <Arduino.h>

namespace ui::buzzer {
namespace {

uint8_t gPin = 10;

}

void begin(uint8_t pinNumber) {
  gPin = pinNumber;
  pinMode(gPin, INPUT);
}

void setPin(uint8_t pinNumber) {
  stop();
  gPin = pinNumber;
  pinMode(gPin, INPUT);
}

void beep(uint16_t frequencyHz, uint16_t durationMs) {
  if (frequencyHz == 0 || durationMs == 0) return;
  pinMode(gPin, OUTPUT);
  tone(gPin, frequencyHz, durationMs);
  delay(durationMs);
  stop();
}

void stop() {
  noTone(gPin);
  pinMode(gPin, INPUT);
}

uint8_t pin() {
  return gPin;
}

}  // namespace ui::buzzer

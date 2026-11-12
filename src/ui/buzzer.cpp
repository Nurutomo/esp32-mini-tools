#include "buzzer.h"

#include <Arduino.h>

namespace ui::buzzer {
namespace {

uint8_t gPin = 10;

}

void begin(uint8_t pinNumber) {
  gPin = pinNumber;
  pinMode(gPin, OUTPUT);
  digitalWrite(gPin, HIGH);
}

void setPin(uint8_t pinNumber) {
  digitalWrite(gPin, HIGH);
  gPin = pinNumber;
  pinMode(gPin, OUTPUT);
  digitalWrite(gPin, HIGH);
}

void beep(uint16_t frequencyHz, uint16_t durationMs) {
  if (frequencyHz == 0 || durationMs == 0) return;
  tone(gPin, frequencyHz, durationMs);
}

void stop() {
  noTone(gPin);
  digitalWrite(gPin, HIGH);
}

uint8_t pin() {
  return gPin;
}

}  // namespace ui::buzzer

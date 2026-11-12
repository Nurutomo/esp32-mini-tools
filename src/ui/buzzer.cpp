#include "buzzer.h"

#include <Arduino.h>

namespace ui::buzzer {
namespace {

uint8_t gPin = 10;
uint32_t gStopAtMs = 0;
bool gPlaying = false;

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
  if (gPlaying) noTone(gPin);
  pinMode(gPin, OUTPUT);
  tone(gPin, frequencyHz);
  gStopAtMs = millis() + durationMs;
  gPlaying = true;
}

void update(uint32_t nowMs) {
  if (gPlaying && static_cast<int32_t>(nowMs - gStopAtMs) >= 0) stop();
}

void stop() {
  if (gPlaying) noTone(gPin);
  gPlaying = false;
  pinMode(gPin, INPUT);
}

uint8_t pin() {
  return gPin;
}

}  // namespace ui::buzzer

#include "settings.h"

#include <Preferences.h>
#include "config.h"
#include "display_internal.h"

namespace ui::settings {

void load(Pins& pins, uint8_t defaultAnalogButton, uint8_t defaultBuzzer) {
  Preferences prefs;
  if (!prefs.begin("esp32ui", true)) return;
  pins.analogButton = prefs.getUChar("apin", defaultAnalogButton);
  pins.buzzer = prefs.getUChar("bpin", defaultBuzzer);
  prefs.end();
}

bool validAnalogPin(uint8_t pin, uint8_t maxGpio) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  return pin <= 5 && pin <= maxGpio;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return pin >= 1 && pin <= 20 && pin <= maxGpio;
#else
  return pin <= 39 && pin <= maxGpio;
#endif
}

bool validBuzzerPin(uint8_t pin, uint8_t maxGpio, uint8_t analogPin, uint8_t oledSda,
                    uint8_t oledScl, uint8_t ledPin) {
  return pin <= maxGpio && pin != analogPin && pin != oledSda && pin != oledScl && pin != ledPin;
}

bool save(const Pins& pins) {
  if (!validAnalogPin(pins.analogButton, 48) ||
      !validBuzzerPin(pins.buzzer, 48, pins.analogButton, OLED_SDA_PIN, OLED_SCL_PIN, LED_PIN))
    return false;
  Preferences prefs;
  if (!prefs.begin("esp32ui", false)) return false;
  const bool ok = prefs.putUChar("apin", pins.analogButton) == 1 &&
                  prefs.putUChar("bpin", pins.buzzer) == 1;
  prefs.end();
  return ok;
}

void render(const Pins& pins, uint8_t selectedOption) {
  display::settings(pins, selectedOption);
}

void edit(Pins& pins, uint8_t selectedOption, int8_t direction, uint8_t maxGpio) {
  uint8_t& pin = selectedOption == 0 ? pins.analogButton : pins.buzzer;
  const int16_t next = static_cast<int16_t>(pin) + direction;
  if (next < 0 || next > maxGpio) return;
  const uint8_t candidate = static_cast<uint8_t>(next);
  if (selectedOption == 0 ? validAnalogPin(candidate, maxGpio)
                          : validBuzzerPin(candidate, maxGpio, pins.analogButton, OLED_SDA_PIN,
                                           OLED_SCL_PIN, LED_PIN)) pin = candidate;
}


}  // namespace ui::settings

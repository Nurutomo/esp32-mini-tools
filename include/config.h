#pragma once

#include <stdint.h>

// Change these for the wiring and board revision in use.
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 8
#endif
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 9
#endif
#ifndef OLED_I2C_ADDRESS
// Retained for builds that want to record a preferred address; runtime scans all devices.
#define OLED_I2C_ADDRESS 0x3C
#endif

// LED pin configuration per board
#ifndef LED_PIN
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    #define LED_PIN 8  // ESP32-C3 SuperMini onboard blue LED
  #elif defined(LED_BUILTIN)
    #define LED_PIN LED_BUILTIN
  #else
    #define LED_PIN 2  // Classic ESP32 DevKit / general default
  #endif
#endif

// On some boards (like ESP32-C3 SuperMini), onboard LED is active-low (LOW = ON)
#ifndef LED_ACTIVE_LOW
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    #define LED_ACTIVE_LOW 1
  #else
    #define LED_ACTIVE_LOW 0
  #endif
#endif

static constexpr uint8_t OLED_WIDTH = 128;
static constexpr uint8_t OLED_HEIGHT = 64;

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "navigation_state.h"

// LED control
void turnLedOn();
void turnLedOff();
void prepareForI2C();

// Display lifecycle & rendering
bool initDisplay(uint8_t sdaPin, uint8_t sclPin);
bool isDisplayReady();
uint8_t getDisplayAddress();

// Renders either Navigation HUD or Status Screen depending on active state
void updateDisplay(const char* statusText, const char* btMessage, uint32_t cycle);
void renderMenuScreen(uint8_t selectedItem, const char* pageTitle, const char* pageBody);
void renderButtonMonitor(uint16_t analogValue, uint8_t activePin, const char* buttonLabel);

// Direct render methods
void renderNavHud(const NavigationData& nav);
void renderStatusScreen(const char* statusText, const char* btMessage, int speed, uint32_t cycle);

// Backlight and invert effects
void flashBacklight();
void setDisplayInverted(bool invert);

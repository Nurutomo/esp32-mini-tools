#pragma once

#include <stdint.h>

void renderMenuScreen(uint8_t selectedItem, const char* pageTitle, const char* pageBody);
void renderButtonMonitor(uint16_t analogValue, uint8_t activePin, const char* buttonLabel);

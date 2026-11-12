#pragma once

#include "navigation_state.h"

namespace ui {
namespace navigation {

void render(const NavigationData& nav);
void renderStatus(const char* statusText, const char* message, int speed, uint32_t cycle);

}  // namespace navigation
}  // namespace ui

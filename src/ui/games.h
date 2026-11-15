#pragma once

#include <stdint.h>

namespace ui {
enum class Screen : uint8_t;
namespace games {

void reset(Screen screen);
void jump(Screen screen);
void render(Screen screen);

}  // namespace games
}  // namespace ui

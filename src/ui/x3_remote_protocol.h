#pragma once

#include <stdint.h>

namespace ui::x3remote {

enum class Action : uint8_t { ScreenToggle = 0x00, Mode = 0x01, Shutter = 0x02 };

constexpr uint8_t kFramePrefix[] = {0xFC, 0xEF, 0xFE, 0x86, 0x00, 0x03, 0x01};
constexpr uint8_t kFrameSuffix = 0x00;
constexpr uint8_t kFrameSize = sizeof(kFramePrefix) + 2;

inline bool validAction(Action action) {
  return action == Action::ScreenToggle || action == Action::Mode || action == Action::Shutter;
}

inline void encode(Action action, uint8_t (&frame)[kFrameSize]) {
  if (!validAction(action)) return;
  for (uint8_t i = 0; i < sizeof(kFramePrefix); ++i) frame[i] = kFramePrefix[i];
  frame[sizeof(kFramePrefix)] = static_cast<uint8_t>(action);
  frame[sizeof(kFramePrefix) + 1] = kFrameSuffix;
}

}  // namespace ui::x3remote

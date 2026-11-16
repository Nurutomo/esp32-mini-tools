#include "../src/ui/x3_remote_protocol.h"

#include <cassert>
#include <cstring>

int main() {
  using namespace ui::x3remote;
  uint8_t frame[kFrameSize];
  encode(Action::Shutter, frame);
  const uint8_t expected[] = {0xFC, 0xEF, 0xFE, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00};
  assert(sizeof(expected) == kFrameSize);
  assert(memcmp(frame, expected, kFrameSize) == 0);

  encode(Action::Mode, frame);
  assert(frame[7] == 0x01);
  encode(Action::ScreenToggle, frame);
  assert(frame[7] == 0x00);
  assert(frame[8] == 0x00);
}

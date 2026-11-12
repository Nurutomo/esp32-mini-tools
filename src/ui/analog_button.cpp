#include "analog_button.h"

#include "ui_manager.h"

namespace ui {
namespace {

constexpr uint16_t kButtonTolerance = 100;
constexpr uint32_t kSelectLongPressMs = 800;
constexpr uint32_t kButtonDebounceMs = 120;
uint32_t gLastActionMs = 0;
uint32_t gButtonTwoDownMs = 0;
Button gPreviousButton = Button::None;
bool gButtonTwoLongHandled = false;

bool nearAdcValue(uint16_t value, uint16_t target) {
  const uint16_t delta = value > target ? value - target : target - value;
  return delta <= kButtonTolerance;
}

}  // namespace

Button readAnalogButton(uint16_t value) {
  if (nearAdcValue(value, 4095)) return Button::One;
  if (nearAdcValue(value, 2700)) return Button::TwoAndThree;
  if (nearAdcValue(value, 2270)) return Button::Two;
  if (nearAdcValue(value, 1410)) return Button::Three;
  return Button::None;
}

const char* analogButtonLabel(Button button) {
  switch (button) {
    case Button::One: return "BTN 1";
    case Button::Two: return "BTN 2";
    case Button::Three: return "BTN 3";
    case Button::TwoAndThree: return "BTN 2+3";
    default: return "NONE";
  }
}

void resetAnalogButton() {
  gLastActionMs = 0;
  gButtonTwoDownMs = 0;
  gPreviousButton = Button::None;
  gButtonTwoLongHandled = false;
}

}  // namespace ui

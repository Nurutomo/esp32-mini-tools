#include "ui_manager.h"

#include "display_manager.h"

namespace ui {
namespace {

constexpr uint16_t kButtonTolerance = 180;
constexpr uint32_t kSelectLongPressMs = 800;
constexpr uint32_t kButtonDebounceMs = 180;
constexpr uint8_t kMenuItemCount = 3;

State gState;
uint32_t gLastActionMs = 0;
uint32_t gButtonTwoDownMs = 0;
Button gPreviousButton = Button::None;
bool gButtonTwoLongHandled = false;

bool nearAdcValue(uint16_t value, uint16_t target) {
  const uint16_t delta = value > target ? value - target : target - value;
  return delta <= kButtonTolerance;
}

void select() {
  if (gState.buttonMonitorOpen) {
    gState.buttonMonitorOpen = false;
    gState.menuOpen = true;
  } else if (!gState.menuOpen) {
    gState.menuOpen = true;
  } else if (gState.selectedItem == 0) {
    gState.menuOpen = false;
  } else if (gState.selectedItem == 1) {
    gState.buttonMonitorOpen = true;
  }
}

void back() {
  if (gState.buttonMonitorOpen) {
    gState.buttonMonitorOpen = false;
  } else {
    gState.menuOpen = false;
  }
}

}  // namespace

Button detectButton(uint16_t value) {
  if (nearAdcValue(value, 4095)) return Button::One;
  if (nearAdcValue(value, 2700)) return Button::TwoAndThree;
  if (nearAdcValue(value, 2270)) return Button::Two;
  if (nearAdcValue(value, 1410)) return Button::Three;
  return Button::None;
}

const char* buttonLabel(Button button) {
  switch (button) {
    case Button::One: return "BTN 1";
    case Button::Two: return "BTN 2";
    case Button::Three: return "BTN 3";
    case Button::TwoAndThree: return "BTN 2+3";
    default: return "NONE";
  }
}

void begin() {
  gState = State{};
  gLastActionMs = 0;
  gButtonTwoDownMs = 0;
  gPreviousButton = Button::None;
  gButtonTwoLongHandled = false;
}

void update(uint16_t analogValue, uint32_t now) {
  const Button button = detectButton(analogValue);
  const bool justPressedTwo = button == Button::Two && gPreviousButton != Button::Two;
  if (justPressedTwo) {
    gButtonTwoDownMs = now;
    gButtonTwoLongHandled = false;
  }

  if (button == Button::Two && !gButtonTwoLongHandled &&
      now - gButtonTwoDownMs >= kSelectLongPressMs) {
    back();
    gButtonTwoLongHandled = true;
    gLastActionMs = now;
  }

  if (gPreviousButton == Button::Two && button != Button::Two) {
    if (!gButtonTwoLongHandled && now - gLastActionMs >= kButtonDebounceMs) {
      select();
      gLastActionMs = now;
    }
    gButtonTwoLongHandled = false;
  }

  if (gState.menuOpen && !gState.buttonMonitorOpen &&
      now - gLastActionMs >= kButtonDebounceMs) {
    // ADC 4095 is the top key; ADC 1410 is the bottom key.
    if (button == Button::One && gState.selectedItem > 0) {
      --gState.selectedItem;
      gLastActionMs = now;
    } else if (button == Button::Three && gState.selectedItem + 1 < kMenuItemCount) {
      ++gState.selectedItem;
      gLastActionMs = now;
    }
  }

  gPreviousButton = button;
}

const State& state() {
  return gState;
}

}  // namespace ui

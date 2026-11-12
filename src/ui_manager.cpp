#include "ui_manager.h"

#include "ui/analog_button.h"
#include "ui/menu.h"
#include "ui/buzzer.h"
#include "ui/settings.h"

namespace ui {
namespace {

constexpr uint32_t kSelectLongPressMs = 800;
constexpr uint32_t kButtonDebounceMs = 120;

State gState;
uint32_t gLastActionMs = 0;
uint32_t gButtonTwoDownMs = 0;
Button gPreviousButton = Button::None;
bool gButtonTwoLongHandled = false;

void select() {
  buzzer::beep(2200, 35);
  if (gState.screen == Screen::Settings) {
    if (gState.settingsOption == 0) gState.settingsOption = 1;
    else {
      if (!settings::save(gState.pins)) buzzer::beep(500, 250);
      gState.screen = Screen::Menu;
    }
    return;
  }
  if (gState.screen != Screen::Menu) return;
  switch (gState.selectedItem) {
    case 0: gState.screen = Screen::Navigation; break;
    case 1: gState.screen = Screen::ButtonMonitor; break;
    case 2: gState.screen = Screen::About; break;
    case 3: gState.screen = Screen::BluetoothInfo; break;
    case 4: gState.screen = Screen::Settings; break;
  }
}

void back() {
  buzzer::beep(1400, 70);
  if (gState.screen == Screen::Settings) {
    gState.screen = Screen::Menu;
    return;
  }
  switch (gState.screen) {
    case Screen::Navigation:
    case Screen::ButtonMonitor:
    case Screen::About:
    case Screen::BluetoothInfo:
      gState.screen = Screen::Menu;
      break;
    default:
      gState.screen = Screen::Navigation;
      break;
  }
}

}  // namespace

void begin() {
  gState = State{};
  settings::load(gState.pins, settings::kDefaultAnalogButtonPin, settings::kDefaultBuzzerPin);
  buzzer::begin(gState.pins.buzzer);
  gLastActionMs = 0;
  gButtonTwoDownMs = 0;
  gPreviousButton = Button::None;
  gButtonTwoLongHandled = false;
  resetAnalogButton();
}

void update(uint16_t analogValue, uint32_t now) {
  const Button button = readAnalogButton(analogValue);
  if (button == Button::Two && gPreviousButton != Button::Two) {
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

  if (now - gLastActionMs >= kButtonDebounceMs) {
    if (gState.screen == Screen::Menu && button == Button::One) {
      menu::moveDown(gState.selectedItem);
      gLastActionMs = now;
    } else if (gState.screen == Screen::Menu && button == Button::Three) {
      menu::moveUp(gState.selectedItem);
      gLastActionMs = now;
    } else if (gState.screen == Screen::Settings && button == Button::One) {
      settings::edit(gState.pins, gState.settingsOption, 1, 21);
      buzzer::setPin(gState.pins.buzzer);
      gLastActionMs = now;
    } else if (gState.screen == Screen::Settings && button == Button::Three) {
      if (gState.settingsOption == 0) gState.settingsOption = 1;
      else settings::edit(gState.pins, gState.settingsOption, -1, 21);
      buzzer::setPin(gState.pins.buzzer);
      gLastActionMs = now;
    }
  }

  gPreviousButton = button;
}

void setAnalogButtonPin(uint8_t pin) {
  gState.pins.analogButton = pin;
}

void setBuzzerPin(uint8_t pin) {
  gState.pins.buzzer = pin;
  buzzer::setPin(pin);
}

const State& state() {
  return gState;
}

}  // namespace ui

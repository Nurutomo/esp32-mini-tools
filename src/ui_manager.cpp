#include "ui_manager.h"

#include "ui/analog_button.h"
#include "ui/menu.h"
#include "ui/buzzer.h"
#include "ui/settings.h"
#include "ui/games.h"
#include "bluetooth_manager.h"
#if defined(USE_WIFI)
#include "wifi_manager.h"
#endif

namespace ui {
namespace {

constexpr uint32_t kSelectLongPressMs = 800;
constexpr uint32_t kButtonDebounceMs = 120;

State gState;
uint32_t gLastActionMs = 0;
uint32_t gButtonTwoDownMs = 0;
Button gPreviousButton = Button::None;
bool gButtonTwoLongHandled = false;

void back();

void select() {
  if (gState.screen == Screen::Insta360Remote) {
    buzzer::beep(2200, 35);
    if (!sendX3Action(X3Action::Shutter)) buzzer::beep(500, 250);
    return;
  }
  buzzer::beep(2200, 35);
  if (gState.screen == Screen::Keyboard) {
    using Action = settings::model::keyboard::Action;
    const auto cell = settings::model::keyboard::cellAt(gState.keyboardPage, gState.keyboardCell);
    switch (cell.action) {
      case Action::Insert: settings::textAppend(gState.values, gState.settingsOption, cell.ch); break;
      case Action::Space: settings::textAppend(gState.values, gState.settingsOption, ' '); break;
      case Action::Backspace: settings::textErase(gState.values, gState.settingsOption); break;
      case Action::Page:
        gState.keyboardPage = static_cast<uint8_t>((gState.keyboardPage + 1) %
                                                    settings::model::keyboard::pageCount());
        gState.keyboardCell = settings::model::keyboard::clampCell(gState.keyboardPage,
                                                                    gState.keyboardCell);
        break;
      case Action::Done: gState.screen = Screen::Settings; break;
      case Action::Empty: break;
    }
    return;
  }
  if (gState.screen == Screen::Settings) {
    const settings::model::Entry* entry = settings::model::entryAt(gState.settingsOption);
    if (gState.settingsEditing) {
      gState.settingsEditing = false;
      return;
    }
    if (entry != nullptr && entry->kind == settings::model::Kind::Text) {
      gState.keyboardPage = 0;
      gState.keyboardCell = 0;
      gState.screen = Screen::Keyboard;
      return;
    }
    if (entry == nullptr) return;
    if (entry->id == settings::model::Id::Save) {
      if (!settings::save(gState.values)) {
        buzzer::beep(500, 250);
      } else {
        gState.screen = Screen::Menu;
      }
#if defined(USE_WIFI)
    } else if (entry->id == settings::model::Id::WifiNetwork) {
      gState.wifiSelectedNetwork = 0;
      startWifiScan();
      gState.screen = Screen::WifiScan;
    } else if (entry->id == settings::model::Id::WifiConnect) {
      if (!wifiConnect(gState.values.wifi)) buzzer::beep(500, 250);
    } else if (entry->id == settings::model::Id::WifiSaved) {
      settings::cycleProfile(gState.values);
    } else if (entry->id == settings::model::Id::WifiForget) {
      if (!settings::forgetActive(gState.values)) buzzer::beep(500, 250);
#endif
    } else if (settings::model::isEditable(gState.settingsOption)) {
      gState.settingsEditing = true;
    }
    return;
  }
#if defined(USE_WIFI)
  if (gState.screen == Screen::WifiScan) {
    if (const WifiNetwork* network = wifiScanResult(gState.wifiSelectedNetwork)) {
      if (settings::selectNetwork(gState.values, network->ssid)) {
        const WifiCredentials* profile = settings::activeProfile(gState.values);
        const auto security = settings::model::classifySsid(profile->ssid);
        if (settings::model::credentialsComplete(security, profile->password, profile->identity,
                                                 profile->username)) {
          if (!wifiConnect(gState.values.wifi)) buzzer::beep(500, 250);
        }
      }
    }
    gState.screen = Screen::Settings;
    return;
  }
#endif
  if (gState.screen != Screen::Menu) return;
  const menu::Item* items = menu::items();
  if (gState.selectedItem >= menu::itemCount()) return;
  gState.screen = items[gState.selectedItem].screen;
  if (gState.screen == Screen::BluetoothInfo) {
    gState.selectedBluetoothDevice = 0;
    startBluetoothScan();
  } else if (gState.screen == Screen::DinoGame || gState.screen == Screen::FlappyBird) {
    games::reset(gState.screen);
  } else if (gState.screen == Screen::Settings) {
    gState.settingsOption = 0;
    gState.settingsEditing = false;
  }
}

void back() {
  buzzer::beep(1400, 70);
  if (gState.screen == Screen::Keyboard) {
    gState.screen = Screen::Settings;
    return;
  }
  if (gState.screen == Screen::Settings) {
    gState.settingsEditing = false;
    gState.screen = Screen::Menu;
    return;
  }
#if defined(USE_WIFI)
  if (gState.screen == Screen::WifiScan) {
    gState.screen = Screen::Settings;
    return;
  }
#endif
  if (gState.screen == Screen::BluetoothInfo) {
    gState.screen = Screen::Menu;
    return;
  }
  switch (gState.screen) {
    case Screen::Navigation:
    case Screen::ButtonMonitor:
    case Screen::About:
    case Screen::DinoGame:
    case Screen::FlappyBird:
    case Screen::Insta360Remote:
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
  settings::load(gState.values);
  buzzer::begin(gState.values.pins.buzzer);
  buzzer::setEnabled(gState.values.pins.buzzerEnabled);
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
      if (gState.screen == Screen::DinoGame || gState.screen == Screen::FlappyBird) {
        games::jump(gState.screen);
      } else {
        select();
      }
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
    } else if (gState.screen == Screen::Insta360Remote && button == Button::One &&
               gPreviousButton != Button::One) {
      if (!sendX3Action(X3Action::Mode)) buzzer::beep(500, 250);
      gLastActionMs = now;
    } else if (gState.screen == Screen::Keyboard &&
               (button == Button::One || button == Button::Three)) {
      gState.keyboardCell = settings::model::keyboard::step(
          gState.keyboardPage, gState.keyboardCell, button == Button::One ? 1 : -1);
      gLastActionMs = now;
    } else if (gState.screen == Screen::Settings &&
               (button == Button::One || button == Button::Three)) {
      const int8_t direction = button == Button::One ? 1 : -1;
      if (gState.settingsEditing) {
        settings::edit(gState.values, gState.settingsOption, direction, settings::kMaxGpio);
        buzzer::setPin(gState.values.pins.buzzer);
        buzzer::setEnabled(gState.values.pins.buzzerEnabled);
      } else if (button == Button::One) {
        gState.settingsOption = settings::model::nextIndex(gState.settingsOption);
      } else {
        gState.settingsOption = settings::model::prevIndex(gState.settingsOption);
      }
      gLastActionMs = now;
#if defined(USE_WIFI)
    } else if (gState.screen == Screen::WifiScan &&
               (button == Button::One || button == Button::Three)) {
      const uint8_t count = wifiScanCount();
      if (count > 0) {
        if (button == Button::One && gState.wifiSelectedNetwork + 1 < count)
          ++gState.wifiSelectedNetwork;
        else if (button == Button::Three && gState.wifiSelectedNetwork > 0)
          --gState.wifiSelectedNetwork;
      }
      gLastActionMs = now;
#endif
    } else if (gState.screen == Screen::Navigation &&
               (button == Button::One || button == Button::Three)) {
      gState.speedFocused = !gState.speedFocused;
      buzzer::beep(1800, 25);
      gLastActionMs = now;
    } else if (gState.screen == Screen::BluetoothInfo &&
               (button == Button::One || button == Button::Three)) {
      BluetoothDeviceInfo devices[kMaxNearbyBluetoothDevices];
      const uint8_t count = getNearbyBluetoothDevices(devices, kMaxNearbyBluetoothDevices);
      if (count > 0) {
        if (button == Button::One && gState.selectedBluetoothDevice + 1 < count)
          ++gState.selectedBluetoothDevice;
        else if (button == Button::Three && gState.selectedBluetoothDevice > 0)
          --gState.selectedBluetoothDevice;
      }
      gLastActionMs = now;
    }
  }

  gPreviousButton = button;
}

void setAnalogButtonPin(uint8_t pin) {
  gState.values.pins.analogButton = pin;
}

void setBuzzerPin(uint8_t pin) {
  gState.values.pins.buzzer = pin;
  buzzer::setPin(pin);
}

const State& state() { return gState; }
State& editableState() { return gState; }

}  // namespace ui

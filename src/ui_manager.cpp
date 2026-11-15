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

void select() {
  buzzer::beep(2200, 35);
  if (gState.screen == Screen::Settings) {
    const settings::model::Entry* entry = settings::model::entryAt(gState.settingsOption);
    if (gState.settingsEditing) {
      if (entry != nullptr && entry->kind == settings::model::Kind::Text &&
          settings::advanceText(gState.values, gState.settingsOption)) return;
      gState.settingsEditing = false;
      return;
    }
    if (entry == nullptr) return;
    if (entry->id == settings::model::Id::Save) {
      if (!settings::save(gState.values)) buzzer::beep(500, 250);
      gState.screen = Screen::Menu;
    } else if (entry->id == settings::model::Id::WifiNetwork) {
#if defined(USE_WIFI)
      gState.wifiSelectedNetwork = 0;
      startWifiScan();
      gState.screen = Screen::WifiScan;
#endif
    } else if (entry->id == settings::model::Id::WifiConnect) {
#if defined(USE_WIFI)
      if (!wifiConnect(gState.values.wifi)) buzzer::beep(500, 250);
#endif
    } else if (settings::model::isEditable(gState.settingsOption)) {
      settings::resetTextCursor(gState.settingsOption);
      gState.settingsEditing = true;
    }
    return;
  }
#if defined(USE_WIFI)
  if (gState.screen == Screen::WifiScan) {
    if (const WifiNetwork* network = wifiScanResult(gState.wifiSelectedNetwork)) {
      strncpy(gState.values.wifi.ssid, network->ssid, sizeof(gState.values.wifi.ssid) - 1);
      gState.values.wifi.ssid[sizeof(gState.values.wifi.ssid) - 1] = '\0';
    }
    gState.screen = Screen::Settings;
    return;
  }
#endif
  if (gState.screen != Screen::Menu) return;
  const menu::Item* items = menu::items();
  if (gState.selectedItem >= menu::itemCount()) return;
  gState.screen = static_cast<Screen>(items[gState.selectedItem].screen);
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
  if (gState.screen == Screen::Settings) {
    gState.settingsEditing = false;
    if (!settings::save(gState.values)) buzzer::beep(500, 250);
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

#include "ui.h"

#include "ui/about.h"
#include "ui/analog_button.h"
#include "ui/bluetooth_info.h"
#include "ui/menu.h"
#include "ui/display.h"
#include "ui/navigation.h"
#include "ui/settings.h"
#include "ui/games.h"
#include "bluetooth_manager.h"
#if defined(USE_WIFI)
#include "ui/display_internal.h"
#include "wifi_manager.h"
#endif

namespace ui {

void renderScreen(const State& current, uint16_t adc, uint8_t pin, const char* status,
                  const char* message, int speed, uint32_t cycle) {
  switch (current.screen) {
    case Screen::Menu:
      menu::render(current.selectedItem);
      break;
    case Screen::ButtonMonitor:
      display::renderButtonMonitor(adc, pin, analogButtonLabel(readAnalogButton(adc)));
      break;
    case Screen::About:
      about::render();
      break;
    case Screen::BluetoothInfo: {
      BluetoothDeviceInfo devices[kMaxNearbyBluetoothDevices];
      const uint8_t count = getNearbyBluetoothDevices(devices, kMaxNearbyBluetoothDevices);
      bluetoothInfo::render(status, devices, count, current.selectedBluetoothDevice);
      break;
    }
    case Screen::Settings:
      settings::render(current.values, current.settingsOption, current.settingsEditing);
      break;
#if defined(USE_WIFI)
    case Screen::WifiScan:
      display::wifiScan(current.wifiSelectedNetwork, wifiScanResults(), wifiScanCount(),
                        wifiScanRunning());
      break;
#endif
    case Screen::DinoGame:
    case Screen::FlappyBird:
      games::render(current.screen);
      break;
    case Screen::Navigation:
      if (isNavigationActive()) navigation::render(getNavigationData(), current.speedFocused);
      else navigation::renderStatus(status, message, speed, cycle);
      break;
#if !defined(USE_WIFI)
    case Screen::WifiScan:
      break;
#endif
  }
}

}  // namespace ui

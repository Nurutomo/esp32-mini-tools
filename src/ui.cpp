#include "ui.h"

#include "ui/about.h"
#include "ui/analog_button.h"
#include "ui/bluetooth_info.h"
#include "ui/menu.h"
#include "ui/display.h"
#include "ui/navigation.h"
#include "ui/settings.h"

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
    case Screen::BluetoothInfo:
      bluetoothInfo::render(status);
      break;
    case Screen::Settings:
      settings::render(current.pins, current.settingsOption);
      break;
    case Screen::Navigation:
      if (isNavigationActive()) navigation::render(getNavigationData());
      else navigation::renderStatus(status, message, speed, cycle);
      break;
  }
}

}  // namespace ui

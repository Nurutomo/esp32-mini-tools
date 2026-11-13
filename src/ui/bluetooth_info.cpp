#include "bluetooth_info.h"

#include "display_internal.h"

namespace ui {
namespace bluetoothInfo {

void render(const char* status, const BluetoothDeviceInfo* devices, uint8_t count,
            uint8_t selectedDevice) {
  display::text("Bluetooth", status, devices, count, selectedDevice);
}

}  // namespace bluetoothInfo
}  // namespace ui

#pragma once

#include "bluetooth_manager.h"

namespace ui {
namespace bluetoothInfo {

void render(const char* status, const BluetoothDeviceInfo* devices, uint8_t count,
            uint8_t selectedDevice);

}  // namespace bluetoothInfo
}  // namespace ui

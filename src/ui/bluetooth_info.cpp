#include "bluetooth_info.h"

#include "display_internal.h"

namespace ui {
namespace bluetoothInfo {

void render(const char* status) {
  display::text("Bluetooth", status);
}

}  // namespace bluetoothInfo
}  // namespace ui

#include "about.h"

#include "display_internal.h"

namespace ui {
namespace about {

void render() {
  display::text("About", "ESP32 OLED UI");
}

}  // namespace about
}  // namespace ui

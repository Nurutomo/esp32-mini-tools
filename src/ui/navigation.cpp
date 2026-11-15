#include "navigation.h"

#include "display_manager.h"

namespace ui {
namespace navigation {

void render(const NavigationData& nav, bool speedFocused) {
  renderNavHud(nav, speedFocused);
}

void renderStatus(const char* statusText, const char* message, int speed, uint32_t cycle) {
  renderStatusScreen(statusText, message, speed, cycle);
}

}  // namespace navigation
}  // namespace ui

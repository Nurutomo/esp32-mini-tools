#include "navigation.h"

#include "display_manager.h"

namespace ui {
namespace navigation {

void render(const NavigationData& nav) {
  renderNavHud(nav);
}

void renderStatus(const char* statusText, const char* message, int speed, uint32_t cycle) {
  renderStatusScreen(statusText, message, speed, cycle);
}

}  // namespace navigation
}  // namespace ui

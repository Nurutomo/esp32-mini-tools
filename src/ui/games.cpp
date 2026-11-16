#include "games.h"

#include "display_manager.h"
#include "games_physics.h"
#include "ui_manager.h"

namespace ui::games {
namespace {
constexpr uint32_t kFrameMs = physics::kTickMs;  // match the 25 ms display cadence
constexpr uint32_t kMaxCatchUpTicks = 4;

physics::State gState;
Screen gGame = Screen::DinoGame;
uint32_t gLastFrameMs = 0;

bool isDino() { return gGame == Screen::DinoGame; }

void update(uint32_t now) {
  if (now - gLastFrameMs < kFrameMs) return;
  // Advance whole ticks only; a slow I2C frame must not warp the physics.
  uint32_t ticks = (now - gLastFrameMs) / kFrameMs;
  if (ticks > kMaxCatchUpTicks) {
    ticks = kMaxCatchUpTicks;
    gLastFrameMs = now;  // drop backlog instead of replaying a huge burst
  } else {
    gLastFrameMs += ticks * kFrameMs;
  }
  physics::advance(gState, ticks);
}
}  // namespace

void reset(Screen screen) {
  gGame = screen;
  physics::reset(gState, !isDino());
  gLastFrameMs = millis();
}

void jump(Screen screen) {
  if (gGame != screen) return;
  if (gState.over) {
    reset(screen);
    return;
  }
  physics::jump(gState);
}

void render(Screen screen) {
  if (gGame != screen) reset(screen);
  update(millis());
  renderGameFrame(!isDino(), gState.playerY, gState.obstacleX,
                  physics::ticksToScore(gState.ticks), gState.over);
}

}  // namespace ui::games

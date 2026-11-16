#include "../src/ui/games_physics.h"

#include <cassert>

int main() {
  using namespace ui::games::physics;
  static_assert(kTickMs == 25);

  State dino;
  reset(dino, false);
  assert(dino.playerY == restY());
  const int16_t initialObstacle = dino.obstacleX;
  advance(dino, 40);  // one second
  assert(dino.obstacleX == initialObstacle - kObstaclePixelsPerSecond);
  assert(ticksToScore(dino.ticks) == 5);

  reset(dino, false);
  dino.obstacleX += 200;  // isolate jump arc from collision timing
  jump(dino);
  advance(dino, 8);
  assert(dino.playerY < restY());
  advance(dino, 44);
  assert(dino.playerY == restY() && !dino.over);

  State flappy;
  reset(flappy, true);
  assert(flappy.playerY == startY(true));
  advance(flappy, 32);
  assert(flappy.over);  // falls to floor without flapping
  jump(flappy);
  assert(flappy.over);  // ended games don't restart through physics input

  reset(flappy, true);
  jump(flappy);
  assert(flappy.velocityY == kFlappyFlapVelocity);
  advance(flappy, 2);
  assert(flappy.playerY < startY(true));
}

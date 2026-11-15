#include "../src/ui/games_physics.h"

#include <cassert>

int main() {
  using namespace ui::games::physics;
  State dino;
  reset(dino, false);
  assert(dino.playerY == restY());
  jump(dino);
  advance(dino, 8);
  assert(dino.playerY < restY());
  advance(dino, 40);
  assert(dino.playerY == restY() && !dino.over);

  State flappy;
  reset(flappy, true);
  assert(flappy.playerY < restY());
  advance(flappy, 8);
  assert(flappy.over);  // falls to floor without flapping
  jump(flappy);
  assert(flappy.over);  // ended games don't restart through physics input

  reset(flappy, true);
  jump(flappy);
  assert(flappy.velocityY == -5);
  advance(flappy, 2);
  assert(flappy.playerY < startY(true));
}

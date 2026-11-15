#pragma once

#include <stdint.h>

// Pure game physics, free of Arduino/display dependencies so the host test in
// scripts/test_game_physics.cpp compiles it directly.
namespace ui::games::physics {

constexpr int16_t kScreenWidth = 128;
constexpr int16_t kGroundY = 53;
constexpr int16_t kPlayerX = 18;
constexpr int16_t kPlayerSize = 8;
constexpr int16_t kObstacleW = 7;
constexpr int16_t kObstacleH = 12;
constexpr int16_t kPipeGapTop = kObstacleH + 14;  // also used by the renderer
constexpr int16_t kPipeGapH = 20;

struct State {
  bool flappy = false;
  int16_t obstacleX = 0;
  int16_t playerY = 0;
  int16_t velocityY = 0;  // negative = rising
  uint32_t ticks = 0;
  bool over = false;
};

inline int16_t restY() { return kGroundY - kPlayerSize; }
inline int16_t startY(bool flappy) { return flappy ? 30 : restY(); }
inline uint32_t ticksToScore(uint32_t ticks) { return ticks / 4; }

inline void reset(State& state, bool flappy) {
  state = State{};
  state.flappy = flappy;
  state.obstacleX = kScreenWidth;
  state.playerY = startY(flappy);
}

inline void jump(State& state) {
  if (state.over) return;
  // Dino can only jump off the ground; Flappy flaps any time.
  if (!state.flappy && state.playerY >= restY()) state.velocityY = -7;
  else if (state.flappy) state.velocityY = -5;
}

inline void step(State& state) {
  ++state.ticks;
  state.obstacleX -= 2;

  state.playerY += state.velocityY;
  state.velocityY += 1;
  if (state.playerY >= restY()) {
    state.playerY = restY();
    state.velocityY = 0;
    if (state.flappy) state.over = true;  // Flappy dies on the ground, Dino runs
  }

  if (state.obstacleX + kObstacleW < kPlayerX) {
    state.obstacleX = kScreenWidth;
    return;
  }
  const bool overlapX = state.obstacleX < kPlayerX + kPlayerSize;
  if (!overlapX) return;
  const bool hit = state.flappy
                       ? (state.playerY < kPipeGapTop ||
                          state.playerY + kPlayerSize > kPipeGapTop + kPipeGapH)
                       : state.playerY + kPlayerSize > kGroundY - kObstacleH;
  if (hit) state.over = true;
}

inline void advance(State& state, uint32_t steps) {
  if (state.over) return;
  for (uint32_t i = 0; i < steps; ++i) {
    if (state.over) return;
    step(state);
  }
}

}  // namespace ui::games::physics

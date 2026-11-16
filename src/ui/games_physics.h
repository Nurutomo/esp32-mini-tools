#pragma once

#include <stdint.h>

// Pure game physics, free of Arduino/display dependencies so the host test in
// scripts/test_game_physics.cpp compiles it directly.
//
// One tick is kTickMs. Speeds are pixels/second so raising the tick rate
// changes smoothness, not gameplay pace.
namespace ui::games::physics {

constexpr int16_t kScreenWidth = 128;
constexpr int16_t kGroundY = 53;
constexpr int16_t kPlayerX = 18;
constexpr int16_t kPlayerSize = 8;
constexpr int16_t kObstacleW = 7;
constexpr int16_t kObstacleH = 12;
constexpr int16_t kPipeGapTop = kObstacleH + 14;  // also used by the renderer
constexpr int16_t kPipeGapH = 20;

constexpr uint32_t kTickMs = 25;
constexpr int16_t kObstaclePixelsPerSecond = 70;
constexpr int16_t kGravityPerTick = 10;  // 400 px/s^2 at 25 ms
constexpr int16_t kDinoJumpVelocity = -140;
constexpr int16_t kFlappyFlapVelocity = -100;
constexpr uint32_t kTicksPerScorePoint = 8;

struct State {
  bool flappy = false;
  int16_t obstacleX = 0;
  int16_t playerY = 0;
  int16_t velocityY = 0;  // pixels/second; negative = rising
  int32_t obstacleRemainder = 0;  // milli-pixels not yet applied
  int32_t playerRemainder = 0;
  uint32_t ticks = 0;
  bool over = false;
};

inline int16_t restY() { return kGroundY - kPlayerSize; }
inline int16_t startY(bool flappy) { return flappy ? 30 : restY(); }
inline uint32_t ticksToScore(uint32_t ticks) { return ticks / kTicksPerScorePoint; }

inline void reset(State& state, bool flappy) {
  state = State{};
  state.flappy = flappy;
  state.obstacleX = kScreenWidth;
  state.playerY = startY(flappy);
}

inline void jump(State& state) {
  if (state.over) return;
  // Dino can only jump off the ground; Flappy flaps any time.
  if (!state.flappy && state.playerY >= restY()) state.velocityY = kDinoJumpVelocity;
  else if (state.flappy) state.velocityY = kFlappyFlapVelocity;
}

inline void step(State& state) {
  ++state.ticks;

  state.obstacleRemainder += kObstaclePixelsPerSecond * static_cast<int32_t>(kTickMs);
  state.obstacleX -= static_cast<int16_t>(state.obstacleRemainder / 1000);
  state.obstacleRemainder %= 1000;

  state.playerRemainder += state.velocityY * static_cast<int32_t>(kTickMs);
  state.playerY += static_cast<int16_t>(state.playerRemainder / 1000);
  state.playerRemainder %= 1000;
  state.velocityY += kGravityPerTick;
  if (state.playerY >= restY()) {
    state.playerY = restY();
    state.velocityY = 0;
    state.playerRemainder = 0;
    if (state.flappy) state.over = true;  // Flappy dies on the ground, Dino runs
  }

  if (state.obstacleX + kObstacleW < kPlayerX) {
    state.obstacleX = kScreenWidth;
    state.obstacleRemainder = 0;
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

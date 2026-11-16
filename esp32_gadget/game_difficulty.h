// How hard the Meteoritos run is, as a pure function of elapsed time.
//
// The game used to carry its ramps inline (`spawnIntervalMs()`, `fallSpeed()`),
// which made "harder and faster" a promise nobody could check. One pure function
// means the tuning is host-tested (`tools/game_difficulty_test.cpp`) and the
// fighters follow the same clock as the meteors.
#pragma once

#include <stdint.h>

// One level every ten seconds: fast enough that a good run reaches the busy
// levels inside a minute.
#define GAME_LEVEL_MS 10000u

// The first fighter shows up six seconds in, so a short run still meets one.
#define GAME_ENEMY_FIRST_MS 6000u

struct GameDifficulty {
  uint32_t level;
  uint32_t meteorIntervalMs;  // spawn cadence
  float meteorSpeed;          // rows per second
  uint32_t enemyIntervalMs;   // cadence for fighter ships
  uint32_t enemyFireMs;       // a fighter's firing period
  float enemyBulletSpeed;     // rows per second
  float enemyDriftCols;       // columns per second
  uint8_t enemyHp;            // player shots needed to destroy a fighter
};

static inline uint32_t gameLevelAt(uint32_t elapsedMs) {
  return 1u + elapsedMs / GAME_LEVEL_MS;
}

// base - lv * step, clamped to floor, without ever wrapping a uint32.
static inline uint32_t gameRampDown(uint32_t base, uint32_t step, uint32_t lv,
                                    uint32_t floor) {
  if (step == 0u || base <= floor) return floor;
  if (lv > (base - floor) / step) return floor;
  uint32_t value = base - lv * step;
  return value < floor ? floor : value;
}

static inline float gameRampUp(float base, float step, uint32_t lv,
                               float cap) {
  float value = base + step * (float)lv;
  return value > cap ? cap : value;
}

static inline GameDifficulty gameDifficultyAt(uint32_t elapsedMs) {
  GameDifficulty d;
  d.level = gameLevelAt(elapsedMs);
  uint32_t lv = d.level - 1u;

  // Meteors: denser and faster; the floors keep a long run brutal instead of
  // becoming a wall of pixels.
  d.meteorIntervalMs = gameRampDown(750u, 80u, lv, 160u);
  d.meteorSpeed = gameRampUp(4.0f, 1.0f, lv, 18.0f);

  // Fighters: more often, and their shots come faster and fall faster.
  d.enemyIntervalMs = gameRampDown(12000u, 1200u, lv, 4500u);
  d.enemyFireMs = gameRampDown(2200u, 200u, lv, 700u);
  d.enemyBulletSpeed = gameRampUp(11.0f, 0.8f, lv, 20.0f);
  d.enemyDriftCols = gameRampUp(4.0f, 0.5f, lv, 9.0f);

  // Fighters get tougher: one extra shot every two levels, capped so a long run
  // stays a game instead of a chore.
  uint32_t hp = 1u + lv / 2u;
  d.enemyHp = (uint8_t)(hp > 6u ? 6u : hp);
  return d;
}

// Hit boxes for Meteoritos, in grid cells.
//
// The windows used to live inline in the collision loops, mixed with scoring and
// particle effects, which made the one rule the owner cares about impossible to
// check: only a player bullet destroys a fighter, and both the fighter's shots
// and its body destroy the player. One header, host-tested by
// `tools/game_hits_test.cpp`, keeps that honest.
#pragma once

#include <math.h>

// Player bullet against a fighter. Wide enough (1.4 columns) that a bullet that
// passes a drifting fighter on the same row still counts.
static inline bool gameBulletHitsEnemy(float bx, float by, float ex, float ey) {
  return fabsf(bx - ex) <= 1.4f && fabsf(by - ey) <= 1.0f;
}

// Player bullet against a meteor.
static inline bool gameBulletHitsMeteor(float bx, float by, float mx,
                                        float my) {
  return fabsf(bx - mx) <= 0.9f && fabsf(by - my) <= 1.0f;
}

// A falling meteor against the ship's 3-column body.
static inline bool gameMeteorHitsShip(float mx, float my, float sx, float sy) {
  return fabsf(mx - sx) <= 1.6f && fabsf(my - sy) <= 0.75f;
}

// A fighter's shot against the ship.
static inline bool gameEnemyShotHitsShip(float bx, float by, float sx,
                                         float sy) {
  return fabsf(bx - sx) <= 1.3f && fabsf(by - sy) <= 0.7f;
}

// The fighter's body against the ship: ramming kills, it does not destroy the
// fighter.
static inline bool gameEnemyBodyHitsShip(float ex, float ey, float sx,
                                         float sy) {
  return fabsf(ex - sx) <= 1.8f && fabsf(ey - sy) <= 0.8f;
}

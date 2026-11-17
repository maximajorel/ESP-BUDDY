// Guided calibration wizard, pure and header-only.
//
// The wizard used to live inside the meteor game; it now has its own module so
// CONFIG can own the flow and `tools/steer_wizard_test.cpp` can pin it without a
// board. One button, no timers: confirm the centre, then for each of the four
// directions tilt, come back to centre and confirm. A tap only counts when a
// real gesture was recorded and the board is back at rest (calibration_gate.h);
// four captures go through steerSolve (steer_solve.h), which decides the axes,
// the signs and the baselines.
//
// Directions are captured in the order the owner knows from the game prompt:
// left, right, toward, away. steerSolve wants them as (toward, away, left,
// right), so the solver call reorders them.
#pragma once

#include <math.h>
#include <stdint.h>

#include "calibration_gate.h"
#include "steer_solve.h"

// A gesture this big counts as a real tilt, and this small counts as back at the
// centre. Same numbers the game used; they lived in screen_game.cpp before.
#define STEER_WIZ_TILT_G 0.12f
#define STEER_WIZ_CENTER_G 0.06f
#define STEER_WIZ_DIRECTIONS 4

struct SteerWizard {
  uint8_t step = 0;   // 0 = centre, 1..4 = the direction being captured
  uint8_t captured = 0;
  bool seeded = false;
  float neutral[3] = {0, 0, 0};
  float captures[STEER_WIZ_DIRECTIONS][3] = {{0}, {0}, {0}, {0}};
  float best[3] = {0, 0, 0};  // strongest reading seen in this step
  float bestDev = 0.0f;
  float deviation = 0.0f;     // current distance from the neutral
};

enum SteerWizardTap {
  STEER_WIZ_NONE,        // nothing to report (no sample yet)
  STEER_WIZ_ADVANCED,    // centre confirmed, or a direction captured
  STEER_WIZ_REJECTED,    // no gesture, or still tilted: the owner keeps trying
  STEER_WIZ_SOLVED,      // four captures, a usable solution in *out
  STEER_WIZ_INCOMPLETE,  // four captures, the solver refused; previous stays
};

static inline void steerWizardReset(SteerWizard *w) { *w = SteerWizard(); }

static inline bool steerWizardRecorded(const SteerWizard *w) {
  return w && calExtremeRecorded(w->bestDev, STEER_WIZ_TILT_G);
}

static inline bool steerWizardCentred(const SteerWizard *w) {
  return w && calAtCenter(w->deviation, STEER_WIZ_CENTER_G);
}

// One accelerometer sample. At the centre it is a slow average of the pose the
// owner is holding; in a direction it tracks the strongest deviation, which is
// the reading the solver gets, not the travel.
static inline void steerWizardFeed(SteerWizard *w, const float a[3]) {
  if (!w || !a) return;
  if (w->step == 0) {
    if (!w->seeded) {
      for (int i = 0; i < 3; i++) w->neutral[i] = a[i];
      w->seeded = true;
    } else {
      for (int i = 0; i < 3; i++) {
        w->neutral[i] = 0.8f * w->neutral[i] + 0.2f * a[i];
      }
    }
    return;
  }
  float dev = 0.0f;
  for (int i = 0; i < 3; i++) {
    float d = a[i] - w->neutral[i];
    dev += d * d;
  }
  w->deviation = sqrtf(dev);
  if (w->deviation > w->bestDev) {
    w->bestDev = w->deviation;
    for (int i = 0; i < 3; i++) w->best[i] = a[i];
  }
}

// The tap that closes the current step. On the fourth capture it asks the solver
// for a solution; `out` is only written when one was found.
static inline SteerWizardTap steerWizardTap(SteerWizard *w, SteerSolution *out) {
  if (!w) return STEER_WIZ_NONE;
  if (w->step == 0) {
    if (!w->seeded) return STEER_WIZ_NONE;
    w->step = 1;
    w->captured = 0;
    w->bestDev = 0.0f;
    w->deviation = 0.0f;
    return STEER_WIZ_ADVANCED;
  }
  if (!calCanConfirm(w->bestDev, w->deviation, STEER_WIZ_TILT_G,
                     STEER_WIZ_CENTER_G)) {
    return STEER_WIZ_REJECTED;
  }
  for (int i = 0; i < 3; i++) w->captures[w->captured][i] = w->best[i];
  w->captured++;
  w->bestDev = 0.0f;
  if (w->captured < STEER_WIZ_DIRECTIONS) {
    w->step = (uint8_t)(w->captured + 1);
    return STEER_WIZ_ADVANCED;
  }
  SteerSolution sol =
      steerSolve(w->neutral, w->captures[2], w->captures[3],
                 w->captures[0], w->captures[1]);
  if (!sol.ok) return STEER_WIZ_INCOMPLETE;
  if (out) *out = sol;
  return STEER_WIZ_SOLVED;
}

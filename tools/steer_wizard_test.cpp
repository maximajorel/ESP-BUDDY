// Host-side test for the guided calibration wizard (moved from the game to
// CONFIG).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/steer_wizard_test.cpp -o /tmp/swt && /tmp/swt
//
// Pins the flow the owner follows with one button: confirm the centre, then for
// each direction tilt, return to centre and confirm. A tap only counts with a
// real gesture and the board back at rest; four captures go through steerSolve,
// and a solution that cannot be trusted leaves the previous calibration.

#include <cstdio>
#include <math.h>

#include "steer_wizard.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void feed(SteerWizard &w, float x, float y, float z) {
  float a[3] = {x, y, z};
  steerWizardFeed(&w, a);
}

static void centre(SteerWizard &w) {
  feed(w, 0.0f, 0.0f, 1.0f);
  feed(w, 0.0f, 0.0f, 1.0f);
}

// Tilt to a direction and come back: what the owner does between two taps.
static void gesture(SteerWizard &w, float x, float y, float z) {
  feed(w, x, y, z);
  centre(w);
}

int main() {
  printf("-- a fresh wizard asks for the centre\n");
  {
    SteerWizard w;
    steerWizardReset(&w);
    check("step 0, nothing captured", w.step == 0 && w.captured == 0);
    check("no neutral seeded", !w.seeded);
    SteerSolution sol;
    check("a tap without a sample does nothing",
          steerWizardTap(&w, &sol) == STEER_WIZ_NONE);
    feed(w, 0.0f, 0.0f, 1.0f);
    check("a centre sample seeds the neutral", w.seeded);
    check("confirming the centre advances",
          steerWizardTap(&w, &sol) == STEER_WIZ_ADVANCED && w.step == 1);
  }

  printf("-- a tap only counts with a gesture and a return to centre\n");
  {
    SteerWizard w;
    steerWizardReset(&w);
    centre(w);
    SteerSolution sol;
    steerWizardTap(&w, &sol);  // centre confirmed, step 1
    check("a tap with no tilt is rejected",
          steerWizardTap(&w, &sol) == STEER_WIZ_REJECTED);
    feed(w, 0.5f, 0.0f, 1.0f);  // tilt left, still tilted
    check("a tap while still tilted is rejected",
          steerWizardTap(&w, &sol) == STEER_WIZ_REJECTED);
    centre(w);
    check("back at centre, the tap captures the gesture",
          steerWizardTap(&w, &sol) == STEER_WIZ_ADVANCED && w.captured == 1);
    check("and the capture is the strongest tilt, not the travel",
          fabsf(w.captures[0][0] - 0.5f) < 0.001f && w.step == 2);
  }

  printf("-- four captures solve the axes\n");
  {
    SteerWizard w;
    steerWizardReset(&w);
    centre(w);
    SteerSolution sol;
    steerWizardTap(&w, &sol);  // centre
    gesture(w, -0.5f, 0.0f, 1.0f);  // left
    check("left captured", steerWizardTap(&w, &sol) == STEER_WIZ_ADVANCED);
    gesture(w, 0.5f, 0.0f, 1.0f);  // right
    check("right captured", steerWizardTap(&w, &sol) == STEER_WIZ_ADVANCED);
    gesture(w, 0.0f, -0.5f, 1.0f);  // toward
    check("toward captured", steerWizardTap(&w, &sol) == STEER_WIZ_ADVANCED);
    gesture(w, 0.0f, 0.5f, 1.0f);  // away
    check("the fourth capture solves",
          steerWizardTap(&w, &sol) == STEER_WIZ_SOLVED);
    check("the solver picked the lateral x axis", sol.ok && sol.latAxis == 0);
    check("the solver picked the fore/aft y axis", sol.fwdAxis == 1);
    check("gravity stayed on z", sol.upAxis == 2);
    check("signs follow the gestures", sol.sign == 1 && sol.signY == 1);
    check("baseline is the neutral reading",
          fabsf(sol.baseline) < 0.001f && fabsf(sol.baselineY) < 0.001f);
  }

  printf("-- an unusable set of gestures does not invent axes\n");
  {
    SteerWizard w;
    steerWizardReset(&w);
    // Held leaning, so the neutral is not the upright pose: the solver still
    // has to find the axes and use the baseline it measured.
    feed(w, 0.10f, 0.05f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    SteerSolution sol;
    steerWizardTap(&w, &sol);
    // The centre here is the leaning pose, not the upright one.
    feed(w, -0.40f, 0.05f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    steerWizardTap(&w, &sol);
    feed(w, 0.60f, 0.05f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    steerWizardTap(&w, &sol);
    feed(w, 0.10f, -0.45f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    steerWizardTap(&w, &sol);
    feed(w, 0.10f, 0.55f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    feed(w, 0.10f, 0.05f, 0.98f);
    check("a leaning neutral still solves",
          steerWizardTap(&w, &sol) == STEER_WIZ_SOLVED);
    check("and the baseline is that neutral",
          fabsf(sol.baseline - 0.10f) < 0.01f &&
              fabsf(sol.baselineY - 0.05f) < 0.02f);
  }

  printf("-- a gesture that never moves is incomplete\n");
  {
    SteerWizard w;
    steerWizardReset(&w);
    centre(w);
    SteerSolution sol;
    steerWizardTap(&w, &sol);
    for (int i = 0; i < 4; i++) {
      // Each "direction" is the same nudge on one axis: no left/right pair, so
      // the solver must refuse instead of guessing.
      gesture(w, 0.3f, 0.0f, 1.0f);
      SteerWizardTap r = steerWizardTap(&w, &sol);
      if (i < 3) {
        check("a real tilt is accepted while there are directions left",
              r == STEER_WIZ_ADVANCED);
      } else {
        check("the fourth capture reports an unusable solution",
              r == STEER_WIZ_INCOMPLETE);
      }
    }
  }

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall steer_wizard checks passed\n");
  return 0;
}

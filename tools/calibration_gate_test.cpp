// Host-side test for the calibration confirmation gate.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/calibration_gate_test.cpp -o /tmp/cgt && /tmp/cgt
//
// The guided flow is tap-confirmed: tilt, return to center, tap. A tap that
// arrives without a recorded tilt must not freeze an empty calibration, and a
// tap while still tilted must not capture the way back.

#include <cstdio>

#include "calibration_gate.h"

static int g_failures = 0;

static void check(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-56s got=%d want=%d\n", what, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-56s %d\n", what, (int)got);
  }
}

int main() {
  const float MIN_TILT = 0.12f;
  const float CENTER = 0.06f;

  printf("-- an extreme counts only past the tilt threshold\n");
  check("no tilt at all", calExtremeRecorded(0.0f, MIN_TILT), false);
  check("just below", calExtremeRecorded(0.119f, MIN_TILT), false);
  check("exactly at the threshold", calExtremeRecorded(0.12f, MIN_TILT), true);
  check("well past", calExtremeRecorded(0.40f, MIN_TILT), true);

  printf("-- center means center\n");
  check("dead center", calAtCenter(0.0f, CENTER), true);
  check("exactly at the tolerance", calAtCenter(0.06f, CENTER), true);
  check("slightly off", calAtCenter(0.061f, CENTER), false);
  check("still tilted", calAtCenter(0.30f, CENTER), false);

  printf("-- the tap only counts with both\n");
  check("tilted before, back in the center", calCanConfirm(0.30f, 0.01f, MIN_TILT, CENTER),
        true);
  check("no tilt recorded", calCanConfirm(0.0f, 0.01f, MIN_TILT, CENTER), false);
  check("still away from center", calCanConfirm(0.30f, 0.20f, MIN_TILT, CENTER),
        false);
  check("nothing done", calCanConfirm(0.0f, 0.30f, MIN_TILT, CENTER), false);
  check("right at both edges", calCanConfirm(0.12f, 0.06f, MIN_TILT, CENTER), true);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}

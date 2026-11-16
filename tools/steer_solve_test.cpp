// Host-side test for the guided calibration solver.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/steer_solve_test.cpp -o /tmp/sst && /tmp/sst
//
// The owner tilts the device in four directions. From those four vectors the
// solver has to name both in-plane axes, the gravity axis, and both signs, with
// no prior calibration to lean on. A wrong answer puts the controls sideways or
// backwards, and the panel is the only place to notice, so the rule is pinned
// here against several postures.

#include <cstdio>
#include <cstring>

#include "steer_solve.h"

static int g_failures = 0;

static void checkInt(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-52s got=%d want=%d\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-52s %d\n", what, got);
  }
}

static void checkFloat(const char *what, float got, float want) {
  float d = got - want;
  if (d < 0) d = -d;
  if (d > 0.001f) {
    printf("FAIL %-52s got=%.3f want=%.3f\n", what, (double)got, (double)want);
    g_failures++;
  } else {
    printf("ok   %-52s %.3f\n", what, (double)got);
  }
}

static void expect(const char *label, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-52s got=%d want=%d\n", label, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-52s %d\n", label, (int)got);
  }
}

// Mirror of the game's steering math, used to prove the solution turns the four
// captured gestures into the movement the player expects.
struct Move {
  float lat;  // > 0 means the ship goes right
  float fwd;  // < 0 means the ship goes up
};

static Move movementFor(const SteerSolution &s, const float a[3]) {
  Move m;
  m.lat = (a[s.latAxis] - s.baseline) * (float)s.sign;
  // The game negates the stored signY; keep that in one place here too.
  m.fwd = (a[s.fwdAxis] - s.baselineY) * (float)(-s.signY);
  return m;
}

int main() {
  printf("-- posture A: gravity z, right = +y, away = +x\n");
  {
    const float neutral[3] = {0.0f, 0.0f, 1.0f};
    const float toward[3] = {-0.4f, 0.0f, 1.0f};
    const float away[3] = {0.4f, 0.0f, 1.0f};
    const float left[3] = {0.0f, -0.4f, 1.0f};
    const float right[3] = {0.0f, 0.4f, 1.0f};
    SteerSolution s = steerSolve(neutral, toward, away, left, right);
    expect("solved", s.ok, true);
    checkInt("lateral axis", s.latAxis, 1);
    checkInt("vertical axis", s.fwdAxis, 0);
    checkInt("gravity axis", s.upAxis, 2);
    checkInt("lateral sign", s.sign, 1);
    checkInt("vertical sign", s.signY, 1);
    checkFloat("lateral baseline", s.baseline, 0.0f);
    checkFloat("vertical baseline", s.baselineY, 0.0f);
    Move m = movementFor(s, right);
    expect("tilt right moves the ship right", m.lat > 0.1f, true);
    m = movementFor(s, away);
    expect("tilt away moves the ship up", m.fwd < -0.1f, true);
  }

  printf("-- posture B: same, every sensor axis inverted\n");
  {
    const float neutral[3] = {0.0f, 0.0f, -1.0f};
    const float toward[3] = {0.4f, 0.0f, -1.0f};
    const float away[3] = {-0.4f, 0.0f, -1.0f};
    const float left[3] = {0.0f, 0.4f, -1.0f};
    const float right[3] = {0.0f, -0.4f, -1.0f};
    SteerSolution s = steerSolve(neutral, toward, away, left, right);
    expect("solved", s.ok, true);
    checkInt("lateral axis", s.latAxis, 1);
    checkInt("vertical axis", s.fwdAxis, 0);
    checkInt("gravity axis", s.upAxis, 2);
    checkInt("lateral sign", s.sign, -1);
    checkInt("vertical sign", s.signY, -1);
    Move m = movementFor(s, right);
    expect("tilt right moves the ship right", m.lat > 0.1f, true);
    m = movementFor(s, away);
    expect("tilt away moves the ship up", m.fwd < -0.1f, true);
  }

  printf("-- posture C: gravity on x, the in-plane axes are y and z\n");
  {
    const float neutral[3] = {1.0f, 0.0f, 0.0f};
    const float toward[3] = {1.0f, 0.0f, -0.4f};
    const float away[3] = {1.0f, 0.0f, 0.4f};
    const float left[3] = {1.0f, -0.4f, 0.0f};
    const float right[3] = {1.0f, 0.4f, 0.0f};
    SteerSolution s = steerSolve(neutral, toward, away, left, right);
    expect("solved", s.ok, true);
    checkInt("lateral axis", s.latAxis, 1);
    checkInt("vertical axis", s.fwdAxis, 2);
    checkInt("gravity axis", s.upAxis, 0);
    checkInt("lateral sign", s.sign, 1);
    checkInt("vertical sign", s.signY, 1);
  }

  printf("-- posture D: neutral is not centered, baselines keep the offset\n");
  {
    const float neutral[3] = {0.10f, 0.05f, 0.95f};
    const float toward[3] = {-0.30f, 0.05f, 0.95f};
    const float away[3] = {0.50f, 0.05f, 0.95f};
    const float left[3] = {0.10f, -0.35f, 0.95f};
    const float right[3] = {0.10f, 0.45f, 0.95f};
    SteerSolution s = steerSolve(neutral, toward, away, left, right);
    expect("solved", s.ok, true);
    checkFloat("lateral baseline follows the neutral posture", s.baseline,
               0.05f);
    checkFloat("vertical baseline follows the neutral posture", s.baselineY,
               0.10f);
  }

  printf("-- posture E: a gesture too small to measure is rejected\n");
  {
    const float neutral[3] = {0.0f, 0.0f, 1.0f};
    const float toward[3] = {-0.02f, 0.0f, 1.0f};
    const float away[3] = {0.02f, 0.0f, 1.0f};
    const float left[3] = {0.0f, -0.02f, 1.0f};
    const float right[3] = {0.0f, 0.02f, 1.0f};
    SteerSolution s = steerSolve(neutral, toward, away, left, right);
    expect("rejected", s.ok, false);
  }

  printf("-- posture F: cross-talk on the other axis does not swap the roles\n");
  {
    const float neutral[3] = {0.0f, 0.0f, 1.0f};
    const float toward[3] = {-0.30f, 0.15f, 1.0f};
    const float away[3] = {0.40f, -0.15f, 1.0f};
    const float left[3] = {-0.10f, -0.40f, 1.0f};
    const float right[3] = {0.20f, 0.40f, 1.0f};
    SteerSolution s = steerSolve(neutral, toward, away, left, right);
    expect("solved", s.ok, true);
    checkInt("lateral axis stays on the axis that moves sideways", s.latAxis,
             1);
    checkInt("vertical axis stays on the other one", s.fwdAxis, 0);
  }

  printf("-- a null capture is rejected\n");
  {
    const float neutral[3] = {0.0f, 0.0f, 1.0f};
    SteerSolution s = steerSolve(neutral, nullptr, nullptr, nullptr, nullptr);
    expect("rejected", s.ok, false);
  }

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}

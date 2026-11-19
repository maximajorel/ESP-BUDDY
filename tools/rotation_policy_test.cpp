// Host-side test for the rotation policy.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/rotation_policy_test.cpp -o /tmp/rpt && /tmp/rpt
//
// The policy is the whole behaviour of "the image follows how the gadget is
// laid down": which orientation wins, when a sample is not a decision, how long
// a new decision has to hold, and which axes a 180 degree turn has to invert so
// the game and the face keep pointing where the owner looks.

#include <cstdio>

#include "rotation_policy.h"

static int g_failures = 0;

static void checkInt(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-58s got=%d want=%d\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-58s %d\n", what, got);
  }
}

static void checkFloat(const char *what, float got, float want) {
  float d = got - want;
  if (d < 0) d = -d;
  if (d > 1e-6f) {
    printf("FAIL %-58s got=%f want=%f\n", what, (double)got, (double)want);
    g_failures++;
  } else {
    printf("ok   %-58s %f\n", what, (double)got);
  }
}

// One board sample: gravity along the long axis (X), along the short one (Y) and
// out of the glass (Z). The gadget is landscape, so X is the long axis.
static void sample(float out[3], float x, float y, float z) {
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

static RotationPolicy fresh(uint8_t applied) {
  RotationPolicy p;
  rotPolicyStart(&p, applied);
  return p;
}

int main() {
  float a[3];

  printf("-- the first usable sample teaches the mapping, does not repaint\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    checkInt("applied stays A", rotPolicyFeed(&p, a, 0, 1, 1000), ROT_POLICY_A);
    // The learned reference is the normalized in-plane direction of that sample.
    checkFloat("reference direction x", p.refX, 0.987241f);
    checkFloat("reference direction y", p.refY, 0.159232f);
    checkInt("anchor is A", p.anchor, ROT_POLICY_A);
  }

  printf("-- flat on its back: no in-plane gravity, keep the last image\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.45f, 0.05f, 0.89f);
    rotPolicyFeed(&p, a, 0, 1, 1000);  // learn with A
    sample(a, 0.02f, -0.10f, 0.99f);   // lying flat, no in-plane direction
    checkInt("flat keeps A at once", rotPolicyFeed(&p, a, 0, 1, 1100),
             ROT_POLICY_A);
    checkInt("flat keeps A much later", rotPolicyFeed(&p, a, 0, 1, 60000),
             ROT_POLICY_A);
    checkInt("flat never proposes a candidate", p.candidate, 0);
  }

  printf("-- a steering roll is not a turn: the short axis carries gravity\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    // Rolled 45 degrees about the long axis: X still up, Y now large.
    sample(a, 0.45f, -0.80f, 0.40f);
    checkInt("roll keeps A", rotPolicyFeed(&p, a, 0, 1, 1100), ROT_POLICY_A);
    checkInt("roll much later keeps A", rotPolicyFeed(&p, a, 0, 1, 90000),
             ROT_POLICY_A);
  }

  printf("-- turned 180 degrees: flips, but only after the sample settles\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, -0.60f, 0.05f, 0.78f);
    checkInt("does not flip at once", rotPolicyFeed(&p, a, 0, 1, 1000),
             ROT_POLICY_A);
    checkInt("still not before the settle window",
             rotPolicyFeed(&p, a, 0, 1, 1000 + ROT_SETTLE_MS - 1), ROT_POLICY_A);
    checkInt("flips once the window elapses",
             rotPolicyFeed(&p, a, 0, 1, 1000 + ROT_SETTLE_MS), ROT_POLICY_B);
    checkInt("and stays there", rotPolicyFeed(&p, a, 0, 1, 5000), ROT_POLICY_B);
  }

  printf("-- handling wobble: a short excursion is not a decision\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, -0.60f, 0.05f, 0.78f);
    rotPolicyFeed(&p, a, 0, 1, 1100);  // candidate B appears at 1100
    sample(a, 0.62f, 0.10f, 0.77f);    // and is gone 200 ms later
    checkInt("back to the original image", rotPolicyFeed(&p, a, 0, 1, 1300),
             ROT_POLICY_A);
    sample(a, -0.60f, 0.05f, 0.78f);
    checkInt("a new excursion restarts the window",
             rotPolicyFeed(&p, a, 0, 1, 1400), ROT_POLICY_A);
    checkInt("and still has to settle",
             rotPolicyFeed(&p, a, 0, 1, 1400 + ROT_SETTLE_MS - 1), ROT_POLICY_A);
    checkInt("then flips", rotPolicyFeed(&p, a, 0, 1, 1400 + ROT_SETTLE_MS),
             ROT_POLICY_B);
  }

  printf("-- turning back restores the image the owner started with\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, -0.60f, 0.05f, 0.78f);
    rotPolicyFeed(&p, a, 0, 1, 1100);
    rotPolicyFeed(&p, a, 0, 1, 1100 + ROT_SETTLE_MS);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 3000);
    checkInt("comes back to A", rotPolicyFeed(&p, a, 0, 1, 3000 + ROT_SETTLE_MS),
             ROT_POLICY_A);
  }

  printf("-- the hold does not matter: the short axis may carry gravity too\n");
  {
    // Held landscape, the way the 320x172 image is actually read: the long axis
    // of the glass is horizontal, so gravity lands on the short axis. Which of
    // the two in-plane axes carries gravity must not decide whether the board
    // can be turned 180 degrees.
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.05f, 0.70f, 0.71f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, -0.05f, -0.70f, 0.71f);
    checkInt("a turn is spotted with gravity on the short axis",
             rotPolicyFeed(&p, a, 0, 1, 1000), ROT_POLICY_A);
    checkInt("and it flips once the window elapses",
             rotPolicyFeed(&p, a, 0, 1, 1000 + ROT_SETTLE_MS), ROT_POLICY_B);

    // The same hold, rolled to steer: gravity slides towards the glass axis but
    // the in-plane direction barely moves, so it is not a turn.
    RotationPolicy q = fresh(ROT_POLICY_A);
    sample(a, 0.05f, 0.70f, 0.71f);
    rotPolicyFeed(&q, a, 0, 1, 1000);
    sample(a, 0.10f, 0.50f, 0.86f);
    checkInt("a steering roll in that hold is not a turn",
             rotPolicyFeed(&q, a, 0, 1, 60000), ROT_POLICY_A);
  }

  printf("-- a flat sample in the middle of a turn does not restart the window\n");
  {
    // The gadget passes through "lying flat" while it is being turned, and the
    // sensor drops samples. Restarting the settle window on every such sample
    // is why a real turn sometimes never completed.
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, -0.60f, 0.05f, 0.78f);
    rotPolicyFeed(&p, a, 0, 1, 1000);  // the turn is voted for at 1000
    sample(a, 0.02f, -0.02f, 0.99f);   // one flat sample mid-gesture
    checkInt("a flat sample keeps the current image",
             rotPolicyFeed(&p, a, 0, 1, 1200), ROT_POLICY_A);
    sample(a, -0.60f, 0.05f, 0.78f);
    checkInt("the next vote resumes the same window",
             rotPolicyFeed(&p, a, 0, 1, 1300), ROT_POLICY_A);
    checkInt("and it flips when the window started at the first vote elapses",
             rotPolicyFeed(&p, a, 0, 1, 1000 + ROT_SETTLE_MS), ROT_POLICY_B);
  }

  printf("-- a flat stretch does not stall the window either\n");
  {
    // At the 10 Hz the loop samples with, a real turn keeps dropping readings
    // while the board is edge-on to gravity. As long as the voting samples keep
    // pointing the same way, the window has to keep counting.
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, -0.60f, 0.05f, 0.78f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, 0.02f, -0.02f, 0.99f);
    rotPolicyFeed(&p, a, 0, 1, 1100);  // flat, no vote
    rotPolicyFeed(&p, a, 0, 1, 1200);  // flat, no vote
    sample(a, -0.60f, 0.05f, 0.78f);
    checkInt("a vote after the flat stretch keeps the window",
             rotPolicyFeed(&p, a, 0, 1, 1300), ROT_POLICY_A);
    checkInt("and it flips on the first vote past the deadline",
             rotPolicyFeed(&p, a, 0, 1, 1000 + ROT_SETTLE_MS), ROT_POLICY_B);

    // Turning back to the image on the glass, however, is a real cancellation.
    RotationPolicy q = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&q, a, 0, 1, 1000);
    sample(a, -0.60f, 0.05f, 0.78f);
    rotPolicyFeed(&q, a, 0, 1, 1000);
    sample(a, 0.02f, -0.02f, 0.99f);
    rotPolicyFeed(&q, a, 0, 1, 1100);
    sample(a, 0.62f, 0.10f, 0.77f);
    checkInt("an upright vote after the flat stretch cancels",
             rotPolicyFeed(&q, a, 0, 1, 1200), ROT_POLICY_A);
    sample(a, -0.60f, 0.05f, 0.78f);
    checkInt("a new turn has to settle from scratch",
             rotPolicyFeed(&q, a, 0, 1, 1300), ROT_POLICY_A);
    checkInt("and flips only after its own window",
             rotPolicyFeed(&q, a, 0, 1, 1300 + ROT_SETTLE_MS), ROT_POLICY_B);
  }

  printf("-- a grip change is absorbed so the next turn is still caught\n");
  {
    // The reference is learned from whatever hold the first sample found. The
    // owner then changes grip, or switched the gadget on in another pose: a
    // quarter turn in the plane is neither "upright" nor "turned" for that old
    // reference, and a reference that never moved left every later 180 degree
    // turn undetectable. Holding one direction steadily is not a decision, so it
    // becomes the new reference after a couple of seconds.
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    sample(a, 0.10f, 0.62f, 0.77f);  // the same board, held a quarter turn round
    bool moved = false;
    for (uint32_t t = 1100; t <= 3600; t += 100) {
      if (rotPolicyFeed(&p, a, 0, 1, t) != ROT_POLICY_A) moved = true;
    }
    checkInt("a grip change alone never flips the image", moved ? 1 : 0, 0);
    checkFloat("the reference followed the new hold x", p.refX, 0.159232f);
    checkFloat("the reference followed the new hold y", p.refY, 0.987241f);

    sample(a, -0.10f, -0.62f, 0.77f);
    checkInt("a turn from the new hold is still caught",
             rotPolicyFeed(&p, a, 0, 1, 3700), ROT_POLICY_A);
    checkInt("and flips after its own window",
             rotPolicyFeed(&p, a, 0, 1, 3700 + ROT_SETTLE_MS), ROT_POLICY_B);
  }

  printf("-- pitching the board forward is not a turn\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    // Gravity moves out of the long axis towards the glass axis; the in-plane
    // direction it leaves behind is the same one.
    sample(a, 0.30f, 0.08f, 0.95f);
    checkInt("pitch keeps A", rotPolicyFeed(&p, a, 0, 1, 1100), ROT_POLICY_A);
    sample(a, 0.05f, 0.02f, 0.99f);
    checkInt("pitching almost flat keeps A too",
             rotPolicyFeed(&p, a, 0, 1, 60000), ROT_POLICY_A);
  }

  printf("-- the mapping survives the flip: only the sign that flipped counts\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_A);
    sample(a, 0.62f, 0.10f, 0.77f);
    rotPolicyFeed(&p, a, 0, 1, 1000);
    checkInt("anchor stays A after learning", p.anchor, ROT_POLICY_A);
    checkFloat("reference x unchanged by the flip", p.refX, 0.987241f);
    sample(a, -0.60f, 0.05f, 0.78f);
    rotPolicyFeed(&p, a, 0, 1, 1100);
    rotPolicyFeed(&p, a, 0, 1, 1100 + ROT_SETTLE_MS);
    checkInt("the image did flip", p.applied, ROT_POLICY_B);
    checkFloat("learning never repeats", p.refX, 0.987241f);
  }

  printf("-- a mirrored orientation is never the answer\n");
  {
    // A stored mirrored rotation (0 or 3) is folded onto the real ones.
    RotationPolicy p = fresh(rotPolicySeed(3));
    sample(a, 0.62f, 0.10f, 0.77f);
    int got = rotPolicyFeed(&p, a, 0, 1, 1000);
    checkInt("seeded from whatever the store had", got, ROT_POLICY_A);
    sample(a, -0.60f, 0.05f, 0.78f);
    rotPolicyFeed(&p, a, 0, 1, 1100);
    got = rotPolicyFeed(&p, a, 0, 1, 1100 + ROT_SETTLE_MS);
    checkInt("flip stays unmirrored", got, ROT_POLICY_B);
  }

  printf("-- bad axes are refused instead of guessing\n");
  {
    RotationPolicy p = fresh(ROT_POLICY_B);
    sample(a, -0.62f, 0.10f, 0.77f);
    checkInt("long == short", rotPolicyFeed(&p, a, 1, 1, 1000), ROT_POLICY_B);
    checkInt("axis out of range", rotPolicyFeed(&p, a, 0, 7, 1000), ROT_POLICY_B);
    checkInt("no sample", rotPolicyFeed(&p, nullptr, 0, 1, 1000), ROT_POLICY_B);
  }

  printf("-- a 180 degree turn inverts the two in-plane axes only\n");
  {
    float accel[3] = {0.4f, -0.3f, 0.9f};
    float gyro[3] = {12.0f, -7.0f, 40.0f};
    rotMirrorInPlane(accel, gyro, 1);
    checkFloat("sign +1 keeps accel x", accel[0], 0.4f);
    checkFloat("sign +1 keeps accel y", accel[1], -0.3f);
    checkFloat("sign +1 keeps gyro x", gyro[0], 12.0f);
    checkFloat("sign +1 keeps gyro z", gyro[2], 40.0f);

    accel[0] = 0.4f;
    accel[1] = -0.3f;
    accel[2] = 0.9f;
    gyro[0] = 12.0f;
    gyro[1] = -7.0f;
    gyro[2] = 40.0f;
    rotMirrorInPlane(accel, gyro, -1);
    checkFloat("sign -1 inverts accel x", accel[0], -0.4f);
    checkFloat("sign -1 inverts accel y", accel[1], 0.3f);
    checkFloat("sign -1 leaves accel z alone", accel[2], 0.9f);
    checkFloat("sign -1 inverts gyro x", gyro[0], -12.0f);
    checkFloat("sign -1 inverts gyro y", gyro[1], 7.0f);
    checkFloat("sign -1 leaves the yaw alone", gyro[2], 40.0f);

    // A missing sample must not become a reading.
    rotMirrorInPlane(nullptr, nullptr, -1);
    checkInt("null sample is not a crash", 1, 1);
  }

  printf("-- only the two unmirrored orientations are offered to the owner\n");
  {
    checkInt("mode auto is offered", rotModeCount(), 3);
    checkInt("auto cycles to horizontal", rotModeCycle(ROT_MODE_AUTO),
             ROT_MODE_CW);
    checkInt("horizontal cycles to inverted", rotModeCycle(ROT_MODE_CW),
             ROT_MODE_CCW);
    checkInt("inverted cycles back to auto", rotModeCycle(ROT_MODE_CCW),
             ROT_MODE_AUTO);
    checkInt("a raw console rotation falls back to auto", rotModeCycle(0),
             ROT_MODE_AUTO);
    checkInt("the other mirrored value too", rotModeCycle(3), ROT_MODE_AUTO);
    checkInt("a corrupt stored mode reads as auto", rotModeCycle(9),
             ROT_MODE_AUTO);
    checkInt("mode name auto", rotModeIsConcrete(ROT_MODE_AUTO), 0);
    checkInt("mode name horizontal", rotModeIsConcrete(ROT_MODE_CW), 1);
    checkInt("mode name inverted", rotModeIsConcrete(ROT_MODE_CCW), 1);
    checkInt("a raw pinned rotation is still concrete", rotModeIsConcrete(0), 1);
    checkInt("a corrupt mode is not concrete", rotModeIsConcrete(9), 0);
  }

  printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}

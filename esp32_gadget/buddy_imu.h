// Physical reactions for Sofía's face, pure and header-only.
//
// The bridge knows nothing about the IMU: this is the local layer from task 4
// of Sofía's firmware list. It turns accelerometer samples into a small,
// time-boxed reaction (dizzy on a sustained shake, startle on a sharp hit,
// squint face-down, gravity-compensating gaze). Every reaction expires on its
// own, so a sensor that stops answering can never leave the face stuck.
#pragma once

#include <math.h>
#include <stdint.h>

struct BuddyImu {
  bool haveSample = false;
  float lastMag = 0;
  float shakeMs = 0;       // time above 2.5 g
  uint32_t dizzyUntil = 0;
  uint32_t annoyedUntil = 0;
  uint32_t startleUntil = 0;
  float tiltX = 0;         // filtered gravity direction, -1..1
  float tiltY = 0;
  bool faceDown = false;
  // task 4.6: follow the hand that carries it, for a moment
  float followX = 0;
  uint32_t followUntil = 0;
  // task 4.7: a soft, rhythmic shake is pleasant, not dizzy
  float gyroSign = 0;
  uint32_t crossings = 0;
  uint32_t crossingWindow = 0;
  uint32_t likeUntil = 0;
  // task 4.3: lifted off the table wakes the sleeping face
  bool resting = false;
  float lowAzMs = 0;
  uint32_t wakeUntil = 0;
};

struct BuddyImuPose {
  float gazeX = 0;
  float gazeY = 0;
  float lid = 0;       // extra eyelid on top of the state pose
  bool wide = false;   // startle
  bool dizzy = false;
  bool squint = false;
  bool faceDown = false;
  bool like = false;   // rhythmic shake
  bool wake = false;   // lifted while asleep
};

static inline float buddyImuClamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// One accelerometer sample in g, plus how long since the previous one. Values
// outside 0.2..6 g are ignored: a missing sensor reads zero and must not count
// as a shake. The timers are millis-based and expire without any help.
static inline void buddyImuFeed(BuddyImu *s, const float a[3], const float g[3],
                                uint32_t nowMs, uint32_t dtMs) {
  float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (!(mag > 0.2f && mag < 6.0f)) return;
  if (dtMs > 500) dtMs = 500;

  bool first = !s->haveSample;
  s->haveSample = true;

  // A slow low-pass of gravity; the eyes lean towards the low side.
  s->tiltX = s->tiltX * 0.85f + a[0] * 0.15f;
  s->tiltY = s->tiltY * 0.85f + a[1] * 0.15f;

  // Upside down: the glass is against the table.
  s->faceDown = a[2] < -0.55f;

  // A sustained shake of 2.5 g for 300 ms makes it dizzy; continuing to shake
  // an already dizzy face makes it annoyed instead.
  if (mag > 2.5f) {
    s->shakeMs += (float)dtMs;
  } else {
    s->shakeMs = 0;
  }
  if (s->shakeMs >= 300.0f) {
    if ((int32_t)(s->dizzyUntil - nowMs) > 0) {
      s->annoyedUntil = nowMs + 1500;
    } else {
      s->dizzyUntil = nowMs + 2500;
    }
    s->shakeMs = 0;
  }

  // A sharp single hit is a startle, not a shake: high peak, short duration.
  // A soft hit (setting it down) gives the short version of the same flinch.
  if (!first && mag > 1.8f && s->lastMag < 1.2f) {
    s->startleUntil = nowMs + (mag > 3.2f ? 600 : 300);
  }
  s->lastMag = mag;

  // 4.3 lift: it was resting on the table (+Z) and the Z axis dropped away for
  // 200 ms. Face-down is not a lift; that is the squint below.
  if (a[2] > 0.75f) {
    s->resting = true;
    s->lowAzMs = 0;
  } else if (s->resting && !s->faceDown && a[2] < 0.55f) {
    s->lowAzMs += (float)dtMs;
    if (s->lowAzMs > 200.0f) {
      s->wakeUntil = nowMs + 4000;
      s->resting = false;
    }
  }

  // 4.6 follow: a quick lateral turn of the board pulls the gaze along for
  // half a second. The gyro is in dps, so a gentle move is already >40.
  if (fabsf(g[0]) > 40.0f) {
    s->followX = buddyImuClamp(g[0] / 80.0f, -1.0f, 1.0f);
    s->followUntil = nowMs + 500;
  }

  // 4.7 rhythmic shake: 4 direction changes inside 1.2 s, soft amplitude. The
  // hard shake path above still wins: this only marks pleasure when not dizzy.
  if (fabsf(g[0]) > 50.0f) {
    float sign = g[0] > 0 ? 1.0f : -1.0f;
    if (s->gyroSign != 0 && sign != s->gyroSign) {
      if (s->crossingWindow == 0 || nowMs - s->crossingWindow > 1200) {
        s->crossings = 0;
        s->crossingWindow = nowMs;
      }
      s->crossings++;
      if (s->crossings >= 4 && (int32_t)(s->dizzyUntil - nowMs) <= 0) {
        s->likeUntil = nowMs + 2000;
        s->crossings = 0;
      }
    }
    s->gyroSign = sign;
  }
}

static inline BuddyImuPose buddyImuPose(const BuddyImu *s, uint32_t nowMs) {
  BuddyImuPose p;
  if (!s->haveSample) return p;

  if ((int32_t)(s->startleUntil - nowMs) > 0) p.wide = true;

  if ((int32_t)(s->followUntil - nowMs) > 0) {
    p.gazeX += s->followX * 8.0f;
  }
  if ((int32_t)(s->wakeUntil - nowMs) > 0) p.wake = true;
  if ((int32_t)(s->likeUntil - nowMs) > 0) {
    p.like = true;
    p.lid = 0.3f;
  }

  if ((int32_t)(s->dizzyUntil - nowMs) > 0) {
    p.dizzy = true;
    p.gazeX = sinf((float)nowMs * 0.012f) * 10.0f;
    p.gazeY = cosf((float)nowMs * 0.009f) * 6.0f;
    p.lid = 0.3f;
  } else {
    // Gravity compensation: tilt the board and the eyes lean downhill. It adds
    // to the follow offset instead of replacing it.
    p.gazeX += buddyImuClamp(-s->tiltX * 10.0f, -12.0f, 12.0f);
    p.gazeY += buddyImuClamp(s->tiltY * 8.0f, -8.0f, 8.0f);
  }

  if (s->faceDown) {
    p.squint = true;
    p.faceDown = true;
    p.lid = 0.6f;
  }
  if ((int32_t)(s->annoyedUntil - nowMs) > 0) {
    p.squint = true;
    p.lid = 0.45f;
  }
  return p;
}

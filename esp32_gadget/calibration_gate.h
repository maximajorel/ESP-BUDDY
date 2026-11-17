// When may the owner confirm a calibration step with a tap?
//
// The guided flow asks for a tilt, then a return to center, then a tap. Accepting
// the tap without a recorded extreme would calibrate from nothing; accepting it
// while still tilted would capture the return trip instead of the gesture. Both
// rules are pure so `tools/calibration_gate_test.cpp` pins them.
#pragma once

#include <math.h>

// A real gesture was seen if the largest deviation from center passed the tilt
// threshold at some point during the step.
static inline bool calExtremeRecorded(float bestDeviation, float minTilt) {
  return bestDeviation >= minTilt;
}

// The device is back near the neutral posture it will play from.
static inline bool calAtCenter(float deviation, float centerTolerance) {
  return deviation <= centerTolerance;
}

// The tap only counts when both hold: a gesture was captured and the device is
// back in the center, so the stored sample is the tilt and not the travel.
static inline bool calCanConfirm(float bestDeviation, float deviation,
                                 float minTilt, float centerTolerance) {
  return calExtremeRecorded(bestDeviation, minTilt) &&
         calAtCenter(deviation, centerTolerance);
}

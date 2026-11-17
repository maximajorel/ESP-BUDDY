#include "buddy_led.h"

#include <math.h>

#include "buddy_phase.h"
#include "rgb.h"
#include "screen_buddy.h"

#define CELEBRATE_MS 5000
#define FADE_MS 300

static BuddyPhase s_prev = BUDDY_PHASE_IDLE;
static bool s_havePrev = false;
static bool s_celebrating = false;
static uint32_t s_celebrationStart = 0;
static bool s_fading = false;
static uint32_t s_fadeStart = 0;

static void setScaled(BuddyRgb c, float scale) {
  float s = scale;
  if (s < 0.0f) s = 0.0f;
  if (s > 1.0f) s = 1.0f;
  rgbSetColor((uint8_t)((float)c.r * s), (uint8_t)((float)c.g * s),
              (uint8_t)((float)c.b * s));
}

void buddyLedTick(uint32_t nowMs) {
  BuddyView view;
  buddyGetView(&view);
  if (!view.configured) return;  // without a bridge the RGB card owns the bead

  // The buddy owns the bead while it is configured; a leftover rainbow effect
  // would hide the phase colour.
  rgbSetEffect(RGB_EFFECT_SOLID);

  if (!s_havePrev) {
    s_havePrev = true;
    s_prev = BUDDY_PHASE_IDLE;
  }

  BuddyPhase phase = view.phase;
  if (phase != s_prev) {
    if (buddyPhaseFinished(s_prev, phase)) {
      // The bridge's ok, or the busy -> idle step Desktop/Telegram produce.
      s_celebrating = true;
      s_celebrationStart = nowMs;
      s_fading = false;
    } else if (phase == BUDDY_PHASE_BUSY || phase == BUDDY_PHASE_ASK) {
      // Work wins over the celebration.
      s_celebrating = false;
      s_fading = false;
    }
    s_prev = phase;
  }

  // The face's own mood outranks the phase light: the owner asked for the bead
  // to go red while the board is angry, and to go to a faint ember while it is
  // asleep on the table.
  BuddyMood mood = buddyMood();
  if (mood == BUDDY_MOOD_ANGRY) {
    BuddyRgb red = {255, 30, 20};
    setScaled(red, (nowMs % 500u) < 300u ? 1.0f : 0.3f);
    return;
  }
  if (mood == BUDDY_MOOD_SLEEPING) {
    setScaled(buddyEyesCyan(), 0.07f);
    return;
  }

  if (s_celebrating) {
    uint32_t elapsed = nowMs - s_celebrationStart;
    if (elapsed >= CELEBRATE_MS) {
      s_celebrating = false;
      s_fading = true;
      s_fadeStart = nowMs;
    } else {
      // 2 Hz on/off at 60% brightness, visible from the corner of the eye.
      BuddyRgb green = buddyPhaseDefaultLed(BUDDY_PHASE_OK);
      if ((elapsed % 500) < 250) {
        setScaled(green, 0.6f);
      } else {
        setScaled(green, 0.0f);
      }
      return;
    }
  }

  if (s_fading) {
    uint32_t elapsed = nowMs - s_fadeStart;
    if (elapsed >= FADE_MS) {
      s_fading = false;
    } else {
      float t = (float)elapsed / (float)FADE_MS;
      BuddyRgb green = buddyPhaseDefaultLed(BUDDY_PHASE_OK);
      BuddyRgb cyan = buddyEyesCyan();
      rgbSetColor((uint8_t)((float)green.r + ((float)cyan.r - (float)green.r) * t),
                  (uint8_t)((float)green.g + ((float)cyan.g - (float)green.g) * t),
                  (uint8_t)((float)green.b + ((float)cyan.b - (float)green.b) * t));
      return;
    }
  }

  BuddyRgb color = view.ledValid ? view.led : buddyPhaseDefaultLed(phase);
  switch (phase) {
    case BUDDY_PHASE_ASK: {
      // Slow pulse, 1.5 s, 0.4..1.0.
      float pulse = 0.4f + 0.6f * (0.5f + 0.5f * sinf((float)nowMs * 0.00418879f - 1.5708f));
      setScaled(color, pulse);
      break;
    }
    case BUDDY_PHASE_BUSY:
      rgbSetColor(color.r, color.g, color.b);
      break;
    case BUDDY_PHASE_OK:
      // `ok` lasts ~8 s at the bridge but the blink is already over; stay dim.
      setScaled(buddyEyesCyan(), 0.8f);
      break;
    case BUDDY_PHASE_IDLE:
    default:
      setScaled(color, 0.8f);
      break;
  }
}

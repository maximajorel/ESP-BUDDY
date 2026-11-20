#include "rgb.h"

#include <Adafruit_NeoPixel.h>

#include "rgb_hold.h"

// Channel order: this bead is wired RGB, not the GRB most WS2812 parts use.
// With NEO_GRB a red request lit it green, so the order is pinned here.
static Adafruit_NeoPixel s_pixel(1, RGB_PIN, NEO_RGB + NEO_KHZ800);

static uint8_t s_r = 255, s_g = 170, s_b = 0;
static uint8_t s_level = 64;
static uint8_t s_effect = RGB_EFFECT_SOLID;
static uint16_t s_hue = 0;

static bool s_celebrating = false;
static uint32_t s_celebrateStart = 0;
static uint32_t s_celebrateMs = 0;

static bool s_pulsing = false;
static uint32_t s_pulseStart = 0;
static uint32_t s_pulseMs = 0;
static uint8_t s_pulseR = 0, s_pulseG = 0, s_pulseB = 0;

// Steady acknowledgement colour, above the buddy's per-loop base colour.
static RgbHold s_hold;
static bool s_sleeping = false;

// Casino palette: the golds and crimsons of a felt table and a jackpot lamp.
static const uint8_t CASINO[6][3] = {
    {255, 215, 0},   // gold
    {220, 20, 60},   // crimson
    {255, 248, 231}, // warm white
    {255, 165, 0},   // amber
    {0, 168, 107},   // emerald felt
    {255, 215, 0},   // gold again, so the strobe lands on gold
};

static void wheel(uint16_t pos, uint8_t &r, uint8_t &g, uint8_t &b) {
  pos %= 1536;
  if (pos < 256) {
    r = 255 - pos;
    g = pos;
    b = 0;
  } else if (pos < 512) {
    r = 0;
    g = 511 - pos;
    b = pos - 256;
  } else if (pos < 768) {
    r = 0;
    g = pos - 512;
    b = 767 - pos;
  } else if (pos < 1024) {
    r = pos - 768;
    g = 255;
    b = 0;
  } else if (pos < 1280) {
    r = 255;
    g = 1279 - pos;
    b = 0;
  } else {
    r = 255;
    g = 0;
    b = 1535 - pos;
  }
}

static void show(uint8_t r, uint8_t g, uint8_t b) {
  // Intensity is applied per channel so setBrightness stays out of the maths.
  r = (uint8_t)((uint16_t)r * s_level / 255);
  g = (uint8_t)((uint16_t)g * s_level / 255);
  b = (uint8_t)((uint16_t)b * s_level / 255);
  s_pixel.setPixelColor(0, r, g, b);
  s_pixel.show();
}

void rgbBegin() {
  s_pixel.begin();
  s_pixel.setBrightness(255);
  show(0, 0, 0);
}

void rgbSetColor(uint8_t r, uint8_t g, uint8_t b) {
  s_r = r;
  s_g = g;
  s_b = b;
}

void rgbGetColor(uint8_t &r, uint8_t &g, uint8_t &b) {
  r = s_r;
  g = s_g;
  b = s_b;
}

void rgbSetLevel(uint8_t level) { s_level = level; }
uint8_t rgbLevel() { return s_level; }

void rgbSetEffect(uint8_t effect) {
  s_effect = effect;
  if (effect == RGB_EFFECT_OFF) show(0, 0, 0);
}

uint8_t rgbEffect() { return s_effect; }

void rgbCelebrate(uint32_t durationMs) {
  s_celebrating = true;
  s_celebrateStart = millis();
  s_celebrateMs = durationMs;
}

void rgbPulse(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) {
  s_pulsing = true;
  s_pulseStart = millis();
  s_pulseMs = durationMs;
  s_pulseR = r;
  s_pulseG = g;
  s_pulseB = b;
}

bool rgbBusy() { return s_celebrating || s_pulsing || s_hold.active; }

void rgbHold(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) {
  rgbHoldStart(&s_hold, millis(), r, g, b, durationMs);
}

void rgbTick() {
  uint32_t now = millis();
  if (s_sleeping) return;  // the bead was turned off by rgbSleep()

  if (s_celebrating) {
    uint32_t elapsed = now - s_celebrateStart;
    if (elapsed >= s_celebrateMs) {
      s_celebrating = false;
    } else {
      // Fast colour chase with a strobe on top: 60 ms per palette entry and a
      // 6 Hz envelope reads as a jackpot lamp rather than a colour fade.
      uint16_t step = (uint16_t)(elapsed / 60) % 6;
      uint8_t r = CASINO[step][0], g = CASINO[step][1], b = CASINO[step][2];
      uint8_t env = ((elapsed / 80) % 2) ? 255 : 110;
      show((uint8_t)((uint16_t)r * env / 255), (uint8_t)((uint16_t)g * env / 255),
           (uint8_t)((uint16_t)b * env / 255));
      return;
    }
  }

  // A hold is steady and outranks the pulse on purpose: an acknowledgement must
  // not be mistaken for feedback that fades away.
  if (rgbHoldStep(&s_hold, now)) {
    show(s_hold.r, s_hold.g, s_hold.b);
    return;
  }

  if (s_pulsing) {
    uint32_t elapsed = now - s_pulseStart;
    if (elapsed >= s_pulseMs) {
      s_pulsing = false;
    } else {
      uint8_t fade = (uint8_t)(255 - (elapsed * 255 / s_pulseMs));
      show((uint8_t)((uint16_t)s_pulseR * fade / 255),
           (uint8_t)((uint16_t)s_pulseG * fade / 255),
           (uint8_t)((uint16_t)s_pulseB * fade / 255));
      return;
    }
  }

  if (s_effect == RGB_EFFECT_OFF) return;

  uint8_t r = s_r, g = s_g, b = s_b;
  if (s_effect == RGB_EFFECT_RAINBOW) {
    wheel(s_hue, r, g, b);
    s_hue = (uint16_t)((s_hue + 12) % 1536);
  }
  show(r, g, b);
}

void rgbApplyNow() {
  // An owner command is also a wake: `rgbSleep()` darkens the bead on the screen
  // timer, and a panel write used to change the state while the bead stayed dark,
  // which read as a light that ignored the slider.
  s_sleeping = false;
  if (s_effect == RGB_EFFECT_OFF) {
    show(0, 0, 0);
    return;
  }
  uint8_t r = s_r, g = s_g, b = s_b;
  if (s_effect == RGB_EFFECT_RAINBOW) wheel(s_hue, r, g, b);
  show(r, g, b);
}

void rgbSleep() {
  s_sleeping = true;
  show(0, 0, 0);
}

void rgbWake() { s_sleeping = false; }

bool rgbIsSleeping() { return s_sleeping; }

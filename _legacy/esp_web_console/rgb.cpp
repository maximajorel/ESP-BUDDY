#include "rgb.h"

#include <Adafruit_NeoPixel.h>

// Single WS2812 bead on GPIO38.
//
// Channel order: the bead on this board is RGB, not the GRB most WS2812 strips
// use. With NEO_GRB a red request lit it green, so the order is pinned here.
// If a future board revision swaps it, this is the one line to change.
static Adafruit_NeoPixel s_pixel(1, RGB_PIN, NEO_RGB + NEO_KHZ800);

static uint8_t s_r = 255, s_g = 0, s_b = 0;
static uint8_t s_level = 64;
static uint8_t s_effect = RGB_EFFECT_SOLID;

// Rainbow phase, 0..1535 in 16-bit HSV-like steps (6 segments of 255).
static uint16_t s_hue = 0;

void rgbBegin() {
  s_pixel.begin();
  s_pixel.setBrightness(255);  // intensity is applied per channel instead
  s_pixel.show();
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

void rgbSetLevel(uint8_t level) {
  s_level = level;
}

uint8_t rgbLevel() { return s_level; }

void rgbSetEffect(uint8_t effect) {
  s_effect = effect;
  if (effect == RGB_EFFECT_OFF) {
    s_pixel.setPixelColor(0, 0, 0, 0);
    s_pixel.show();
  }
}

uint8_t rgbEffect() { return s_effect; }

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
  // Second half of the cycle fades red back up so it loops smoothly.
  r = (uint8_t)((uint16_t)r * 255 / 255);
}

void rgbTick() {
  if (s_effect == RGB_EFFECT_OFF) return;

  uint8_t r = s_r, g = s_g, b = s_b;
  if (s_effect == RGB_EFFECT_RAINBOW) {
    wheel(s_hue, r, g, b);
    s_hue = (uint16_t)((s_hue + 12) % 1536);
  }

  // Scale by intensity, 8-bit multiply to stay in range.
  r = (uint8_t)((uint16_t)r * s_level / 255);
  g = (uint8_t)((uint16_t)g * s_level / 255);
  b = (uint8_t)((uint16_t)b * s_level / 255);

  s_pixel.setPixelColor(0, r, g, b);
  s_pixel.show();
}

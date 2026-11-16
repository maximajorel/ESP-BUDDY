// Single WS2812 bead on GPIO38 ("RGB light bead" of the board).
//
// Waveshare's LED strip config uses max_leds = 1 for this board, so a single
// pixel is all there is; the effects below are about colour, not animation
// across a strip.

#pragma once

#include <Arduino.h>

#define RGB_PIN 38
#define RGB_EFFECT_SOLID 0
#define RGB_EFFECT_RAINBOW 1
#define RGB_EFFECT_OFF 2

void rgbBegin();

// Base colour, before the intensity level is applied.
void rgbSetColor(uint8_t r, uint8_t g, uint8_t b);
void rgbGetColor(uint8_t &r, uint8_t &g, uint8_t &b);

// Overall intensity, 0..255.
void rgbSetLevel(uint8_t level);
uint8_t rgbLevel();

void rgbSetEffect(uint8_t effect);
uint8_t rgbEffect();

// Advances animated effects. Call once per loop.
void rgbTick();

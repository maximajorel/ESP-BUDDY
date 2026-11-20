// Single WS2812 bead on GPIO38 - the "RGB light" behind the panel.
//
// The bead is a single pixel, so every effect here is about colour and time
// rather than motion across a strip. Screens use three layers, in priority
// order: a jackpot celebration (slot machine wins), a short pulse (game
// feedback), and the steady base colour.
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

// Casino jackpot burst: gold/amber/crimson strobe for `durationMs`.
void rgbCelebrate(uint32_t durationMs);

// Brief highlight over the base colour, for example a red hit flash.
void rgbPulse(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs);

// A steady colour that outranks the base colour until it expires. Unlike a pulse
// it does not fade and, unlike a celebration, it does not strobe: this is an
// acknowledgement (the study session that just finished), not a light show.
void rgbHold(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs);

// Drives animated layers. Call once per loop.
void rgbTick();

// Paints the configured colour right now and brings the bead back from the sleep
// the screen timer put it in. For an owner command (panel, console): the answer to
// "the bead is at 60%" has to be a bead at 60%, even with the glass off. Not for
// the buddy's per-loop colour, which the next tick paints anyway.
void rgbApplyNow();

// True while a celebration or pulse is running.
bool rgbBusy();

// Turns the bead dark without forgetting the configured colour, level or effect;
// `rgbWake` lets the next tick paint the base colour again.
void rgbSleep();
void rgbWake();
bool rgbIsSleeping();

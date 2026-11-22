// Landscape display driver for the Waveshare ESP32-S3-LCD-1.47B.
//
// The glass is a portrait strip: the ST7789 controller holds 172 columns x 320
// rows, and Waveshare's reference code presents it that way. This gadget runs
// the other way round, so the panel is driven in its native portrait addressing
// and the framebuffer is transposed on the way out.
//
// Keeping the transpose in the driver is deliberate: every screen module then
// draws in plain 320x172 landscape coordinates and never has to know that the
// hardware underneath is sideways.
#pragma once

#include <Arduino.h>

// Logical canvas the whole gadget draws into.
#define DISPLAY_W 320
#define DISPLAY_H 172
#define DISPLAY_PIXELS (DISPLAY_W * DISPLAY_H)

// Panel geometry, used only for the transpose.
#define PANEL_W 172
#define PANEL_H 320
#define PANEL_PIXELS (PANEL_W * PANEL_H)

// The panel's 172 source lines sit centred inside the controller's 240-column
// range, so every column address is shifted by this offset.
#define PANEL_COL_OFFSET 34

// Rotations of the landscape framebuffer onto the portrait panel. Only one of
// each pair renders an unmirrored image; `orient` on the serial console picks
// the right one on real hardware instead of guessing here.
//   0: mirror      1: 90 deg clockwise   2: 90 deg counter-clockwise
//   3: mirror
#define ORIENT_MIRROR 0
#define ORIENT_CW 1
#define ORIENT_CCW 2
#define ORIENT_MIRROR2 3

// Shipped default: rotate the landscape image 90 degrees clockwise.
#define ORIENT_DEFAULT ORIENT_CW

// Returns false when the 110 KB framebuffer cannot be allocated.
bool displayBegin();

// Rotation used by the blit. Defaults to ORIENT_CW.
void displaySetOrientation(uint8_t orientation);
uint8_t displayGetOrientation();

// Transposes `fb` (320x172, RGB565, host byte order) onto the panel and pushes
// it in one SPI transfer.
void displayPush(const uint16_t *fb);

// How long the last pushes took, in microseconds: average, worst case and how
// many frames the numbers cover. This is the honest half of the frame budget and
// the input for deciding the frame rate.
void displayPushStats(uint32_t *avgUs, uint32_t *maxUs, uint32_t *count);
void displayPushStatsReset();

// 0..100 backlight duty. Kept while the panel is asleep, so waking restores it.
void displaySetBacklight(uint8_t percent);
uint8_t displayGetBacklight();

// Glass on/off without touching the configured brightness.
void displaySetAwake(bool awake);
bool displayIsAwake();

// One-time SPI/panel init, exposed for the serial self-test path.
void displayInitPanel();

// Panel self-test: four saturated full-screen colours, 420 ms each, then the
// caller repaints whatever screen it wants. If none of them appear, the fault is
// between the ESP32 and the glass. On demand only (console `colors`, panel
// button), never at boot: an owner turning the device on should see the clock.
void displayColorTest();

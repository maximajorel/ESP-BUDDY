// Particle simulation: falling sand and a shallow-water tank.
//
// Both views live on the same logical grid: the panel is 172x320, and each
// grid cell covers CELL x CELL pixels. That keeps the physics cheap enough to
// run at frame rate while the grains still read as small round dots.
//
// Sand is a falling-sand cellular automaton: a grain tries the neighbours in
// the order of how well they align with the gravity vector, so tilting the
// board makes the whole pile flow that way.
//
// Water is a mass model (each cell holds 0..255 of water) instead of discrete
// grains: water moves along the dominant gravity axis and equalises sideways,
// which is what makes it level out into a flat surface.

#pragma once

#include <Arduino.h>
#include <esp32-hal-psram.h>

#include "Display_ST7789.h"

#define SIM_CELL 2
#define SIM_GW (LCD_WIDTH / SIM_CELL)
#define SIM_GH (LCD_HEIGHT / SIM_CELL)

#define SIM_MODE_SAND 0
#define SIM_MODE_WATER 1

// Allocates the grids in PSRAM. Returns false if PSRAM is unavailable or too
// small, which would leave the simulator with nowhere to keep its state.
bool simBegin();

void simSetMode(int mode);
int simMode();

// Reloads the current mode with a fresh scene.
void simReset();

// One physics step. (gx, gy) is the direction gravity pulls in screen space;
// it does not need to be normalised.
void simStep(float gx, float gy);

// Paints the current mode into a framebuffer sized LCD_WIDTH*LCD_HEIGHT.
// Pixels are written in panel byte order (high byte first).
void simRender(uint16_t *fb);

int simParticles();
float simCentroidX();  // 0..1, left to right
float simCentroidY();  // 0..1, top to bottom

// Prints a coarse ASCII map of the world, so the physics can be inspected
// over the serial port without looking at the panel.
void simDumpAscii();

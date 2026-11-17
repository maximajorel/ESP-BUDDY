// Screen registry and shared chrome.
//
// The gadget is a five-screen carousel behind a picker: a one-second hold opens a
// centred menu with the screen names, a tap moves the highlight and a second hold
// enters. Each screen owns
// its own state and draws into the shared 320x172 framebuffer, returning
// whether it changed anything so an idle clock does not push 30 frames a
// second for nothing.
#pragma once

#include <Arduino.h>

#include "gfx.h"
#include "screen_name.h"

#define SCREEN_CLOCK 0
#define SCREEN_SLOT 1
#define SCREEN_GAME 2
#define SCREEN_NETWORK 3
#define SCREEN_MUSIC 4
#define SCREEN_BUDDY 5
// Appended last for the same reason the music screen was: the screen ids are
// part of the HTTP API, so inserting one earlier would move what an existing
// panel already controls. The stored pick order migrates by appending it.
#define SCREEN_SETTINGS 6
#define SCREEN_COUNT 7

// Shared layout bands, in landscape pixels. There is no header band: the screens
// own the whole canvas from y=0, and only the footer and hold bar are shared.
#define CHROME_FOOTER_Y 158
#define CHROME_HOLD_BAR_Y 168

// Ugly but deliberate palette: the clock must stay black-on-white, so the
// chrome uses the same neutrals the whole gadget reads as one device.
#define CHROME_DIM gfxHex(0x8A8A8A)
#define CHROME_LINE gfxHex(0x333333)

void screensBegin();

// Switches screen, calling the previous screen's exit and the next one's enter.
void screensGoTo(uint8_t index);
void screensNext();
uint8_t screensCurrent();

// Screen picker: hold opens it, tap moves the highlight, hold enters.
bool screensMenuIsOpen();
// Name currently highlighted in the picker, or "" when it is closed.
const char *screensMenuName();
void screensMenuClose();

// Configured pick order: a permutation of screen ids. The order decides what a
// tap walks through in the picker and what "next" means.
void screensGetOrder(uint8_t *out);
bool screensSetOrder(const uint8_t *order);

// Owner-chosen name for one screen. Cleaned with `screenNameClean`, stored in
// NVS and persisted across power cycles; an empty name restores the built-in
// one. The returned name is what was stored, so the API and the panel can keep
// accents while the glass folds them with `textFoldAscii`.
bool screensSetName(uint8_t index, const char *name);
void screensResetNames();

// Draws the active screen and the long-press indicator. Returns true when the
// framebuffer changed and needs to be pushed to the panel. While the panel is
// asleep it draws nothing and reports no change.
bool screensTick(uint32_t nowMs, uint16_t holdPermille);

// True when the current run must not let the panel sleep (playing, calibrating
// or confirming a calibration).
bool gameWantsScreenOn();

// Button gestures forwarded to the active screen.
void screensShortPress();
void screensLongPress();

const char *screenName(uint8_t index);

// --- chrome, used by the screen modules -------------------------------------

// Centred hint at the bottom of the canvas.
void chromeFooter(const char *hint);
// Progress toward the long-press threshold, drawn above the footer.
void chromeHoldBar(uint16_t permille);
// Right-aligned text helper, returns the left edge it used.
int gfxTextRight(int rightX, int y, int scale, const char *text, uint16_t color);

// --- screen modules ---------------------------------------------------------

void clockEnter();
bool clockTick(uint32_t nowMs);
void clockShortPress();

void slotEnter();
bool slotTick(uint32_t nowMs);
void slotShortPress();

// Loads the stored steering calibration. Called from screensBegin so the axes
// are valid before any screen reads them, not only after the game is opened.
void gameInit();
void gameEnter();
bool gameTick(uint32_t nowMs);
void gameShortPress();
// Claims a long press when the calibration prompt is on screen, so a hold there
// confirms the prompt instead of opening the screen menu. Returns false for any
// other state and the menu opens as usual.
bool gameLongPress();

// Steering axes for the dodge game. The board can be held in several postures,
// so which sensor axis reads lateral tilt is configurable at runtime instead of
// guessed. latAxis/upAxis are 0=x, 1=y, 2=z; the remaining axis reads the
// fore/aft tilt. sign and signY flip each direction independently.
void gameSetSteer(uint8_t latAxis, uint8_t upAxis, int8_t sign, int8_t signY,
                  uint8_t gain);
void gameGetSteer(uint8_t *latAxis, uint8_t *upAxis, int8_t *sign,
                  int8_t *signY, uint8_t *gain);
// Discards the learned axes so the next game asks for the calibration gesture.
void gameInvalidateCalibration();
bool gameIsCalibrating();
// Swaps the learned tilt axis for the other in-plane one.
void gameSwapSteerAxis();

// Live game numbers, so the difficulty ramp, the fighters and the score can be
// observed from the console instead of trusted.
void gameGetStats(uint32_t *score, uint32_t *level, int *meteorsAlive,
                  int *bulletsAlive, int *enemiesAlive,
                  int *enemyBulletsAlive);
// Alive fighters, and the column/drift of the n-th one (or -1), for the console.
int gameAliveEnemies();
float gameEnemyColumn(int slot);
float gameEnemyDrift(int slot);
int gameEnemyHp(int slot);
int gameEnemyMaxHp(int slot);
// Test hooks for the console: jump the run's clock and spawn a fighter on
// demand, so the level-scaled HP can be checked without surviving to that level.
void gameSetAgeSeconds(uint32_t seconds);
bool gameSpawnEnemyForTest();
// Test hook for the console: drop the ship at a grid position so an aimed shot
// can be checked without a finger on the tilt sensor.
void gameSetShip(float column, float row);

// Current ship column in grid cells. Exposed so the serial console can prove
// that a tilt actually moves the ship.
float gameShipColumn();

// Current ship row in grid cells. Same reason as the column: the console checks
// that the second tilt axis reaches the vertical movement.
float gameShipRow();

// State name for the console (READY, CONFIRM, CALIBRATING, PLAYING, OVER). The
// calibration prompt has no pixels the probe can read reliably, so the state is
// reported instead.
const char *gameStateName();

// True once the IMU has answered. Set by the sketch during startup.
extern bool g_imuOk;

void netEnter();
bool netTick(uint32_t nowMs);
void netShortPress();

// Spotify "now playing". Appended last on purpose: the screen indices are part
// of the HTTP API, so inserting a screen earlier would move what an existing
// panel already controls.
void musicEnter();
void musicLeave();
bool musicTick(uint32_t nowMs);
void musicShortPress();

// Brightness configuration. The only screen whose gestures are not a plain tap:
// a tap raises the highlighted value and a double tap moves the highlight, while
// the hold still opens the picker so the screen can never trap the owner.
void configEnter();
// Persists a value whose debounced NVS write has not fired yet.
void configLeave();
bool configTick(uint32_t nowMs);
void configShortPress();

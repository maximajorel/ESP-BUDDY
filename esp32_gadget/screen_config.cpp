// CONFIG screen: the two brightnesses the owner can change without a phone.
//
// Both values already existed but only the web panel could reach them: the LCD
// backlight lived behind the panel's slider and the rear bead's intensity behind
// its RGB card. The gadget is supposed to work with no network at all, so the
// two controls live here as well, driven by the same one button.
//
// Two rows, one button:
//   tap        raises the highlighted value one step
//   double tap moves the highlight to the other row
//   hold       opens the screen picker, unchanged, so this screen never traps
//
// Two quick taps therefore change the row, not the value twice. That is the
// trade-off of doing everything with a single button, and the footer says it.
//
// Writes are debounced: each tap applies to the hardware immediately, and NVS is
// only touched once the owner stops tapping. A save per tap would block the loop
// (the display would stutter) and burn flash for a knob turning.
#include <Arduino.h>

#include "config_value.h"
#include "display.h"
#include "rgb.h"
#include "screens.h"
#include "settings.h"
#include "tap_window.h"
#include "theme.h"

#define CFG_ROW_LCD 0
#define CFG_ROW_LED 1

// Once the owner stops tapping for this long, the value is written to NVS. Long
// enough to coalesce a burst of taps, short enough that a power cut right after
// adjusting loses at most the last step.
#define CFG_SAVE_DEBOUNCE_MS 600

// Row geometry. The label is drawn at scale 2, so ten characters take 120 px and
// the bar has to start past that.
#define CFG_CARET_X 6
#define CFG_LABEL_X 22
#define CFG_BAR_X 150
#define CFG_BAR_W 110
#define CFG_BAR_H 12
#define CFG_VALUE_RIGHT 314
#define CFG_ROW0_Y 72
#define CFG_ROW1_Y 108

// Live values in the same percent domain the screen shows. They are seeded from
// the hardware whenever the screen is entered, so a change made from the panel is
// reflected instead of stepped back to a stale number.
static uint8_t s_lcdPct = 90;
static uint8_t s_ledPct = 25;
static uint8_t s_row = CFG_ROW_LCD;

static TapWindow s_taps;
static bool s_needRedraw = true;
static bool s_savePending = false;
static uint32_t s_saveDueAt = 0;

static void seedValues() {
  s_lcdPct = cfgClamp(settingsGet().brightness, CFG_LCD_MIN, CFG_LCD_MAX);
  s_ledPct = cfgLevelToPercent(rgbLevel());
}

static void flushSave() {
  if (!s_savePending) return;
  s_savePending = false;
  settingsSetLcdBrightness(s_lcdPct);
  settingsSetRgbLevel(cfgPercentToLevel(s_ledPct));
}

static void scheduleSave(uint32_t nowMs) {
  s_savePending = true;
  s_saveDueAt = nowMs + CFG_SAVE_DEBOUNCE_MS;
}

static void drawRow(uint8_t row, int y) {
  bool active = row == s_row;
  // The highlighted row is the one the next tap changes, so it is the one the
  // accent marks; the other stays dim and readable.
  uint16_t labelColor = active ? uiAccent() : CHROME_DIM;
  uint16_t fillColor = active ? uiAccent() : CHROME_DIM;
  uint16_t frameColor = active ? uiAccent() : CHROME_LINE;
  uint8_t pct = row == CFG_ROW_LCD ? s_lcdPct : s_ledPct;

  if (active) gfxText(CFG_CARET_X, y, 2, ">", uiAccent());
  gfxText(CFG_LABEL_X, y, 2, row == CFG_ROW_LCD ? "BRILLO LCD" : "BRILLO LED",
          labelColor);

  int barY = y + 1;
  gfxRect(CFG_BAR_X, barY, CFG_BAR_W, CFG_BAR_H, frameColor);
  int filled = (CFG_BAR_W - 4) * pct / 100;
  if (filled > 0) {
    gfxFillRect(CFG_BAR_X + 2, barY + 2, filled, CFG_BAR_H - 4, fillColor);
  }

  char value[6];
  snprintf(value, sizeof(value), "%u%%", (unsigned)pct);
  gfxTextRight(CFG_VALUE_RIGHT, y, 2, value, active ? GFX_WHITE : CHROME_DIM);
}

static void drawScreen() {
  gfxClear(GFX_BLACK);
  gfxTextCentered(24, 2, "CONFIGURACION", GFX_WHITE);
  drawRow(CFG_ROW_LCD, CFG_ROW0_Y);
  drawRow(CFG_ROW_LED, CFG_ROW1_Y);
  chromeFooter("TOCA: CAMBIAR  2 TOCS: FILA");
}

static void applyStep(uint32_t nowMs) {
  if (s_row == CFG_ROW_LCD) {
    s_lcdPct = cfgStepUp(s_lcdPct, CFG_LCD_MIN, CFG_LCD_MAX, CFG_LCD_STEP);
    displaySetBacklight(s_lcdPct);
    Serial.printf("config: brillo lcd %u%%\n", (unsigned)s_lcdPct);
  } else {
    s_ledPct = cfgStepUp(s_ledPct, CFG_LED_MIN, CFG_LED_MAX, CFG_LED_STEP);
    uint8_t level = cfgPercentToLevel(s_ledPct);
    rgbSetLevel(level);
    Serial.printf("config: brillo led %u%% (%u/255)\n", (unsigned)s_ledPct,
                  (unsigned)level);
  }
  scheduleSave(nowMs);
  s_needRedraw = true;
}

void configEnter() {
  s_row = CFG_ROW_LCD;
  tapWindowReset(s_taps);
  // A pending save belongs to the previous visit; write it before reading the
  // live values back, or the read would overwrite what is still unsaved.
  flushSave();
  seedValues();
  s_needRedraw = true;
}

void configLeave() {
  // Leaving is the last chance to persist: the debounce window may not have
  // elapsed yet.
  flushSave();
}

void configShortPress() {
  // The first tap of a double tap does nothing yet: the window has to expire
  // before it is known to be a single tap that raises the value.
  if (tapWindowPress(s_taps, millis(), TAP_DOUBLE_MS) == TAP_DOUBLE) {
    s_row = s_row == CFG_ROW_LCD ? CFG_ROW_LED : CFG_ROW_LCD;
    s_needRedraw = true;
    Serial.printf("config: fila %s\n", s_row == CFG_ROW_LCD ? "lcd" : "led");
  }
}

bool configTick(uint32_t nowMs) {
  if (s_savePending && (int32_t)(nowMs - s_saveDueAt) >= 0) flushSave();

  // The panel can change either value while this screen is open. While nothing
  // of ours is waiting to be written, take what the hardware actually has, so
  // the next tap starts from the number the owner sees elsewhere.
  if (!s_savePending) {
    uint8_t lcd = cfgClamp(settingsGet().brightness, CFG_LCD_MIN, CFG_LCD_MAX);
    uint8_t led = cfgLevelToPercent(rgbLevel());
    if (lcd != s_lcdPct || led != s_ledPct) {
      s_lcdPct = lcd;
      s_ledPct = led;
      s_needRedraw = true;
    }
  }

  // A resolved single tap raises the value. The redraw happens below in the same
  // tick, so the changed bar is what gets pushed to the panel; returning early
  // here would push the previous frame and repaint on the next one.
  TapResult tap = tapWindowPoll(s_taps, nowMs, TAP_DOUBLE_MS);
  if (tap == TAP_SINGLE) applyStep(nowMs);

  if (!s_needRedraw) return false;
  s_needRedraw = false;
  drawScreen();
  return true;
}

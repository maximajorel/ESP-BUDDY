// CONFIG screen: the settings the owner can change without a phone.
//
// Three of them already existed and only the web panel could reach them: the LCD
// backlight, the rear bead's intensity, and the rotation of the image onto the
// panel. The gadget is supposed to work with no network at all, so they live here
// too, driven by the same one button.
//
// Four rows, one button:
//   tap        raises or cycles the highlighted value
//   double tap moves the highlight to the next row
//   hold       opens the screen picker, unchanged, so this screen never traps
//
// Two quick taps therefore change the row, not the value twice. That is the
// trade-off of doing everything with a single button, and the footer says it.
//
// Brightness writes are debounced: each tap applies to the hardware immediately,
// and NVS is only touched once the owner stops tapping. A save per tap would block
// the loop (the display would stutter) and burn flash for a knob turning. The
// rotation mode and the lock are three and two values wide, so they are written
// once per tap, like the panel does.
#include <Arduino.h>

#include "config_value.h"
#include "display.h"
#include "rgb.h"
#include "rotation.h"
#include "screens.h"
#include "settings.h"
#include "tap_window.h"
#include "theme.h"

#define CFG_ROW_LCD 0
#define CFG_ROW_LED 1
#define CFG_ROW_ROT 2
#define CFG_ROW_LOCK 3
#define CFG_ROW_COUNT 4

// Once the owner stops tapping for this long, the value is written to NVS. Long
// enough to coalesce a burst of taps, short enough that a power cut right after
// adjusting loses at most the last step.
#define CFG_SAVE_DEBOUNCE_MS 600

// Row geometry. The label is drawn at scale 2, so ten characters take 120 px and
// the bar has to start past that. Four rows fit between the title and the footer,
// and the discrete rows draw their value inside the same frame the bars use.
#define CFG_CARET_X 6
#define CFG_LABEL_X 22
#define CFG_BAR_X 150
#define CFG_BAR_W 110
#define CFG_BAR_H 12
#define CFG_VALUE_RIGHT 314
#define CFG_ROW0_Y 52
#define CFG_ROW_Y_STEP 26

// Live values in the same percent domain the screen shows. They are seeded from
// the hardware whenever the screen is entered, so a change made from the panel is
// reflected instead of stepped back to a stale number.
static uint8_t s_lcdPct = 90;
static uint8_t s_ledPct = 25;
static uint8_t s_mode = ROT_MODE_AUTO;
static bool s_locked = false;
static uint8_t s_row = CFG_ROW_LCD;

static TapWindow s_taps;
static bool s_needRedraw = true;
static bool s_savePending = false;
static uint32_t s_saveDueAt = 0;

static void seedValues() {
  s_lcdPct = cfgClamp(settingsGet().brightness, CFG_LCD_MIN, CFG_LCD_MAX);
  s_ledPct = cfgLevelToPercent(rgbLevel());
  s_mode = rotationMode();
  s_locked = rotationLocked();
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

static int rowY(uint8_t row) { return CFG_ROW0_Y + row * CFG_ROW_Y_STEP; }

static const char *rowLabel(uint8_t row) {
  switch (row) {
    case CFG_ROW_LCD: return "BRILLO LCD";
    case CFG_ROW_LED: return "BRILLO LED";
    case CFG_ROW_ROT: return "ROTACION";
    default: return "BLOQUEO";
  }
}

// The glass is 320 px wide and the mode names are long, so the discrete rows show
// them at scale 1, centred in the same frame the bars use.
static const char *modeLabel(uint8_t mode) {
  switch (mode) {
    case ROT_MODE_CW: return "HORIZONTAL";
    case ROT_MODE_CCW: return "INVERTIDA";
    case ROT_MODE_AUTO: return "AUTOMATICA";
    default: return "FIJA";  // a raw value only the console can pin
  }
}

static void drawDiscreteRow(uint8_t row, int y, const char *text, bool active) {
  uint16_t frameColor = active ? uiAccent() : CHROME_LINE;
  if (active) gfxText(CFG_CARET_X, y, 2, ">", uiAccent());
  gfxText(CFG_LABEL_X, y, 2, rowLabel(row),
          active ? uiAccent() : CHROME_DIM);

  int barY = y + 1;
  gfxRect(CFG_BAR_X, barY, CFG_BAR_W, CFG_BAR_H, frameColor);
  int textW = gfxTextWidth(1, text);
  gfxText(CFG_BAR_X + (CFG_BAR_W - textW) / 2, barY + 3, 1, text,
          active ? GFX_WHITE : CHROME_DIM);
}

static void drawBarRow(uint8_t row, int y, const char *label, uint8_t pct,
                       bool active) {
  uint16_t labelColor = active ? uiAccent() : CHROME_DIM;
  uint16_t fillColor = active ? uiAccent() : CHROME_DIM;
  uint16_t frameColor = active ? uiAccent() : CHROME_LINE;

  if (active) gfxText(CFG_CARET_X, y, 2, ">", uiAccent());
  gfxText(CFG_LABEL_X, y, 2, label, labelColor);

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
  drawBarRow(CFG_ROW_LCD, rowY(CFG_ROW_LCD), "BRILLO LCD", s_lcdPct,
             s_row == CFG_ROW_LCD);
  drawBarRow(CFG_ROW_LED, rowY(CFG_ROW_LED), "BRILLO LED", s_ledPct,
             s_row == CFG_ROW_LED);
  drawDiscreteRow(CFG_ROW_ROT, rowY(CFG_ROW_ROT), modeLabel(s_mode),
                  s_row == CFG_ROW_ROT);
  drawDiscreteRow(CFG_ROW_LOCK, rowY(CFG_ROW_LOCK), s_locked ? "SI" : "NO",
                  s_row == CFG_ROW_LOCK);
  chromeFooter("TOCA: CAMBIAR  2 TOCS: FILA");
}

static void applyStep(uint32_t nowMs) {
  switch (s_row) {
    case CFG_ROW_LCD: {
      s_lcdPct = cfgStepUp(s_lcdPct, CFG_LCD_MIN, CFG_LCD_MAX, CFG_LCD_STEP);
      displaySetBacklight(s_lcdPct);
      Serial.printf("config: brillo lcd %u%%\n", (unsigned)s_lcdPct);
      scheduleSave(nowMs);
      break;
    }
    case CFG_ROW_LED: {
      s_ledPct = cfgStepUp(s_ledPct, CFG_LED_MIN, CFG_LED_MAX, CFG_LED_STEP);
      uint8_t level = cfgPercentToLevel(s_ledPct);
      rgbSetLevel(level);
      Serial.printf("config: brillo led %u%% (%u/255)\n", (unsigned)s_ledPct,
                    (unsigned)level);
      scheduleSave(nowMs);
      break;
    }
    case CFG_ROW_ROT: {
      s_mode = rotModeCycle(s_mode);
      rotationSetMode(s_mode);
      Serial.printf("config: rotacion %s\n", modeLabel(s_mode));
      break;
    }
    default: {
      s_locked = !s_locked;
      rotationSetLocked(s_locked);
      Serial.printf("config: rotacion %s\n", s_locked ? "bloqueada" : "libre");
      break;
    }
  }
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
  // before it is known to be a single tap that changes the value.
  if (tapWindowPress(s_taps, millis(), TAP_DOUBLE_MS) == TAP_DOUBLE) {
    s_row = (uint8_t)((s_row + 1) % CFG_ROW_COUNT);
    s_needRedraw = true;
    Serial.printf("config: fila %s\n", rowLabel(s_row));
  }
}

bool configTick(uint32_t nowMs) {
  if (s_savePending && (int32_t)(nowMs - s_saveDueAt) >= 0) flushSave();

  // The panel can change any of these while this screen is open. While nothing of
  // ours is waiting to be written, take what the hardware actually has, so the
  // next tap starts from the value the owner sees elsewhere.
  if (!s_savePending) {
    uint8_t lcd = cfgClamp(settingsGet().brightness, CFG_LCD_MIN, CFG_LCD_MAX);
    uint8_t led = cfgLevelToPercent(rgbLevel());
    if (lcd != s_lcdPct || led != s_ledPct) {
      s_lcdPct = lcd;
      s_ledPct = led;
      s_needRedraw = true;
    }
  }
  if (rotationMode() != s_mode || rotationLocked() != s_locked) {
    s_mode = rotationMode();
    s_locked = rotationLocked();
    s_needRedraw = true;
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

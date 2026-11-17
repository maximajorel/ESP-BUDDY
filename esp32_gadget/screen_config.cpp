// CONFIG screen: the settings the owner can change without a phone.
//
// Three of them already existed and only the web panel could reach them: the LCD
// backlight, the rear bead's intensity, and the rotation of the image onto the
// panel. The gadget is supposed to work with no network at all, so they live here
// too, driven by the same one button.
//
// Four rows, six things the button can press:
//
//   double tap   moves the highlight to the next control (wrapping)
//   tap          presses the highlighted control
//   hold         opens the screen picker, unchanged, so this screen never traps
//
// The two brightness rows draw a `−` button, the bar and a `+` button. That is
// what the owner asked for: walking a value up only, with a wrap at the top, made
// lowering it a full trip around the range and one tap too many dropped the LCD
// to its 5% floor, which reads as a screen that turned off. With two buttons each
// step saturates, and the focus says which one the next tap presses.
//
// A single tap still has to wait out the double-tap window before it acts, because
// the first tap of a double is the one that moves the highlight. The window is the
// price of one button; the row the owner is on is never lost to it.
//
// Brightness writes are debounced: each press applies to the hardware immediately,
// and NVS is only touched once the owner stops pressing. A save per press would
// block the loop (the display would stutter) and burn flash for a knob turning.
// The rotation mode and the lock are three and two values wide, so they are written
// once per press, like the panel does.
#include <Arduino.h>

#include "config_value.h"
#include "display.h"
#include "imu.h"
#include "rgb.h"
#include "rotation.h"
#include "screens.h"
#include "settings.h"
#include "steer_solve.h"
#include "steer_wizard.h"
#include "tap_window.h"
#include "theme.h"

#define CFG_ROW_LCD 0
#define CFG_ROW_LED 1
#define CFG_ROW_ROT 2
#define CFG_ROW_LOCK 3
#define CFG_ROW_CAL 4
#define CFG_ROW_COUNT 5

// Focus stops, in the order a double tap walks them. The bar rows contribute two
// each because the two buttons do opposite things.
#define CFG_FOCUS_LCD_MINUS 0
#define CFG_FOCUS_LCD_PLUS 1
#define CFG_FOCUS_LED_MINUS 2
#define CFG_FOCUS_LED_PLUS 3
#define CFG_FOCUS_ROT 4
#define CFG_FOCUS_LOCK 5
#define CFG_FOCUS_CAL 6
#define CFG_FOCUS_COUNT 7
#define CFG_FOCUS_FIRST CFG_FOCUS_LCD_MINUS

// Once the owner stops pressing for this long, the value is written to NVS. Long
// enough to coalesce a burst of presses, short enough that a power cut right after
// adjusting loses at most the last step.
#define CFG_SAVE_DEBOUNCE_MS 600

// Row geometry, in landscape pixels. The label is drawn at scale 2, so ten
// characters take 118 px; the `−` box starts right after it, the bar fills the
// middle, and `+` plus the percentage share the right edge. Four rows fit between
// the title and the footer.
#define CFG_LABEL_X 6
#define CFG_BTN_W 18
#define CFG_BTN_H 16
#define CFG_MINUS_X 128
#define CFG_BAR_X 150
#define CFG_BAR_W 88
#define CFG_BAR_H 12
#define CFG_PLUS_X 242
#define CFG_VALUE_RIGHT 317
#define CFG_ROW0_Y 46
#define CFG_ROW_Y_STEP 22

// Live values in the same percent domain the screen shows. They are seeded from
// the hardware whenever the screen is entered, so a change made from the panel is
// reflected instead of stepped back to a stale number.
static uint8_t s_lcdPct = 90;
static uint8_t s_ledPct = 25;
static uint8_t s_mode = ROT_MODE_AUTO;
static bool s_locked = false;
static uint8_t s_focus = CFG_FOCUS_FIRST;

static TapWindow s_taps;
static bool s_needRedraw = true;
// The guided tilt calibration, moved here from the meteor game. While it runs it
// owns the glass and the button: tap closes a step, hold cancels.
static SteerWizard s_wiz;
static bool s_wizActive = false;
static uint32_t s_wizDrawAt = 0;
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
    case CFG_ROW_LOCK: return "BLOQUEO";
    default: return "CALIBRAR";
  }
}

static const char *focusLabel(uint8_t focus) {
  switch (focus) {
    case CFG_FOCUS_LCD_MINUS: return "BRILLO LCD -";
    case CFG_FOCUS_LCD_PLUS: return "BRILLO LCD +";
    case CFG_FOCUS_LED_MINUS: return "BRILLO LED -";
    case CFG_FOCUS_LED_PLUS: return "BRILLO LED +";
    case CFG_FOCUS_ROT: return "ROTACION";
    case CFG_FOCUS_LOCK: return "BLOQUEO";
    default: return "CALIBRAR";
  }
}

// The glass is 320 px wide and the mode names are long, so the discrete rows show
// them at scale 1, centred in the same frame the bar occupies.
static const char *modeLabel(uint8_t mode) {
  switch (mode) {
    case ROT_MODE_CW: return "HORIZONTAL";
    case ROT_MODE_CCW: return "INVERTIDA";
    case ROT_MODE_AUTO: return "AUTOMATICA";
    default: return "FIJA";  // a raw value only the console can pin
  }
}

// One `−`/`+` box. The focused one is filled with the accent and its glyph goes
// black, which reads from across a table; the other keeps a dim outline so the
// owner sees that both exist.
static void drawButton(int x, int y, const char *glyph, bool focused) {
  if (focused) gfxFillRect(x, y, CFG_BTN_W, CFG_BTN_H, uiAccent());
  gfxRect(x, y, CFG_BTN_W, CFG_BTN_H, focused ? uiAccent() : CHROME_LINE);
  uint16_t color = focused ? GFX_BLACK : CHROME_DIM;
  int w = gfxTextWidth(2, glyph);
  gfxText(x + (CFG_BTN_W - w) / 2, y + (CFG_BTN_H - 7 * 2) / 2, 2, glyph,
          color);
}

static void drawBarRow(uint8_t row, int y, const char *label, uint8_t pct,
                       uint8_t focus) {
  bool minusFocused = focus == (uint8_t)(row == CFG_ROW_LCD
                                             ? CFG_FOCUS_LCD_MINUS
                                             : CFG_FOCUS_LED_MINUS);
  bool plusFocused = focus == (uint8_t)(row == CFG_ROW_LCD ? CFG_FOCUS_LCD_PLUS
                                                           : CFG_FOCUS_LED_PLUS);
  bool rowFocused = minusFocused || plusFocused;
  uint16_t frameColor = rowFocused ? uiAccent() : CHROME_LINE;
  uint16_t fillColor = rowFocused ? uiAccent() : CHROME_DIM;

  gfxText(CFG_LABEL_X, y, 2, label, rowFocused ? uiAccent() : CHROME_DIM);

  drawButton(CFG_MINUS_X, y, "-", minusFocused);

  int barY = y + 2;
  gfxRect(CFG_BAR_X, barY, CFG_BAR_W, CFG_BAR_H, frameColor);
  int filled = (CFG_BAR_W - 4) * pct / 100;
  if (filled > 0) {
    gfxFillRect(CFG_BAR_X + 2, barY + 2, filled, CFG_BAR_H - 4, fillColor);
  }

  drawButton(CFG_PLUS_X, y, "+", plusFocused);

  char value[6];
  snprintf(value, sizeof(value), "%u%%", (unsigned)pct);
  gfxTextRight(CFG_VALUE_RIGHT, y, 2, value, rowFocused ? GFX_WHITE : CHROME_DIM);
}

static uint8_t discreteFocus(uint8_t row) {
  switch (row) {
    case CFG_ROW_ROT: return CFG_FOCUS_ROT;
    case CFG_ROW_LOCK: return CFG_FOCUS_LOCK;
    default: return CFG_FOCUS_CAL;
  }
}

static void drawDiscreteRow(uint8_t row, int y, const char *text, uint8_t focus) {
  bool focused = focus == discreteFocus(row);
  uint16_t frameColor = focused ? uiAccent() : CHROME_LINE;

  gfxText(CFG_LABEL_X, y, 2, rowLabel(row), focused ? uiAccent() : CHROME_DIM);

  int boxX = CFG_MINUS_X;
  int boxW = CFG_PLUS_X + CFG_BTN_W - CFG_MINUS_X;
  gfxRect(boxX, y, boxW, CFG_BTN_H, frameColor);
  int textW = gfxTextWidth(1, text);
  gfxText(boxX + (boxW - textW) / 2, y + 4, 1, text,
          focused ? GFX_WHITE : CHROME_DIM);
}

static void drawScreen() {
  gfxClear(GFX_BLACK);
  gfxTextCentered(18, 2, "CONFIGURACION", GFX_WHITE);
  drawBarRow(CFG_ROW_LCD, rowY(CFG_ROW_LCD), "BRILLO LCD", s_lcdPct, s_focus);
  drawBarRow(CFG_ROW_LED, rowY(CFG_ROW_LED), "BRILLO LED", s_ledPct, s_focus);
  drawDiscreteRow(CFG_ROW_ROT, rowY(CFG_ROW_ROT), modeLabel(s_mode), s_focus);
  drawDiscreteRow(CFG_ROW_LOCK, rowY(CFG_ROW_LOCK), s_locked ? "SI" : "NO",
                  s_focus);
  drawDiscreteRow(CFG_ROW_CAL, rowY(CFG_ROW_CAL),
                  settingsGetSteerCalibration().valid ? "HECHA" : "HACER",
                  s_focus);
  chromeFooter("TOCA: CAMBIAR  2 TOCS: SIGUIENTE");
}

// --- the calibration wizard, on its own glass -------------------------------
//
// Same flow the game used to draw: confirm the centre, then each direction, with
// a progress ring and a live deviation bar so a tap that does nothing explains
// itself. The palette is CONFIG's (owner accent), not the game's terminal green.

static const char *const kWizDir[5] = {"", "IZQUIERDA", "DERECHA", "HACIA VOS",
                                       "ALEJANDO"};
static const char *const kWizHint[5] = {
    "", "Inclina hacia la izquierda", "Inclina hacia la derecha",
    "Inclina el borde de arriba hacia vos",
    "Ahora aleja el borde de arriba"};

static void drawWizardDots(int y) {
  const int gap = 20;
  int x = (DISPLAY_W - 3 * gap) / 2 - 6;
  for (uint8_t i = 0; i < STEER_WIZ_DIRECTIONS; i++) {
    int cx = x + i * gap;
    if (i < s_wiz.captured) {
      gfxFillCircle(cx, y, 4, uiAccent());
    } else if (i == s_wiz.captured) {
      gfxCircle(cx, y, 5, GFX_WHITE);
    } else {
      gfxCircle(cx, y, 4, CHROME_DIM);
    }
  }
}

static void drawWizard() {
  gfxClear(GFX_BLACK);
  gfxTextCentered(8, 1, "CALIBRACION", CHROME_DIM);

  if (s_wiz.step == 0) {
    gfxTextCentered(38, 2, "DEJALO EN", uiAccent());
    gfxTextCentered(62, 2, "EL CENTRO", uiAccent());
    gfxTextCentered(96, 1, "Apoyalo como para jugar, quieto", GFX_WHITE);
    chromeFooter("TOCA: CONFIRMAR   MANTENE: CANCELAR");
    return;
  }

  bool recorded = steerWizardRecorded(&s_wiz);
  bool centered = steerWizardCentred(&s_wiz);
  if (!recorded) {
    gfxTextCentered(34, 2, "AHORA", CHROME_DIM);
    gfxTextCentered(58, 2, kWizDir[s_wiz.step], uiAccent());
    gfxTextCentered(84, 1, kWizHint[s_wiz.step], GFX_WHITE);
  } else if (!centered) {
    gfxTextCentered(38, 2, "VOLVE AL CENTRO", GFX_WHITE);
    gfxTextCentered(84, 1, "Soltalo quieto, sin inclinar", GFX_WHITE);
  } else {
    gfxTextCentered(34, 2, "TOCA PARA", CHROME_DIM);
    gfxTextCentered(58, 2, "CONFIRMAR", uiAccent());
    gfxTextCentered(84, 1, kWizDir[s_wiz.step], GFX_WHITE);
  }

  drawWizardDots(104);

  int w = DISPLAY_W - 80;
  float full = STEER_WIZ_TILT_G * 2.0f;
  int permille = (int)(s_wiz.deviation / full * 1000.0f);
  if (permille > 1000) permille = 1000;
  if (permille < 0) permille = 0;
  gfxFillRect(40, 118, w, 8, CHROME_LINE);
  gfxFillRect(40, 118, w * permille / 1000, 8, uiAccent());
  gfxFillRect(40 + w / 2, 114, 2, 16, CHROME_DIM);

  chromeFooter("MANTENE: CANCELAR");
}

// Writes the solved axes, signs and baselines to the same NVS the game reads.
// The game re-reads it on entry, so it picks this up without a reboot.
static void applySteerSolution(const SteerSolution &sol) {
  SteerCalibration cal = settingsGetSteerCalibration();
  cal.latAxis = sol.latAxis;
  cal.upAxis = sol.upAxis;
  cal.sign = sol.sign;
  cal.signY = sol.signY;
  cal.baseline = sol.baseline;
  cal.baselineY = sol.baselineY;
  cal.valid = true;
  if (cal.gain == 0) cal.gain = 100;
  settingsSetSteerCalibration(cal);
  // The game keeps its own copy of the axes; reload it now so a later "swap
  // axis" or manual `steer` command cannot write the pre-wizard values back.
  gameInit();
}

static void applyFocus(uint32_t nowMs) {
  switch (s_focus) {
    case CFG_FOCUS_LCD_MINUS:
    case CFG_FOCUS_LCD_PLUS: {
      bool up = s_focus == CFG_FOCUS_LCD_PLUS;
      s_lcdPct = up ? cfgStepUp(s_lcdPct, CFG_LCD_MIN, CFG_LCD_MAX, CFG_LCD_STEP)
                    : cfgStepDown(s_lcdPct, CFG_LCD_MIN, CFG_LCD_MAX,
                                  CFG_LCD_STEP);
      displaySetBacklight(s_lcdPct);
      Serial.printf("config: brillo lcd %u%%\n", (unsigned)s_lcdPct);
      scheduleSave(nowMs);
      break;
    }
    case CFG_FOCUS_LED_MINUS:
    case CFG_FOCUS_LED_PLUS: {
      bool up = s_focus == CFG_FOCUS_LED_PLUS;
      s_ledPct = up ? cfgStepUp(s_ledPct, CFG_LED_MIN, CFG_LED_MAX, CFG_LED_STEP)
                    : cfgStepDown(s_ledPct, CFG_LED_MIN, CFG_LED_MAX,
                                  CFG_LED_STEP);
      uint8_t level = cfgPercentToLevel(s_ledPct);
      rgbSetLevel(level);
      Serial.printf("config: brillo led %u%% (%u/255)\n", (unsigned)s_ledPct,
                    (unsigned)level);
      scheduleSave(nowMs);
      break;
    }
    case CFG_FOCUS_ROT: {
      s_mode = rotModeCycle(s_mode);
      rotationSetMode(s_mode);
      Serial.printf("config: rotacion %s\n", modeLabel(s_mode));
      break;
    }
    case CFG_FOCUS_CAL: {
      steerWizardReset(&s_wiz);
      s_wizActive = true;
      s_wizDrawAt = 0;
      Serial.println("config: calibracion, deja el equipo en el centro");
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
  tapWindowReset(s_taps);
  s_wizActive = false;
  // A pending save belongs to the previous visit; write it before reading the
  // live values back, or the read would overwrite what is still unsaved.
  flushSave();
  seedValues();
  s_needRedraw = true;
}

void configLeave() {
  // Leaving is the last chance to persist: the debounce window may not have
  // elapsed yet. The focus goes back to the first control so the next visit
  // starts where the footer says it does; a rotation change never gets here,
  // because it repaints without leaving the screen.
  flushSave();
  s_wizActive = false;
  s_focus = CFG_FOCUS_FIRST;
}

void configShortPress() {
  // While the wizard owns the glass, a tap closes its current step. It is not a
  // double-tap gesture here: the owner is following a prompt, not navigating.
  if (s_wizActive) {
    SteerSolution sol;
    SteerWizardTap r = steerWizardTap(&s_wiz, &sol);
    if (r == STEER_WIZ_ADVANCED) {
      Serial.printf("config: calibracion %u/4\n", (unsigned)s_wiz.captured);
    } else if (r == STEER_WIZ_REJECTED) {
      Serial.println("config: falta inclinar y volver al centro");
    } else if (r == STEER_WIZ_SOLVED) {
      applySteerSolution(sol);
      s_wizActive = false;
      rgbPulse(0, 200, 120, 900);
      Serial.printf("config: calibrado lat=%u up=%u sign=%d signY=%d\n",
                    (unsigned)sol.latAxis, (unsigned)sol.upAxis, (int)sol.sign,
                    (int)sol.signY);
    } else if (r == STEER_WIZ_INCOMPLETE) {
      s_wizActive = false;
      Serial.println("config: calibracion incompleta, queda la anterior");
    }
    s_needRedraw = true;
    return;
  }
  // The first tap of a double does nothing yet: the window has to expire before
  // it is known to be a single tap that presses the highlighted control.
  if (tapWindowPress(s_taps, millis(), TAP_DOUBLE_MS) == TAP_DOUBLE) {
    s_focus = (uint8_t)((s_focus + 1) % CFG_FOCUS_COUNT);
    s_needRedraw = true;
    Serial.printf("config: foco %s\n", focusLabel(s_focus));
  }
}

bool configWizardActive() { return s_wizActive; }

void configStartCalibration() {
  steerWizardReset(&s_wiz);
  s_wizActive = true;
  s_wizDrawAt = 0;
  s_needRedraw = true;
  Serial.println("config: calibracion, deja el equipo en el centro");
}

bool configLongPress() {
  if (!s_wizActive) return false;
  // A hold is the way out of the wizard; the picker must not open on top of it.
  s_wizActive = false;
  s_needRedraw = true;
  Serial.println("config: calibracion cancelada");
  return true;
}

bool configTick(uint32_t nowMs) {
  if (s_savePending && (int32_t)(nowMs - s_saveDueAt) >= 0) flushSave();

  if (s_wizActive) {
    if (!g_imuOk) {
      s_wizActive = false;
      s_needRedraw = true;
      Serial.println("config: sin IMU, calibracion cancelada");
    } else {
      float a[3], g[3];
      imuReadAll(a, g);
      steerWizardFeed(&s_wiz, a);
      // 20 fps: the deviation bar and the prompt change with the tilt.
      if ((uint32_t)(nowMs - s_wizDrawAt) >= 50 || s_needRedraw) {
        s_wizDrawAt = nowMs;
        s_needRedraw = false;
        drawWizard();
        return true;
      }
      return false;
    }
  }

  // The panel can change any of these while this screen is open. While nothing of
  // ours is waiting to be written, take what the hardware actually has, so the
  // next press starts from the value the owner sees elsewhere.
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

  // A resolved single tap presses the highlighted control. The redraw happens
  // below in the same tick, so the changed bar or label is what gets pushed to
  // the panel; returning early here would push the previous frame and repaint on
  // the next one.
  TapResult tap = tapWindowPoll(s_taps, nowMs, TAP_DOUBLE_MS);
  if (tap == TAP_SINGLE) applyFocus(nowMs);

  if (!s_needRedraw) return false;
  s_needRedraw = false;
  drawScreen();
  return true;
}

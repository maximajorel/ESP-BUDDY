// Clock screen (S1).
//
// Requirement: current time for Tucuman, Argentina (UTC-3, no daylight saving),
// a digital clock on a black background with white text.
//
// The face is a seven-segment display, which is what "digital clock" means to a
// reader, drawn with dim unlit segments the way a real LED module looks. The
// timezone itself is not hard-coded here: the network module applies the offset
// from settings, so the web panel can move it.
#include <time.h>

#include "screens.h"
#include "settings.h"

// The face owns the whole canvas now that there is no header band and no
// seconds trace, so the digits grew to fill it: 310 px wide stays inside the
// 320 px landscape width.
#define CLK_DIGIT_W 64
#define CLK_DIGIT_H 104
#define CLK_THICK 10
#define CLK_GAP 6
#define CLK_COLON_W 18
#define CLK_COLON_GAP 12
#define CLK_Y 16

#define CLK_TOTAL_W                                          \
  (CLK_DIGIT_W * 4 + CLK_GAP * 2 + CLK_COLON_W + CLK_COLON_GAP * 2)
#define CLK_X0 ((DISPLAY_W - CLK_TOTAL_W) / 2)

#define CLK_DIGIT_X1 (CLK_X0 + CLK_DIGIT_W + CLK_GAP)
#define CLK_COLON_X (CLK_DIGIT_X1 + CLK_DIGIT_W + CLK_COLON_GAP)
#define CLK_DIGIT_X3 (CLK_COLON_X + CLK_COLON_W + CLK_COLON_GAP)
#define CLK_DIGIT_X4 (CLK_DIGIT_X3 + CLK_DIGIT_W + CLK_GAP)

#define CLK_DATE_Y 129

// Puro blanco sobre negro: los segmentos apagados no se dibujan. Un gris tenue
// se veía violáceo en el panel, y el requisito es texto blanco.
#define CLK_UNLIT GFX_BLACK
#define CLK_LIT GFX_WHITE

static const char *const WEEKDAYS[7] = {"DOM", "LUN", "MAR", "MIE",
                                        "JUE", "VIE", "SAB"};
static const char *const MONTHS[12] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                                       "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};

// Only repaint when something visible changed: an idle clock then costs no SPI
// traffic at all.
static long s_lastKey = -1;

static bool timeIsValid(const struct tm &t) {
  return t.tm_year > 100;  // anything before 2000 means NTP has not answered
}

// A clock that shows a wrong time is worse than one that admits it does not
// know yet, so the face refuses to print a time before NTP has answered.
static bool localNow(struct tm &out) {
  time_t now = time(nullptr);
  if (now < 1600000000) return false;
  localtime_r(&now, &out);
  return timeIsValid(out);
}

void clockEnter() { s_lastKey = -1; }

static void drawFace(const struct tm *t, bool hasTime, bool use24h) {
  gfxClear(GFX_BLACK);

  int hour = 0, minute = 0, second = 0;
  bool pm = false;
  if (hasTime) {
    hour = t->tm_hour;
    minute = t->tm_min;
    second = t->tm_sec;
    if (!use24h) {
      pm = hour >= 12;
      hour %= 12;
      if (hour == 0) hour = 12;
    }
  }

  char digits[5];
  if (hasTime) {
    snprintf(digits, sizeof(digits), "%02d%02d", hour, minute);
  } else {
    snprintf(digits, sizeof(digits), "    ");
  }

  const int xs[4] = {CLK_X0, CLK_DIGIT_X1, CLK_DIGIT_X3, CLK_DIGIT_X4};
  if (hasTime) {
    for (int i = 0; i < 4; i++) {
      gfxSegmentDigit(xs[i], CLK_Y, CLK_DIGIT_W, CLK_DIGIT_H, CLK_THICK,
                      digits[i], CLK_LIT, CLK_UNLIT);
    }
  } else {
    // Unlit segments are black now, so a clock with no time would be a blank
    // panel and read as a broken device. A dash per digit says "not set yet"
    // in the same pure-white-on-black language as the rest of the face.
    int barY = CLK_Y + (CLK_DIGIT_H - CLK_THICK) / 2;
    for (int i = 0; i < 4; i++) {
      gfxFillRect(xs[i], barY, CLK_DIGIT_W, CLK_THICK, CLK_LIT);
    }
  }
  // The colon blinks once a second, which is the cheapest way to show the clock
  // is alive without adding another moving element.
  bool colonOn = !hasTime || (second % 2) == 0;
  if (colonOn) {
    gfxSegmentColon(CLK_COLON_X, CLK_Y, CLK_COLON_W, CLK_DIGIT_H, CLK_THICK,
                    CLK_LIT);
  } else {
    gfxSegmentColon(CLK_COLON_X, CLK_Y, CLK_COLON_W, CLK_DIGIT_H, CLK_THICK,
                    CLK_UNLIT);
  }

  // Date line, plus the meridiem marker when the clock runs in 12-hour form.
  char dateLine[40];
  if (hasTime) {
    snprintf(dateLine, sizeof(dateLine), "%s %02d %s %04d",
             WEEKDAYS[t->tm_wday % 7], t->tm_mday, MONTHS[t->tm_mon % 12],
             t->tm_year + 1900);
  } else {
    snprintf(dateLine, sizeof(dateLine), "SIN HORA TODAVIA");
  }
  int dateW = gfxTextWidth(1, dateLine);
  int dateX = (DISPLAY_W - dateW) / 2;
  gfxText(dateX, CLK_DATE_Y, 1, dateLine, CLK_LIT);
  if (hasTime && !use24h) {
    gfxText(DISPLAY_W - 21, CLK_DATE_Y, 1, pm ? "PM" : "AM", CLK_LIT);
  }

  if (hasTime) {
    chromeFooter(use24h ? "TOCA: 12H   MANTENE: MENU" : "TOCA: 24H   MANTENE: MENU");
  } else {
    // Tapping does nothing useful here: the only fix is giving the device a
    // network, which happens on the NETWORK screen through the web panel.
    chromeFooter("CONFIGURA EL WIFI EN LA PANTALLA RED");
  }
}

bool clockTick(uint32_t nowMs) {
  (void)nowMs;
  struct tm t;
  bool hasTime = localNow(t);
  bool use24h = settingsGet().clock24h;

  // The repaint key carries every input the face depends on, so the screen only
  // redraws when a visible pixel could differ.
  long key = 0;
  if (hasTime) {
    key = ((long)(t.tm_year) * 400L + t.tm_mon * 32L + t.tm_mday) * 100000L +
          t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec;
  } else {
    key = -2;
  }
  key = key * 2 + (use24h ? 1 : 0);
  if (key == s_lastKey) return false;
  s_lastKey = key;

  drawFace(&t, hasTime, use24h);
  return true;
}

void clockShortPress() {
  Settings &cfg = settingsMutable();
  cfg.clock24h = !cfg.clock24h;
  settingsSave();
  s_lastKey = -1;  // force a repaint with the new format
}

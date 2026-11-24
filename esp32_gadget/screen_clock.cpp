// Clock screen (S1).
//
// Requirement: current time for Tucuman, Argentina (UTC-3, no daylight saving),
// a digital clock on a black background with white text. The clock is always
// 24-hour: the format switch was deprecated, and the short press now toggles the
// face to the weather of the place configured in the panel.
//
// The face is a seven-segment display, which is what "digital clock" means to a
// reader, drawn with dim unlit segments the way a real LED module looks. The
// timezone itself is not hard-coded here: the network module applies the offset
// from settings, so the web panel can move it.
#include <math.h>
#include <stdio.h>
#include <time.h>

#include "screens.h"
#include "text_fold.h"
#include "weather.h"
#include "weather_code.h"

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
// Which face the glass shows: the clock, or the weather the tap toggles to.
static bool s_showWeather = false;

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

void clockEnter() {
  s_lastKey = -1;
  // Entering the screen always lands on the clock: the weather is a peek, not a
  // mode the owner left on last time.
  s_showWeather = false;
}

// The clock is 24-hour only. The format switch was deprecated: the owner asked
// for a fixed 24 h face and the short press now belongs to the weather view, so
// there is no 12-hour branch left to keep in sync with the rest of the face.
static void drawFace(const struct tm *t, bool hasTime) {
  gfxClear(GFX_BLACK);

  int hour = 0, minute = 0, second = 0;
  if (hasTime) {
    hour = t->tm_hour;
    minute = t->tm_min;
    second = t->tm_sec;
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

  if (hasTime) {
    chromeFooter("TOCA: CLIMA   MANTENE: MENU");
  } else {
    // Tapping does nothing useful here: the only fix is giving the device a
    // network, which happens on the NETWORK screen through the web panel.
    chromeFooter("CONFIGURA EL WIFI EN LA PANTALLA RED");
  }
}

// ---------------------------------------------------------------------------
// Weather face
//
// The tap on the clock shows the weather of the configured place. Everything
// here is drawn from the snapshot the service already fetched: the face never
// touches the network, so a slow or dead connection can only delay the reading,
// never freeze the glass.
// ---------------------------------------------------------------------------

static int roundInt(float value) {
  return (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static void drawSun(int cx, int cy, int r) {
  gfxFillCircle(cx, cy, (r * 42) / 100, GFX_WHITE);
  for (int i = 0; i < 8; i++) {
    const float angle = (float)i * 3.14159265f / 4.0f;
    const int x0 = cx + (int)(cosf(angle) * (float)((r * 60) / 100));
    const int y0 = cy + (int)(sinf(angle) * (float)((r * 60) / 100));
    const int x1 = cx + (int)(cosf(angle) * (float)((r * 92) / 100));
    const int y1 = cy + (int)(sinf(angle) * (float)((r * 92) / 100));
    gfxLine(x0, y0, x1, y1, GFX_WHITE);
  }
}

static void drawCloud(int cx, int cy, int r) {
  gfxFillCircle(cx - (r * 40) / 100, cy + (r * 10) / 100, (r * 40) / 100,
                GFX_WHITE);
  gfxFillCircle(cx, cy - (r * 14) / 100, (r * 52) / 100, GFX_WHITE);
  gfxFillCircle(cx + (r * 42) / 100, cy + (r * 10) / 100, (r * 38) / 100,
                GFX_WHITE);
  gfxFillRect(cx - (r * 46) / 100, cy + (r * 20) / 100, (r * 92) / 100,
              (r * 34) / 100, GFX_WHITE);
}

static void drawDrops(int cx, int cy, int length) {
  for (int i = -1; i <= 1; i++) {
    const int x = cx + i * 14;
    gfxLine(x, cy, x - 4, cy + length, GFX_WHITE);
  }
}

static void drawFlakes(int cx, int cy) {
  for (int i = -1; i <= 1; i++) {
    const int x = cx + i * 14;
    gfxFillCircle(x, cy + 8, 2, GFX_WHITE);
    gfxHLine(x - 4, cy + 8, 9, GFX_WHITE);
    gfxVLine(x, cy + 4, 9, GFX_WHITE);
  }
}

static void drawBolt(int cx, int cy) {
  const int xs[6] = {cx + 5, cx - 12, cx - 2, cx + 6, cx - 6, cx + 12};
  const int ys[6] = {cy + 12, cy + 34, cy + 34, cy + 26, cy + 52, cy + 26};
  gfxFillPolygon(xs, ys, 6, GFX_WHITE);
}

static void drawWeatherIcon(int cx, int cy, int r, uint8_t icon) {
  switch (icon) {
    case WEATHER_ICON_CLEAR:
      drawSun(cx, cy, r);
      break;
    case WEATHER_ICON_PARTLY:
      drawSun(cx - (r * 30) / 100, cy - (r * 30) / 100, (r * 62) / 100);
      drawCloud(cx + (r * 15) / 100, cy + (r * 22) / 100, (r * 78) / 100);
      break;
    case WEATHER_ICON_CLOUDY:
      drawCloud(cx, cy - (r * 8) / 100, r);
      break;
    case WEATHER_ICON_FOG:
      drawCloud(cx, cy - (r * 24) / 100, r);
      gfxHLine(cx - (r * 70) / 100, cy + (r * 48) / 100, (r * 140) / 100,
               GFX_WHITE);
      gfxHLine(cx - (r * 58) / 100, cy + (r * 66) / 100, (r * 116) / 100,
               GFX_WHITE);
      break;
    case WEATHER_ICON_DRIZZLE:
      drawCloud(cx, cy - (r * 20) / 100, r);
      drawDrops(cx, cy + (r * 38) / 100, 8);
      break;
    case WEATHER_ICON_RAIN:
      drawCloud(cx, cy - (r * 20) / 100, r);
      drawDrops(cx, cy + (r * 38) / 100, 14);
      break;
    case WEATHER_ICON_SNOW:
      drawCloud(cx, cy - (r * 20) / 100, r);
      drawFlakes(cx, cy + (r * 38) / 100);
      break;
    case WEATHER_ICON_STORM:
      drawCloud(cx, cy - (r * 22) / 100, r);
      drawBolt(cx, cy + (r * 22) / 100);
      break;
    default:
      gfxTextCentered(cy - 16, 4, "?", GFX_WHITE);
      break;
  }
}

// One number per visible field. It is cheaper and more honest than comparing the
// whole struct: a new fetch that lands on the same numbers does not need a
// repaint, and any change that matters does.
static long weatherFaceKey(const WeatherStatusInfo &w) {
  uint32_t h = 2166136261u;
  const uint32_t parts[] = {
      (uint32_t)w.configured, (uint32_t)w.hasData, (uint32_t)w.syncing,
      (uint32_t)roundInt(w.now.tempC), (uint32_t)roundInt(w.now.feelsC),
      (uint32_t)roundInt(w.now.tmaxC), (uint32_t)roundInt(w.now.tminC),
      (uint32_t)w.now.humidity, (uint32_t)roundInt(w.now.windKph),
      (uint32_t)w.now.code, (uint32_t)w.now.isDay,
  };
  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
    h ^= parts[i];
    h *= 16777619u;
  }
  for (const char *p = w.place.name; *p; p++) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }
  for (const char *p = w.lastError; *p; p++) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }
  return (long)(h & 0x7FFFFFFFu);
}

static void drawWeatherFace(const WeatherStatusInfo &w) {
  gfxClear(GFX_BLACK);
  char line[96];

  if (!w.configured) {
    gfxTextCentered(52, 3, "SIN UBICACION", GFX_WHITE);
    gfxTextCentered(98, 1, "CONFIGURA EL CLIMA EN EL PANEL", GFX_WHITE);
    gfxTextCentered(116, 1, "INTEGRACIONES > CLIMA", CHROME_DIM);
    chromeFooter("TOCA: RELOJ   MANTENE: MENU");
    return;
  }

  // City line, folded to what the 5x7 font can draw. Scale 2 when it fits, which
  // keeps short names readable without pushing a long one off the glass.
  char city[WEATHER_NAME_MAX + 1];
  textFoldAscii(w.place.name, city, sizeof(city));
  const int cityScale = gfxTextWidth(2, city) <= DISPLAY_W - 12 ? 2 : 1;
  gfxTextCentered(4, cityScale, city, GFX_WHITE);

  if (!w.hasData) {
    gfxTextCentered(58, 3, "SIN LECTURA", GFX_WHITE);
    const char *why = w.lastError[0] ? w.lastError : "Esperando la primera consulta";
    char folded[96];
    textFoldAscii(why, folded, sizeof(folded));
    gfxTextCentered(102, 1, folded, CHROME_DIM);
    if (w.syncing) gfxTextCentered(120, 1, "Consultando...", CHROME_DIM);
    chromeFooter("TOCA: RELOJ   MANTENE: MENU");
    return;
  }

  // Big temperature on the left, the icon on the right.
  snprintf(line, sizeof(line), "%d", roundInt(w.now.tempC));
  const int tempScale = 6;
  const int tempW = gfxTextWidth(tempScale, line);
  gfxText(20, 24, tempScale, line, GFX_WHITE);
  gfxText(20 + tempW + 4, 32, 2, "C", CHROME_DIM);
  drawWeatherIcon(252, 52, 26, weatherCodeIcon(w.now.code));

  // The condition in words, because a number is not weather to a reader.
  char label[WEATHER_LABEL_MAX];
  textFoldAscii(weatherCodeLabel(w.now.code), label, sizeof(label));
  gfxText(20, 82, 1, label, GFX_WHITE);

  snprintf(line, sizeof(line), "SENSACION %d C", roundInt(w.now.feelsC));
  gfxText(20, 104, 1, line, GFX_WHITE);
  snprintf(line, sizeof(line), "MAX %d   MIN %d", roundInt(w.now.tmaxC),
           roundInt(w.now.tminC));
  gfxText(20, 120, 1, line, GFX_WHITE);
  snprintf(line, sizeof(line), "HUMEDAD %d%%   VIENTO %d KM/H", w.now.humidity,
           roundInt(w.now.windKph));
  gfxText(20, 136, 1, line, GFX_WHITE);
  if (w.syncing) gfxText(DISPLAY_W - 22, 104, 1, "...", CHROME_DIM);

  chromeFooter("TOCA: RELOJ   MANTENE: MENU");
}

bool clockTick(uint32_t nowMs) {
  (void)nowMs;

  if (s_showWeather) {
    const WeatherStatusInfo w = weatherStatusInfo();
    const long key = weatherFaceKey(w);
    if (key == s_lastKey) return false;
    s_lastKey = key;
    drawWeatherFace(w);
    return true;
  }

  struct tm t;
  bool hasTime = localNow(t);

  // The repaint key carries every input the face depends on, so the screen only
  // redraws when a visible pixel could differ.
  long key = 0;
  if (hasTime) {
    key = ((long)(t.tm_year) * 400L + t.tm_mon * 32L + t.tm_mday) * 100000L +
          t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec;
  } else {
    key = -2;
  }
  if (key == s_lastKey) return false;
  s_lastKey = key;

  drawFace(&t, hasTime);
  return true;
}

void clockShortPress() {
  // The format switch is gone: a tap toggles between the clock and the weather
  // of the configured place, and a second tap comes back.
  s_showWeather = !s_showWeather;
  s_lastKey = -1;  // force a repaint of the other face
}

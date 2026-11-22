#include "display.h"

#include "display_blit.h"
#include "gfx.h"

#include <SPI.h>
#include <esp_heap_caps.h>

// Pins fixed by the board layout (Waveshare ESP32-S3-LCD-1.47B schematic).
#define PIN_MOSI 45
#define PIN_SCLK 40
#define PIN_CS 42
#define PIN_DC 41
#define PIN_RST 39
#define PIN_BL 46

#define SPI_FREQ 80000000

// The blit helper's numbers have to be the driver's, or `displayBlitTarget` would
// describe a mapping the panel never sees.
static_assert(ORIENT_MIRROR == BLIT_ORIENT_MIRROR, "orientation numbering drifted");
static_assert(ORIENT_CW == BLIT_ORIENT_CW, "orientation numbering drifted");
static_assert(ORIENT_CCW == BLIT_ORIENT_CCW, "orientation numbering drifted");
static_assert(ORIENT_MIRROR2 == BLIT_ORIENT_MIRROR2, "orientation numbering drifted");
#define BL_FREQ 1000
#define BL_RES 10

static SPIClass s_spi(FSPI);

static uint16_t *s_panel = nullptr;  // transpose target, panel scan order
static uint8_t s_orientation = ORIENT_CW;
static uint8_t s_backlight = 90;
static bool s_awake = true;

static void applyBacklight() {
  uint32_t duty = s_awake ? (uint32_t)s_backlight * 1024 / 100 : 0;
  ledcWrite(PIN_BL, duty);
}

static void writeCommand(uint8_t cmd) {
  s_spi.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW);
  s_spi.transfer(cmd);
  digitalWrite(PIN_CS, HIGH);
  s_spi.endTransaction();
}

static void writeData(uint8_t data) {
  s_spi.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, HIGH);
  s_spi.transfer(data);
  digitalWrite(PIN_CS, HIGH);
  s_spi.endTransaction();
}

static void writeDataBulk(const uint8_t *data, size_t len) {
  s_spi.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, HIGH);
  s_spi.writeBytes(data, len);
  digitalWrite(PIN_CS, HIGH);
  s_spi.endTransaction();
}

void displaySetBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  s_backlight = percent;
  applyBacklight();
}

uint8_t displayGetBacklight() { return s_backlight; }

void displaySetAwake(bool awake) {
  s_awake = awake;
  applyBacklight();
}

bool displayIsAwake() { return s_awake; }

void displayInitPanel() {
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_RST, OUTPUT);

  ledcAttach(PIN_BL, BL_FREQ, BL_RES);
  displaySetBacklight(s_backlight);

  s_spi.begin(PIN_SCLK, -1, PIN_MOSI);

  // Hardware reset.
  digitalWrite(PIN_CS, LOW);
  delay(50);
  digitalWrite(PIN_RST, LOW);
  delay(50);
  digitalWrite(PIN_RST, HIGH);
  delay(50);

  // Initialisation sequence taken from Waveshare's reference driver. Only the
  // values that matter for this panel are commented; the rest is the vendor
  // power/gamma tuning and is left as published.
  writeCommand(0x11);  // sleep out
  delay(120);
  writeCommand(0x36);  // MADCTL
  // 0x00: no address transposition. This is the configuration Waveshare's own
  // driver actually executes - its VERTICAL/HORIZONTAL macros are a leftover and
  // `if (HORIZONTAL)` is a constant true, so the 0x70 branch is dead code that
  // was never validated. Using it produced a 240-column clamped window and a
  // sheared image.
  writeData(0x00);
  writeCommand(0x3A);  // pixel format
  writeData(0x05);     // 16 bits per pixel
  writeCommand(0xB0);
  writeData(0x00);
  writeData(0xE8);
  writeCommand(0xB2);
  writeData(0x0C);
  writeData(0x0C);
  writeData(0x00);
  writeData(0x33);
  writeData(0x33);
  writeCommand(0xB7);
  writeData(0x35);
  writeCommand(0xBB);
  writeData(0x35);
  writeCommand(0xC0);
  writeData(0x2C);
  writeCommand(0xC2);
  writeData(0x01);
  writeCommand(0xC3);
  writeData(0x13);
  writeCommand(0xC4);
  writeData(0x20);
  writeCommand(0xC6);
  writeData(0x0F);
  writeCommand(0xD0);
  writeData(0xA4);
  writeData(0xA1);
  writeCommand(0xD6);
  writeData(0xA1);
  writeCommand(0xE0);
  writeData(0xF0);
  writeData(0x00);
  writeData(0x04);
  writeData(0x04);
  writeData(0x04);
  writeData(0x05);
  writeData(0x29);
  writeData(0x33);
  writeData(0x3E);
  writeData(0x38);
  writeData(0x12);
  writeData(0x12);
  writeData(0x28);
  writeData(0x30);
  writeCommand(0xE1);
  writeData(0xF0);
  writeData(0x07);
  writeData(0x0A);
  writeData(0x0D);
  writeData(0x0B);
  writeData(0x07);
  writeData(0x28);
  writeData(0x33);
  writeData(0x3E);
  writeData(0x36);
  writeData(0x14);
  writeData(0x14);
  writeData(0x29);
  writeData(0x32);
  writeCommand(0x21);  // inversion on
  writeCommand(0x11);
  delay(120);
  writeCommand(0x29);  // display on
}

bool displayBegin() {
  s_panel = (uint16_t *)heap_caps_malloc(PANEL_PIXELS * 2, MALLOC_CAP_SPIRAM);
  if (!s_panel) return false;
  memset(s_panel, 0, PANEL_PIXELS * 2);
  displayInitPanel();
  return true;
}

void displaySetOrientation(uint8_t orientation) {
  if (orientation > 3) return;
  s_orientation = orientation;
}

uint8_t displayGetOrientation() { return s_orientation; }

// Frame-time accounting, read by `buddydraw`. microseconds since boot wrap every
// ~71 minutes; the arithmetic below only ever uses differences inside one frame.
static uint32_t s_pushTotalUs = 0;
static uint32_t s_pushMaxUs = 0;
static uint32_t s_pushCount = 0;

void displayPush(const uint16_t *fb) {
  if (!s_panel) return;

  // The panel wants the high byte first while the ESP32 keeps uint16_t low byte
  // first, so the swap happens here; doing it during the transpose costs
  // nothing extra.
  //
  // One loop per orientation instead of a `switch` inside the 55 040-pixel loop:
  // that branch is the difference between affording a higher frame rate and not.
  // The mapping itself is pinned on the host by `tools/display_blit_test.cpp`.
  // The panel takes the low byte first on this unit (proven with the three bars:
  // a red 0xF800 arrived as 0x00F8, blue), so the host-order value is kept and
  // the little-endian bytes go out as low, high.
  uint32_t t0 = micros();
  switch (s_orientation) {
    case ORIENT_MIRROR:
      for (int y = 0; y < DISPLAY_H; y++) {
        const uint16_t *src = fb + (size_t)y * DISPLAY_W;
        for (int x = 0; x < DISPLAY_W; x++) s_panel[x * PANEL_W + y] = src[x];
      }
      break;
    case ORIENT_CCW:
      for (int y = 0; y < DISPLAY_H; y++) {
        const uint16_t *src = fb + (size_t)y * DISPLAY_W;
        for (int x = 0; x < DISPLAY_W; x++) {
          s_panel[(DISPLAY_W - 1 - x) * PANEL_W + y] = src[x];
        }
      }
      break;
    case ORIENT_MIRROR2:
      for (int y = 0; y < DISPLAY_H; y++) {
        const uint16_t *src = fb + (size_t)y * DISPLAY_W;
        for (int x = 0; x < DISPLAY_W; x++) {
          s_panel[(DISPLAY_W - 1 - x) * PANEL_W + (PANEL_W - 1 - y)] = src[x];
        }
      }
      break;
    case ORIENT_CW:
    default:
      for (int y = 0; y < DISPLAY_H; y++) {
        const uint16_t *src = fb + (size_t)y * DISPLAY_W;
        for (int x = 0; x < DISPLAY_W; x++) {
          s_panel[x * PANEL_W + (PANEL_W - 1 - y)] = src[x];
        }
      }
      break;
  }

  // Full-frame window. With MADCTL 0x00 the column address is the panel's 172
  // source lines, centred by a 34-column offset, and the row address is its 320
  // gate lines. Writing 320 columns instead would exceed the ST7789's 240-column
  // limit and shear the image.
  writeCommand(0x2A);
  writeData(0x00);
  writeData(0x00 + PANEL_COL_OFFSET);
  writeData((PANEL_W - 1) >> 8);
  writeData((PANEL_W - 1 + PANEL_COL_OFFSET) & 0xFF);
  writeCommand(0x2B);
  writeData(0x00);
  writeData(0x00);
  writeData((PANEL_H - 1) >> 8);
  writeData((PANEL_H - 1) & 0xFF);
  writeCommand(0x2C);

  writeDataBulk((const uint8_t *)s_panel, (uint32_t)PANEL_PIXELS * 2);

  // How long one frame costs on the way out. A face animation is only as smooth
  // as the slowest half of the frame, so this number decides the frame rate
  // instead of a guess: `buddydraw` prints it.
  uint32_t us = (uint32_t)(micros() - t0);
  s_pushTotalUs += us;
  s_pushCount++;
  if (us > s_pushMaxUs) s_pushMaxUs = us;
}

void displayPushStats(uint32_t *avgUs, uint32_t *maxUs, uint32_t *count) {
  if (avgUs) *avgUs = s_pushCount ? s_pushTotalUs / s_pushCount : 0;
  if (maxUs) *maxUs = s_pushMaxUs;
  if (count) *count = s_pushCount;
}

void displayPushStatsReset() {
  s_pushTotalUs = 0;
  s_pushMaxUs = 0;
  s_pushCount = 0;
}

// Proof that the panel is actually being driven: three pure bars side by side,
// red | green | blue. A sequential full-screen flash was easy to miss and said
// nothing about channel order; with the three at once, a red/blue swap is
// obvious and a missing blue channel is too. This used to run at every boot,
// which flashed the room red before the splash; now it only runs on demand.
void displayColorTest() {
  static const uint16_t COLORS[3] = {0xF800, 0x07E0, 0x001F};
  const int w = DISPLAY_W / 3;
  for (int i = 0; i < 3; i++) {
    int bar = (i == 2) ? DISPLAY_W - 2 * w : w;
    gfxFillRect(i * w, 0, bar, DISPLAY_H, COLORS[i]);
  }
  displayPush(gfxBuffer());
  Serial.println("panel test: red | green | blue bars");
  delay(5000);
  Serial.println("color test done");
}

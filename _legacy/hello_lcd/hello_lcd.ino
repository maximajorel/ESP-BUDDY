/*
 * hello_lcd - first contact with the Waveshare ESP32-S3-LCD-1.47B panel.
 *
 * Goal: understand the layer under LVGL. The screen is a plain ST7789
 * controller wired over SPI; everything LVGL does later is "write pixels
 * into a rectangular window".
 *
 * What it does, in order:
 *   1. initialises the panel and turns the backlight on
 *   2. paints eight colour bars, so RGB order can be checked by eye
 *   3. paints an RGB gradient, so colour depth is visible
 *   4. sweeps a white line, so refresh speed is visible
 *
 * Panel: ST7789, 172x320, 262K colours, SPI @ 80 MHz.
 * Pins come from Display_ST7789.h (MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=46).
 */

#include "Display_ST7789.h"

// Panel is 172x320. Sending it one row at a time means 320 window setups per
// frame, so we push small bands instead: fewer commands, less overhead.
#define BAND_H 16
static uint16_t band[LCD_WIDTH * BAND_H];

// The ST7789 expects 16-bit pixels as rrrrrggggggbbbbb.
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Fills rows [y0, y0+h) with a solid colour.
// `band` only holds BAND_H rows, so taller requests are split into chunks.
// Without the split a caller asking for 40 rows would write ~13 KB past the
// end of a 5.5 KB buffer and corrupt memory (observed: panic loop).
void fillBand(int y0, int h, uint16_t color) {
  while (h > 0) {
    int rows = (h < BAND_H) ? h : BAND_H;
    for (int i = 0; i < LCD_WIDTH * rows; i++) band[i] = color;
    LCD_addWindow(0, y0, LCD_WIDTH - 1, y0 + rows - 1, band);
    y0 += rows;
    h -= rows;
  }
}

void drawColorBars() {
  static const uint16_t bars[8] = {
      0xF800,  // red
      0x07E0,  // green
      0x001F,  // blue
      0x07FF,  // cyan
      0xFFE0,  // yellow
      0xF81F,  // magenta
      0xFFFF,  // white
      0x0000,  // black
  };
  const int barH = LCD_HEIGHT / 8;
  for (int b = 0; b < 8; b++) {
    int y0 = b * barH;
    int h = (b == 7) ? (LCD_HEIGHT - y0) : barH;
    fillBand(y0, h, bars[b]);
  }
}

void drawGradient() {
  for (int y0 = 0; y0 < LCD_HEIGHT; y0 += BAND_H) {
    int h = (LCD_HEIGHT - y0 < BAND_H) ? (LCD_HEIGHT - y0) : BAND_H;
    for (int y = 0; y < h; y++) {
      uint8_t level = (uint8_t)((uint32_t)(y0 + y) * 255 / (LCD_HEIGHT - 1));
      for (int x = 0; x < LCD_WIDTH; x++) {
        uint8_t across = (uint8_t)((uint32_t)x * 255 / (LCD_WIDTH - 1));
        band[y * LCD_WIDTH + x] = rgb565(level, across, 255 - level);
      }
    }
    LCD_addWindow(0, y0, LCD_WIDTH - 1, y0 + h - 1, band);
  }
}

void sweepLine() {
  for (int y = 0; y < LCD_HEIGHT; y += 4) {
    fillBand(y, 2, 0xFFFF);
    delay(3);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nhello_lcd: starting");

  LCD_Init();
  Set_Backlight(90);

  Serial.println("colour bars...");
  drawColorBars();
  delay(1500);

  Serial.println("gradient...");
  drawGradient();
  delay(1500);

  Serial.println("sweep...");
  sweepLine();

  Serial.println("done, cycling");
}

void loop() {
  static uint32_t frame = 0;
  Serial.printf("frame %lu\n", (unsigned long)frame++);
  drawColorBars();
  delay(800);
  drawGradient();
  delay(800);
}

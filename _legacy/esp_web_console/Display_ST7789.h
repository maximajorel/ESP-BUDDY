#pragma once
#include <Arduino.h>
#include <SPI.h>
#define LCD_WIDTH   172 //LCD width
#define LCD_HEIGHT  320 //LCD height
#define LCD_PIXELS  (LCD_WIDTH * LCD_HEIGHT)

// The panel takes 16-bit pixels high byte first, while the ESP32 stores
// uint16_t low byte first. Swapping up front lets a framebuffer be handed to
// LCD_PushFullFrame as raw bytes with no per-pixel work at transfer time.
static inline uint16_t LCD_Swap16(uint16_t c) {
  return (uint16_t)((c << 8) | (c >> 8));
}

#define SPIFreq                        80000000
#define EXAMPLE_PIN_NUM_MISO           -1
#define EXAMPLE_PIN_NUM_MOSI           45
#define EXAMPLE_PIN_NUM_SCLK           40
#define EXAMPLE_PIN_NUM_LCD_CS         42
#define EXAMPLE_PIN_NUM_LCD_DC         41
#define EXAMPLE_PIN_NUM_LCD_RST        39
#define EXAMPLE_PIN_NUM_BK_LIGHT       46
#define Frequency       1000                    // PWM frequencyconst 
#define Resolution      10                         
#define Backlight_MAX   100     

#define VERTICAL   0
#define HORIZONTAL 1

#define Offset_X 34
#define Offset_Y 0

extern uint8_t LCD_Backlight;

void LCD_SetCursor(uint16_t x1, uint16_t y1, uint16_t x2,uint16_t y2);

void LCD_Init(void);
void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t  Yend);
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend,uint16_t* color);

// Fast full-frame blit. `buf` must hold LCD_WIDTH*LCD_HEIGHT pixels already in
// panel byte order (high byte first), which is what LCD_Swap16() produces.
void LCD_PushFullFrame(const uint16_t* buf);

// Same, but taking raw panel-order bytes. Used when the pixels arrive already
// encoded, for example an RGB565 image uploaded from the browser.
void LCD_PushFullFrameBytes(const uint8_t *bytes);

void Backlight_Init(void);
void Set_Backlight(uint8_t Light);

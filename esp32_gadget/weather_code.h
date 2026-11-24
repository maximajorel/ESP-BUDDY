// WMO weather codes turned into the two things the glass can use: a short
// Spanish label and one of a handful of drawable icons.
//
// Open-Meteo reports the WMO 4677 code group (0..99). Showing the number would be
// honest and useless, so this maps it once, here, and `tools/weather_core_test.cpp`
// pins every group the mapping claims to cover. Pure: no Arduino, no drawing.
#pragma once

#include <stdint.h>

enum WeatherIcon {
  WEATHER_ICON_UNKNOWN = 0,
  WEATHER_ICON_CLEAR,
  WEATHER_ICON_PARTLY,
  WEATHER_ICON_CLOUDY,
  WEATHER_ICON_FOG,
  WEATHER_ICON_DRIZZLE,
  WEATHER_ICON_RAIN,
  WEATHER_ICON_SNOW,
  WEATHER_ICON_STORM,
};

// Short ASCII label for the code. "Sin datos" when the code is not one this
// firmware knows: admitting it is better than guessing a word.
#define WEATHER_LABEL_MAX 32
const char *weatherCodeLabel(int code);

// One icon per group, so the face can draw a sun, a cloud or a drop without
// knowing the exact code.
uint8_t weatherCodeIcon(int code);

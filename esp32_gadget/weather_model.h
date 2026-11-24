// The weather snapshot the glass reads, plus the place the owner configured.
//
// Pure: no Arduino, no network. `weather.*` fills it, `screen_clock.cpp` draws
// it, the API publishes it. Splitting the shape from the fetch keeps the parser
// and the drawing rules testable on the computer.
#pragma once

#include <stdint.h>

#define WEATHER_NAME_MAX 48
#define WEATHER_REGION_MAX 32
#define WEATHER_COUNTRY_MAX 8
#define WEATHER_GEOCODE_MAX 5

// One reading from Open-Meteo's `current` block plus the day's extremes.
struct WeatherNow {
  bool valid;        // false until a successful fetch
  float tempC;
  float feelsC;
  int humidity;      // 0..100
  float windKph;
  float tmaxC;       // today's maximum
  float tminC;       // today's minimum
  int code;          // WMO weather code
  bool isDay;
};

// Where the weather is for. `name` is what the panel and the glass show.
struct WeatherPlace {
  bool valid;
  float lat;
  float lon;
  char name[WEATHER_NAME_MAX + 1];
};

// One hit from the Open-Meteo geocoding search. The panel shows
// "name, region, country" so two cities with the same name stay distinguishable.
struct WeatherGeoResult {
  char name[WEATHER_NAME_MAX + 1];
  char region[WEATHER_REGION_MAX + 1];
  char country[WEATHER_COUNTRY_MAX + 1];
  float lat;
  float lon;
};

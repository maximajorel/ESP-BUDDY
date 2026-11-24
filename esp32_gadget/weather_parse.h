// Parses the Open-Meteo responses into the pure shapes in `weather_model.h`.
//
// Pure in the same sense as `calendar_parse`: it needs ArduinoJson but not
// Arduino, so the exact JSON contract is pinned on the computer. A reading is
// only accepted when the required fields are present; anything else returns
// false and the caller keeps the previous snapshot, because a half-read weather
// is worse than an honest "sin lectura".
#pragma once

#include <stddef.h>

#include "weather_model.h"

// Parses the `/v1/forecast` answer. Returns false when the JSON is unusable or
// the `current` block is missing its temperature or weather code.
bool weatherParseNow(const char *json, WeatherNow *out);

// Parses the `/v1/search` answer into up to `cap` places. Returns the count.
size_t weatherParseGeocode(const char *json, WeatherGeoResult *out, size_t cap);

// Parses one ipwho.is answer into a place, naming it "City, CC". Returns false
// when the lookup failed or did not carry usable coordinates.
bool weatherParseIpLocation(const char *json, WeatherPlace *out);

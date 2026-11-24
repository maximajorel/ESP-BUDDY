// The configured place and refresh cadence, in their own NVS namespace.
//
// Same reason as `calendar_store`: `Settings` is migrated by the size of its
// blob, so a new feature gets its own namespace instead of growing that struct.
// The place name is stored as UTF-8 exactly as the geocoder returned it; only the
// glass folds it to ASCII when drawing.
#pragma once

#include <stdint.h>

#include "weather_model.h"

#define WEATHER_REFRESH_MIN_MIN 5
#define WEATHER_REFRESH_MIN_MAX 180
#define WEATHER_REFRESH_MIN_DEFAULT 15

void weatherStoreBegin();

// Reads the stored place. Returns false (and an empty place) when none was saved
// or the coordinates are out of range.
bool weatherStoreLoad(WeatherPlace *out);

// Validates and saves the place under its own keys.
bool weatherStoreSave(const WeatherPlace *place);

void weatherStoreForget();

int weatherStoreRefreshMin();
bool weatherStoreSetRefreshMin(int minutes);

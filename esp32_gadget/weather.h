// The weather service: the place the owner configured, the Open-Meteo poll and
// the snapshot the clock's weather face reads every frame.
//
// Its own task, its own TLS client (through `weather_http`) and its own mutex,
// exactly like the calendar. The glass never blocks on the network: `screen_clock`
// copies the last snapshot, and a failed fetch leaves the previous reading in
// place with an error the panel can explain.
#pragma once

#include <Arduino.h>

#include "weather_model.h"

enum WeatherStatusKind {
  WEATHER_UNCONFIGURED = 0,  // no place saved yet
  WEATHER_NO_DATA,           // place saved, no successful reading yet
  WEATHER_READY,             // place saved, last reading is good
  WEATHER_ERROR,             // place saved, last fetch failed
};

struct WeatherStatusInfo {
  WeatherStatusKind kind;
  bool configured;
  bool hasData;
  bool syncing;
  WeatherPlace place;
  WeatherNow now;  // meaningful when hasData
  uint32_t lastAttemptMs;
  uint32_t lastSuccessMs;
  int refreshMin;
  char lastError[96];
};

void weatherBegin();

// Called by the service task every second. Exposed so the console can pump it
// in-process when there is no network task for a probe.
void weatherTick();

// Copies the status under the lock. Always safe from the web task.
WeatherStatusInfo weatherStatusInfo();

// Asks the service task for a fetch on its next tick.
bool weatherRequestRefresh();

// Panel actions. These may take a couple of seconds because they talk HTTPS on
// the caller's task, the same trust model as the calendar's connect button.
bool weatherSavePlace(float lat, float lon, const char *name, char *err,
                      size_t errCap);
bool weatherDetectPlace(char *err, size_t errCap);
size_t weatherSearch(const char *query, WeatherGeoResult *out, size_t cap,
                     char *err, size_t errCap);
void weatherForget();
bool weatherSetRefreshMin(int minutes);

// The exact request paths sent to Open-Meteo and to the IP-location service.
//
// Pure and header-driven in spirit, but the percent-encoding needs the shared
// RFC 3986 encoder, so the strings live in a `.cpp` and are pinned by
// `tools/weather_core_test.cpp`. A path built wrong here is a 400 from the
// server, which the panel could only report as "sin lectura".
#pragma once

#include <stddef.h>

// Same hosts the services document. Kept next to the paths so a host change is
// one edit, not a hunt through the module.
#define WEATHER_HOST "api.open-meteo.com"
#define WEATHER_GEOCODE_HOST "geocoding-api.open-meteo.com"
#define WEATHER_IP_HOST "ipwho.is"

// Long enough for the forecast query with a four-decimal coordinate pair and for
// a generously encoded city search.
#define WEATHER_PATH_MAX 320

// `/v1/forecast` for the current reading and today's extremes, in the place's own
// timezone. Returns false — leaving `out` empty — when the buffer does not fit.
bool weatherBuildForecastPath(float lat, float lon, char *out, size_t cap);

// `/v1/search` for a typed city name. Returns false on an empty query or when the
// buffer does not fit.
bool weatherBuildGeocodePath(const char *query, char *out, size_t cap);

// HTTPS to the weather services, with its own client.
//
// Deliberately not shared with the calendar's, Spotify's or the bridge's TLS
// client: two tasks tearing down one client already cost this project a heap
// crash. Open-Meteo and the IP-location service are plain GETs with no headers,
// so this transport is smaller than the calendar's, but the rule is the same:
// one client, one mutex, certificates validated against the core's CA bundle.
#pragma once

#include <Arduino.h>

// `transportError` when the answer was larger than the caller allowed.
#define WEATHER_HTTP_BODY_TOO_LARGE (-1000)

struct WeatherHttpResult {
  int status = 0;          // 0 when no status line arrived
  int transportError = 0;  // HTTPClient's negative code, for the panel
  bool tlsFailed = false;
  String body;
};

void weatherHttpBegin();

// GET a JSON document. Returns true when a status line arrived, whatever it was;
// `out->status` says which.
bool weatherHttpGet(const char *host, const char *path, size_t maxBody,
                    WeatherHttpResult *out);

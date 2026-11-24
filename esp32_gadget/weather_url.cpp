#include "weather_url.h"

#include <stdio.h>

#include "url_codec.h"

namespace {

// The fields the face uses. Asking only for these keeps the answer around one
// kilobyte, which is what makes it safe to hold in RAM on a shared TLS client.
constexpr const char *kCurrent =
    "temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
    "weather_code,wind_speed_10m";
constexpr const char *kDaily = "temperature_2m_max,temperature_2m_min";

bool finish(int written, char *out, size_t cap) {
  if (written > 0 && (size_t)written < cap) return true;
  out[0] = '\0';
  return false;
}

}  // namespace

bool weatherBuildForecastPath(float lat, float lon, char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  // Four decimals is about eleven metres: far beyond what weather needs, and a
  // fixed width keeps the query stable for the test.
  const int n = snprintf(out, cap,
                         "/v1/forecast?latitude=%.4f&longitude=%.4f"
                         "&current=%s&daily=%s&timezone=auto&forecast_days=1",
                         (double)lat, (double)lon, kCurrent, kDaily);
  return finish(n, out, cap);
}

bool weatherBuildGeocodePath(const char *query, char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  if (!query || !*query) return false;
  char encoded[WEATHER_PATH_MAX];
  if (!urlEncode(query, encoded, sizeof(encoded))) return false;
  const int n = snprintf(out, cap, "/v1/search?name=%s&count=5&language=es"
                                   "&format=json",
                         encoded);
  return finish(n, out, cap);
}

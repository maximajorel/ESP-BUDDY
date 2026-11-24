#include "weather_store.h"

#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

Preferences s_prefs;
bool s_ready = false;
// Cached because the service asks for the cadence every tick and NVS reads are
// not free.
int s_refreshMin = WEATHER_REFRESH_MIN_DEFAULT;

bool validLat(float lat) { return lat >= -90.0f && lat <= 90.0f; }
bool validLon(float lon) { return lon >= -180.0f && lon <= 180.0f; }

}  // namespace

void weatherStoreBegin() {
  s_refreshMin = WEATHER_REFRESH_MIN_DEFAULT;
  s_ready = s_prefs.begin("weather", false);
  if (!s_ready) return;
  int minutes = s_prefs.getInt("ref", WEATHER_REFRESH_MIN_DEFAULT);
  if (minutes >= WEATHER_REFRESH_MIN_MIN && minutes <= WEATHER_REFRESH_MIN_MAX) {
    s_refreshMin = minutes;
  }
}

bool weatherStoreLoad(WeatherPlace *out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
  if (!s_ready) return false;
  const float lat = s_prefs.getFloat("lat", 0.0f);
  const float lon = s_prefs.getFloat("lon", 0.0f);
  String name = s_prefs.getString("name", "");
  // A saved place always carries a name; coordinates alone would make the panel
  // show a blank line and the glass a nameless reading.
  if (name.isEmpty() || !validLat(lat) || !validLon(lon)) return false;
  out->valid = true;
  out->lat = lat;
  out->lon = lon;
  snprintf(out->name, sizeof(out->name), "%s", name.c_str());
  out->name[WEATHER_NAME_MAX] = '\0';
  return true;
}

bool weatherStoreSave(const WeatherPlace *place) {
  if (!place || !place->valid) return false;
  if (!validLat(place->lat) || !validLon(place->lon)) return false;
  const size_t len = strlen(place->name);
  if (len == 0 || len > WEATHER_NAME_MAX) return false;
  if (!s_ready) return false;
  s_prefs.putFloat("lat", place->lat);
  s_prefs.putFloat("lon", place->lon);
  s_prefs.putString("name", place->name);
  return true;
}

void weatherStoreForget() {
  if (!s_ready) return;
  s_prefs.remove("lat");
  s_prefs.remove("lon");
  s_prefs.remove("name");
}

int weatherStoreRefreshMin() { return s_refreshMin; }

bool weatherStoreSetRefreshMin(int minutes) {
  if (minutes < WEATHER_REFRESH_MIN_MIN || minutes > WEATHER_REFRESH_MIN_MAX) {
    return false;
  }
  s_refreshMin = minutes;
  if (s_ready) s_prefs.putInt("ref", minutes);
  return true;
}

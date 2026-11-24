#include "weather_parse.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace {

// Copies a JSON string into a fixed buffer, backing the cut off to the start of
// a UTF-8 sequence so a truncated name never renders as a broken glyph. The same
// rule the calendar uses for event summaries.
void copyField(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  dst[0] = '\0';
  if (!src) return;
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool coordinates(JsonVariantConst source, float *lat, float *lon) {
  JsonVariantConst la = source["latitude"];
  JsonVariantConst lo = source["longitude"];
  if (!la.is<float>() || !lo.is<float>()) return false;
  *lat = la.as<float>();
  *lon = lo.as<float>();
  return true;
}

}  // namespace

bool weatherParseNow(const char *json, WeatherNow *out) {
  if (!json || !out) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

  JsonObjectConst current = doc["current"].as<JsonObjectConst>();
  if (current.isNull()) return false;
  // Temperature and code are the two fields the face cannot invent. Everything
  // else has an honest default: no humidity reads 0 and no apparent
  // temperature reads as the real one.
  if (!current["temperature_2m"].is<float>()) return false;
  if (!current["weather_code"].is<int>()) return false;

  WeatherNow now;
  memset(&now, 0, sizeof(now));
  now.valid = true;
  now.tempC = current["temperature_2m"].as<float>();
  now.feelsC = current["apparent_temperature"].is<float>()
                   ? current["apparent_temperature"].as<float>()
                   : now.tempC;
  now.humidity = current["relative_humidity_2m"] | 0;
  now.windKph = current["wind_speed_10m"] | 0.0f;
  now.code = current["weather_code"].as<int>();
  now.isDay = (current["is_day"] | 1) != 0;

  // The daily block is optional: without it the bar shows the current value in
  // both ends rather than a made-up range.
  JsonArrayConst highs = doc["daily"]["temperature_2m_max"].as<JsonArrayConst>();
  JsonArrayConst lows = doc["daily"]["temperature_2m_min"].as<JsonArrayConst>();
  now.tmaxC = (highs.size() > 0 && highs[0].is<float>()) ? highs[0].as<float>()
                                                         : now.tempC;
  now.tminC = (lows.size() > 0 && lows[0].is<float>()) ? lows[0].as<float>()
                                                       : now.tempC;
  *out = now;
  return true;
}

size_t weatherParseGeocode(const char *json, WeatherGeoResult *out, size_t cap) {
  if (!json || !out || cap == 0) return 0;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return 0;
  JsonArrayConst results = doc["results"].as<JsonArrayConst>();
  if (results.isNull()) return 0;

  size_t n = 0;
  for (JsonVariantConst item : results) {
    if (n >= cap) break;
    const char *name = item["name"] | "";
    float lat = 0.0f, lon = 0.0f;
    if (!*name || !coordinates(item, &lat, &lon)) continue;
    copyField(out[n].name, sizeof(out[n].name), name);
    copyField(out[n].region, sizeof(out[n].region), item["admin1"] | "");
    copyField(out[n].country, sizeof(out[n].country), item["country_code"] | "");
    out[n].lat = lat;
    out[n].lon = lon;
    n++;
  }
  return n;
}

bool weatherParseIpLocation(const char *json, WeatherPlace *out) {
  if (!json || !out) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  if (!(doc["success"] | false)) return false;
  const char *city = doc["city"] | "";
  const char *country = doc["country_code"] | "";
  float lat = 0.0f, lon = 0.0f;
  if (!*city || !coordinates(doc, &lat, &lon)) return false;

  WeatherPlace place;
  memset(&place, 0, sizeof(place));
  place.valid = true;
  place.lat = lat;
  place.lon = lon;
  if (*country) {
    snprintf(place.name, sizeof(place.name), "%s, %s", city, country);
  } else {
    snprintf(place.name, sizeof(place.name), "%s", city);
  }
  *out = place;
  return true;
}

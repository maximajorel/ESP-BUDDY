// Host-side test for the pure weather rules: WMO codes, the Open-Meteo forecast
// parse, the geocoding parse, the IP-location parse and the exact request paths.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/weather_core_test.cpp \
//       esp32_gadget/weather_code.cpp esp32_gadget/weather_parse.cpp \
//       esp32_gadget/weather_geocode.cpp esp32_gadget/weather_url.cpp \
//       esp32_gadget/url_codec.cpp -I <ArduinoJson/src> -o /tmp/weather && /tmp/weather
//
// These rules decide what the glass says ("Lluvia", not a number) and where the
// request goes. Pinning the exact bytes means a change in the query is seen here,
// not in a packet trace after the panel says "sin lectura".

#include <cstdio>
#include <cstring>

#include "weather_code.h"
#include "weather_geocode.h"
#include "weather_model.h"
#include "weather_parse.h"
#include "weather_url.h"

static int g_failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void checkText(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-46s\n     got  \"%s\"\n     want \"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-46s \"%s\"\n", what, got);
  }
}

static void checkFloat(const char *what, float got, float want) {
  const float diff = got > want ? got - want : want - got;
  if (diff > 0.001f) {
    printf("FAIL %-46s got=%.3f want=%.3f\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-46s %.3f\n", what, got);
  }
}

static void checkInt(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-46s got=%d want=%d\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-46s %d\n", what, got);
  }
}

static const char *const kForecast =
    "{\"latitude\":-26.8241,\"longitude\":-65.2226,"
    "\"timezone\":\"America/Argentina/Tucuman\","
    "\"current\":{\"time\":\"2026-11-22T12:00\","
    "\"temperature_2m\":28.4,\"relative_humidity_2m\":54,"
    "\"apparent_temperature\":29.9,\"is_day\":1,\"weather_code\":2,"
    "\"wind_speed_10m\":11.3},"
    "\"daily\":{\"time\":[\"2026-11-22\"],"
    "\"temperature_2m_max\":[33.1],\"temperature_2m_min\":[18.6]}}";

static const char *const kGeocode =
    "{\"results\":[{\"id\":3833578,\"name\":\"Tucuman\",\"latitude\":-26.8241,"
    "\"longitude\":-65.2226,\"country\":\"Argentina\",\"country_code\":\"AR\","
    "\"admin1\":\"Tucuman\"}]}";

static const char *const kIp =
    "{\"ip\":\"181.1.2.3\",\"success\":true,"
    "\"city\":\"San Miguel de Tucuman\",\"region\":\"Tucuman\","
    "\"country\":\"Argentina\",\"country_code\":\"AR\","
    "\"latitude\":-26.83,\"longitude\":-65.22}";

int main() {
  printf("-- WMO codes become a Spanish word and one icon\n");
  checkText("0 despejado", weatherCodeLabel(0), "Despejado");
  checkText("2 parcialmente nublado", weatherCodeLabel(2), "Parcialmente nublado");
  checkText("3 nublado", weatherCodeLabel(3), "Nublado");
  checkText("45 niebla", weatherCodeLabel(45), "Niebla");
  checkText("51 llovizna", weatherCodeLabel(51), "Llovizna leve");
  checkText("61 lluvia", weatherCodeLabel(61), "Lluvia leve");
  checkText("71 nieve", weatherCodeLabel(71), "Nieve leve");
  checkText("80 chaparrones", weatherCodeLabel(80), "Chaparrones leves");
  checkText("95 tormenta", weatherCodeLabel(95), "Tormenta");
  checkText("unknown code admits it", weatherCodeLabel(42), "Sin datos");
  checkInt("0 is clear", weatherCodeIcon(0), WEATHER_ICON_CLEAR);
  checkInt("2 is partly", weatherCodeIcon(2), WEATHER_ICON_PARTLY);
  checkInt("3 is cloudy", weatherCodeIcon(3), WEATHER_ICON_CLOUDY);
  checkInt("45 is fog", weatherCodeIcon(45), WEATHER_ICON_FOG);
  checkInt("55 is drizzle", weatherCodeIcon(55), WEATHER_ICON_DRIZZLE);
  checkInt("65 is rain", weatherCodeIcon(65), WEATHER_ICON_RAIN);
  checkInt("75 is snow", weatherCodeIcon(75), WEATHER_ICON_SNOW);
  checkInt("95 is storm", weatherCodeIcon(95), WEATHER_ICON_STORM);
  checkInt("unknown icon", weatherCodeIcon(42), WEATHER_ICON_UNKNOWN);

  printf("-- the current reading parses into the snapshot\n");
  WeatherNow now;
  memset(&now, 0, sizeof(now));
  check(weatherParseNow(kForecast, &now), "forecast parses");
  check(now.valid, "snapshot is valid");
  checkFloat("temperature", now.tempC, 28.4f);
  checkFloat("feels like", now.feelsC, 29.9f);
  checkInt("humidity", now.humidity, 54);
  checkFloat("wind", now.windKph, 11.3f);
  checkInt("code", now.code, 2);
  check(now.isDay, "daylight flag");
  checkFloat("daily max", now.tmaxC, 33.1f);
  checkFloat("daily min", now.tminC, 18.6f);

  printf("-- unusable answers are refused, not invented\n");
  WeatherNow bad;
  check(!weatherParseNow("not json", &bad), "garbage is refused");
  check(!weatherParseNow("{}", &bad), "missing current is refused");
  check(!weatherParseNow(
            "{\"current\":{\"temperature_2m\":20}}", &bad),
        "missing weather code is refused");
  check(!weatherParseNow(nullptr, &bad), "null is refused");

  printf("-- geocoding lists candidate places\n");
  WeatherGeoResult places[WEATHER_GEOCODE_MAX];
  const size_t count = weatherParseGeocode(kGeocode, places, WEATHER_GEOCODE_MAX);
  checkInt("one result", (int)count, 1);
  checkText("name", places[0].name, "Tucuman");
  checkText("region", places[0].region, "Tucuman");
  checkText("country code", places[0].country, "AR");
  checkFloat("lat", places[0].lat, -26.8241f);
  checkFloat("lon", places[0].lon, -65.2226f);
  checkInt("empty results array", (int)weatherParseGeocode("{\"results\":[]}",
                                                           places, 5),
           0);
  checkInt("garbage", (int)weatherParseGeocode("nope", places, 5), 0);

  printf("-- an IP lookup names the place it found\n");
  WeatherPlace place;
  memset(&place, 0, sizeof(place));
  check(weatherParseIpLocation(kIp, &place), "ip location parses");
  check(place.valid, "place is valid");
  checkText("city and country", place.name, "San Miguel de Tucuman, AR");
  checkFloat("lat", place.lat, -26.83f);
  checkFloat("lon", place.lon, -65.22f);
  check(!weatherParseIpLocation("{\"success\":false}", &place),
        "failed lookup is refused");
  check(!weatherParseIpLocation(nullptr, &place), "null lookup is refused");

  printf("-- the request paths are exact\n");
  char path[WEATHER_PATH_MAX];
  check(weatherBuildForecastPath(-26.8241f, -65.2226f, path, sizeof(path)),
        "forecast path builds");
  checkText("forecast path", path,
            "/v1/forecast?latitude=-26.8241&longitude=-65.2226"
            "&current=temperature_2m,relative_humidity_2m,"
            "apparent_temperature,is_day,weather_code,wind_speed_10m"
            "&daily=temperature_2m_max,temperature_2m_min"
            "&timezone=auto&forecast_days=1");
  check(weatherBuildGeocodePath("San Miguel de Tucuman", path, sizeof(path)),
        "geocode path builds");
  checkText("geocode path", path,
            "/v1/search?name=San%20Miguel%20de%20Tucuman"
            "&count=5&language=es&format=json");
  check(!weatherBuildGeocodePath("", path, sizeof(path)),
        "empty query is refused");
  char tiny[12];
  tiny[0] = 'x';
  check(!weatherBuildForecastPath(-26.8241f, -65.2226f, tiny, sizeof(tiny)),
        "small buffer is refused");
  checkText("small buffer is left empty", tiny, "");

  if (g_failures == 0) {
    printf("all weather checks passed\n");
    return 0;
  }
  printf("%d weather checks FAILED\n", g_failures);
  return 1;
}

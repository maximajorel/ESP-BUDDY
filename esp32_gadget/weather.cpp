#include "weather.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "weather_http.h"
#include "weather_parse.h"
#include "weather_store.h"
#include "weather_url.h"

namespace {

// The forecast answer is around one kilobyte; four is generous and still refuses
// a captive portal answering with megabytes.
constexpr size_t kMaxBody = 4096;
// Retry a failed fetch soon, but not on every tick: a dead network must not turn
// into a TLS handshake every second.
constexpr uint32_t kRetryMs = 60u * 1000u;
constexpr uint32_t kTaskStack = 12288;

SemaphoreHandle_t s_lock = nullptr;
TaskHandle_t s_task = nullptr;

WeatherPlace s_place;
WeatherNow s_now;
bool s_hasPlace = false;
bool s_hasData = false;
bool s_syncing = false;
bool s_lastOk = false;
bool s_attempted = false;
uint32_t s_lastAttemptMs = 0;
uint32_t s_lastSuccessMs = 0;
char s_lastError[96] = "";
volatile bool s_refreshRequested = false;

void lock() {
  if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

void unlock() {
  if (s_lock) xSemaphoreGive(s_lock);
}

void fail(const char *text) {
  lock();
  snprintf(s_lastError, sizeof(s_lastError), "%s", text ? text : "");
  s_syncing = false;
  s_lastOk = false;
  unlock();
}

void storePlaceState(const WeatherPlace &place, bool valid) {
  lock();
  if (valid) {
    s_place = place;
    s_hasPlace = true;
  } else {
    memset(&s_place, 0, sizeof(s_place));
    s_hasPlace = false;
    s_hasData = false;
    memset(&s_now, 0, sizeof(s_now));
  }
  unlock();
}

void fetchNow(const WeatherPlace &place) {
  lock();
  s_syncing = true;
  s_attempted = true;
  s_lastAttemptMs = millis();
  unlock();

  char path[WEATHER_PATH_MAX];
  if (!weatherBuildForecastPath(place.lat, place.lon, path, sizeof(path))) {
    fail("Las coordenadas guardadas no son validas");
    return;
  }
  WeatherHttpResult res;
  if (!weatherHttpGet(WEATHER_HOST, path, kMaxBody, &res)) {
    fail("No pude iniciar la consulta del clima");
    return;
  }
  if (res.transportError == WEATHER_HTTP_BODY_TOO_LARGE) {
    fail("La respuesta del clima fue demasiado grande");
    return;
  }
  if (res.status != 200) {
    char msg[96];
    snprintf(msg, sizeof(msg), "El clima respondio %d", res.status);
    fail(msg);
    return;
  }
  WeatherNow now;
  if (!weatherParseNow(res.body.c_str(), &now)) {
    fail("La respuesta del clima no se entendio");
    return;
  }
  lock();
  s_now = now;
  s_hasData = true;
  s_lastOk = true;
  s_lastSuccessMs = millis();
  s_lastError[0] = '\0';
  s_syncing = false;
  unlock();
}

void taskLoop(void *) {
  for (;;) {
    weatherTick();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace

void weatherBegin() {
  if (s_lock) return;
  s_lock = xSemaphoreCreateMutex();
  weatherStoreBegin();
  weatherHttpBegin();
  WeatherPlace place;
  const bool saved = weatherStoreLoad(&place);
  storePlaceState(place, saved);
  xTaskCreatePinnedToCore(taskLoop, "weather", kTaskStack, nullptr, 1, &s_task,
                          1);
}

void weatherTick() {
  // The place changes only through this module's own actions (save/detect/
  // forget), which update the cached state under the lock. Re-reading NVS every
  // second would be pointless traffic.
  WeatherPlace place;
  lock();
  const bool configured = s_hasPlace;
  place = s_place;
  unlock();
  if (!configured) return;

  const bool refresh = s_refreshRequested;
  s_refreshRequested = false;
  const uint32_t now = millis();
  if (!refresh && s_attempted) {
    const uint32_t waitMs =
        s_lastOk ? (uint32_t)weatherStoreRefreshMin() * 60000u : kRetryMs;
    if ((uint32_t)(now - s_lastAttemptMs) < waitMs) return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    lock();
    const bool hasData = s_hasData;
    unlock();
    if (!hasData) fail("Conecta el equipo al WiFi para ver el clima");
    return;
  }
  fetchNow(place);
}

WeatherStatusInfo weatherStatusInfo() {
  WeatherStatusInfo info;
  memset(&info, 0, sizeof(info));
  lock();
  info.place = s_place;
  info.configured = s_hasPlace;
  info.hasData = s_hasData;
  info.now = s_now;
  info.syncing = s_syncing;
  info.lastAttemptMs = s_lastAttemptMs;
  info.lastSuccessMs = s_lastSuccessMs;
  info.refreshMin = weatherStoreRefreshMin();
  snprintf(info.lastError, sizeof(info.lastError), "%s", s_lastError);
  unlock();

  if (!info.configured) {
    info.kind = WEATHER_UNCONFIGURED;
  } else if (info.hasData) {
    info.kind = WEATHER_READY;
  } else if (info.lastError[0]) {
    info.kind = WEATHER_ERROR;
  } else {
    info.kind = WEATHER_NO_DATA;
  }
  return info;
}

bool weatherRequestRefresh() {
  WeatherPlace place;
  if (!weatherStoreLoad(&place)) return false;
  s_refreshRequested = true;
  return true;
}

bool weatherSavePlace(float lat, float lon, const char *name, char *err,
                      size_t errCap) {
  WeatherPlace place;
  memset(&place, 0, sizeof(place));
  if (!name || !*name) {
    if (err) snprintf(err, errCap, "Falta el nombre del lugar");
    return false;
  }
  // Reject, do not silently truncate: the mock and the panel both treat a name
  // over the limit as a client error, and a truncated name is a different place
  // the owner never chose.
  if (strlen(name) > WEATHER_NAME_MAX) {
    if (err) snprintf(err, errCap, "El nombre no puede superar los 48 caracteres");
    return false;
  }
  place.valid = true;
  place.lat = lat;
  place.lon = lon;
  snprintf(place.name, sizeof(place.name), "%s", name);
  if (!weatherStoreSave(&place)) {
    if (err) snprintf(err, errCap, "Coordenadas o nombre invalidos");
    return false;
  }
  storePlaceState(place, true);
  lock();
  s_lastError[0] = '\0';
  unlock();
  s_refreshRequested = true;
  return true;
}

bool weatherDetectPlace(char *err, size_t errCap) {
  if (WiFi.status() != WL_CONNECTED) {
    if (err) snprintf(err, errCap, "Conecta el equipo a tu WiFi primero");
    return false;
  }
  WeatherHttpResult res;
  if (!weatherHttpGet(WEATHER_IP_HOST, "/", kMaxBody, &res)) {
    if (err) snprintf(err, errCap, "No pude consultar tu ubicacion");
    return false;
  }
  if (res.status != 200) {
    if (err) snprintf(err, errCap, "La deteccion respondio %d", res.status);
    return false;
  }
  WeatherPlace place;
  if (!weatherParseIpLocation(res.body.c_str(), &place)) {
    if (err) snprintf(err, errCap, "No pude leer la ubicacion detectada");
    return false;
  }
  if (!weatherStoreSave(&place)) {
    if (err) snprintf(err, errCap, "No pude guardar la ubicacion detectada");
    return false;
  }
  storePlaceState(place, true);
  lock();
  s_lastError[0] = '\0';
  unlock();
  s_refreshRequested = true;
  return true;
}

size_t weatherSearch(const char *query, WeatherGeoResult *out, size_t cap,
                     char *err, size_t errCap) {
  if (!query || !*query) {
    if (err) snprintf(err, errCap, "Escribi una ciudad para buscar");
    return 0;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (err) snprintf(err, errCap, "Conecta el equipo a tu WiFi primero");
    return 0;
  }
  char path[WEATHER_PATH_MAX];
  if (!weatherBuildGeocodePath(query, path, sizeof(path))) {
    if (err) snprintf(err, errCap, "La busqueda es demasiado larga");
    return 0;
  }
  WeatherHttpResult res;
  if (!weatherHttpGet(WEATHER_GEOCODE_HOST, path, kMaxBody, &res)) {
    if (err) snprintf(err, errCap, "No pude buscar la ciudad");
    return 0;
  }
  if (res.status != 200) {
    if (err) snprintf(err, errCap, "La busqueda respondio %d", res.status);
    return 0;
  }
  const size_t count = weatherParseGeocode(res.body.c_str(), out, cap);
  if (count == 0 && err) snprintf(err, errCap, "No encontre esa ciudad");
  return count;
}

void weatherForget() {
  weatherStoreForget();
  storePlaceState(s_place, false);
  lock();
  s_lastError[0] = '\0';
  s_attempted = false;
  s_lastOk = false;
  unlock();
}

bool weatherSetRefreshMin(int minutes) {
  return weatherStoreSetRefreshMin(minutes);
}

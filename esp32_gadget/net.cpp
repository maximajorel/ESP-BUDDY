#include "net.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "console.h"
#include "display.h"
#include "rgb.h"
#include "screen_buddy.h"
#include "screen_sleep.h"
#include "screens.h"
#include "settings.h"
#include "spotify.h"
#include "spotify_auth.h"
#include "spotify_store.h"
#include "ui_color.h"
#include "web_assets.h"

#define FIRMWARE_NAME "esp32-gadget"
#define FIRMWARE_VERSION "1.0.0"

// Re-issue the association attempt this rarely. A WPA2 handshake plus DHCP can
// legitimately take longer than a few seconds on a busy router, so a short
// retry turns into a storm that aborts each attempt before it can finish.
#define STA_RETRY_MS 60000

static WebServer s_server(80);
static NetStatus s_status;
static uint32_t s_lastStaAttempt = 0;
// When the current attempt started, so "still trying" can turn into "it failed".
static uint32_t s_staSince = 0;
static volatile int s_disconnectReason = 0;
#define STA_FAIL_HINT_MS 25000
static char s_lastError[96] = "";

const NetStatus &netStatus() { return s_status; }
const char *netLastError() { return s_lastError; }

static void setError(const char *text) {
  snprintf(s_lastError, sizeof(s_lastError), "%s", text ? text : "");
}

static void refreshStatus() {
  s_status.apActive = (WiFi.getMode() & WIFI_AP) != 0;
  s_status.staConfigured = settingsHasSta();
  s_status.staConnected = WiFi.status() == WL_CONNECTED;
  // The raw lwIP/WiFi status code is what distinguishes "wrong password" from
  // "the network is not there"; without it a failure is undiagnosable.
  s_status.wifiStatusCode = (int)WiFi.status();
  s_status.disconnectReason = s_disconnectReason;

  if (!s_status.staConfigured) {
    s_status.staState = NET_STA_IDLE;
  } else if (s_status.staConnected) {
    s_status.staState = NET_STA_OK;
  } else if ((uint32_t)(millis() - s_staSince) > STA_FAIL_HINT_MS) {
    s_status.staState = NET_STA_FAILED;
  } else {
    s_status.staState = NET_STA_CONNECTING;
  }

  if (s_status.staConnected) {
    snprintf(s_status.staIp, sizeof(s_status.staIp), "%s",
             WiFi.localIP().toString().c_str());
    s_status.staRssi = WiFi.RSSI();
    // Report the network actually joined, which may differ from the stored name
    // if the router was renamed.
    snprintf(s_status.staSsid, sizeof(s_status.staSsid), "%s",
             WiFi.SSID().c_str());
  } else {
    snprintf(s_status.staIp, sizeof(s_status.staIp), "%s", "-");
    s_status.staRssi = 0;
    snprintf(s_status.staSsid, sizeof(s_status.staSsid), "%s",
             settingsGet().staSsid);
  }

  snprintf(s_status.apSsid, sizeof(s_status.apSsid), "%s",
           settingsGet().apSsid);
  s_status.apClients = WiFi.softAPgetStationNum();
  s_status.timeSynced = time(nullptr) > 1600000000;
}

// --- time -------------------------------------------------------------------

void netApplyTimeZone() {
  int8_t hours = settingsGet().tzOffsetHours;
  configTime((long)hours * 3600L, 0, "pool.ntp.org", "time.google.com");
}

// --- API --------------------------------------------------------------------
//
// The HTTP layer is a thin transport: every endpoint is a pure function of
// (method, path, body) that returns a code and a body string. That keeps the
// behaviour testable from the serial console (`api GET /api/status`), which
// matters because the panel runs on its own access point and is not always
// reachable from a development machine.

static ApiResult apiFail(int code, const char *message) {
  ApiResult r;
  r.code = code;
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  serializeJson(doc, r.body);
  return r;
}

static ApiResult apiOk() {
  ApiResult r;
  r.code = 200;
  r.body = "{\"ok\":true}";
  return r;
}

static void buildStatusJson(String &out) {
  refreshStatus();
  const Settings &cfg = settingsGet();
  NetStatus &st = s_status;

  JsonDocument doc;
  doc["firmware"] = FIRMWARE_NAME;
  doc["version"] = FIRMWARE_VERSION;
  doc["uptimeMs"] = (uint32_t)millis();
  doc["heapFree"] = (uint32_t)ESP.getFreeHeap();
  doc["psramFree"] = (uint32_t)ESP.getFreePsram();
  doc["screen"] = screensCurrent();
  doc["screenName"] = screenName(screensCurrent());
  {
    uint8_t order[SCREEN_COUNT];
    screensGetOrder(order);
    JsonArray arr = doc["order"].to<JsonArray>();
    for (uint8_t i = 0; i < SCREEN_COUNT; i++) arr.add(order[i]);
  }
  {
    // Stored names, accents included, so the panel edits what it saved instead
    // of the folded form the glass shows.
    JsonArray arr = doc["names"].to<JsonArray>();
    for (uint8_t i = 0; i < SCREEN_COUNT; i++) arr.add(screenName(i));
  }
  doc["imuOk"] = g_imuOk;

  JsonObject sta = doc["sta"].to<JsonObject>();
  sta["configured"] = st.staConfigured;
  sta["connected"] = st.staConnected;
  sta["ssid"] = st.staSsid;
  sta["ip"] = st.staIp;
  sta["rssi"] = st.staRssi;
  sta["state"] = st.staState == NET_STA_OK         ? "ok"
                 : st.staState == NET_STA_FAILED   ? "failed"
                 : st.staState == NET_STA_IDLE     ? "idle"
                                                   : "connecting";
  sta["wifiStatusCode"] = st.wifiStatusCode;
  // Printed so an owner whose router filters by MAC can allow the device
  // without hunting through the router's client list.
  sta["mac"] = WiFi.macAddress();
  sta["disconnectReason"] = st.disconnectReason;
  JsonObject ap = doc["ap"].to<JsonObject>();
  ap["active"] = st.apActive;
  ap["error"] = s_lastError;
  ap["ssid"] = st.apSsid;
  // The access point password is published deliberately: it is printed on the
  // device screen on purpose, because the buyer has to read it to connect.
  ap["password"] = cfg.apPass;
  ap["ip"] = NET_AP_IP;
  ap["clients"] = st.apClients;

  JsonObject t = doc["time"].to<JsonObject>();
  t["synced"] = st.timeSynced;
  t["tz"] = (int)cfg.tzOffsetHours;
  t["clock24h"] = cfg.clock24h;
  char stamp[24] = "--";
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmNow);
  }
  t["local"] = stamp;

  JsonObject disp = doc["display"].to<JsonObject>();
  disp["brightness"] = displayGetBacklight();
  disp["orientation"] = displayGetOrientation();
  disp["awake"] = displayIsAwake();
  disp["timeoutSec"] = (int)settingsGetScreenTimeout().seconds;
  disp["timeoutMask"] = (int)settingsGetScreenTimeout().mask;
  {
    char uiHex[8];
    uiColorToHex(settingsGetUiColor(), uiHex);
    disp["uiColor"] = uiHex;
  }

  {
    BuddyView buddy;
    buddyGetView(&buddy);
    char url[SETTINGS_BUDDY_URL_MAX + 1];
    settingsGetBuddyUrl(url, sizeof(url));
    JsonObject obj = doc["buddy"].to<JsonObject>();
    obj["configured"] = buddy.configured;
    obj["online"] = buddy.online;
    obj["state"] = buddyStateWireName(buddy.state);
    obj["label"] = buddy.label;
    obj["active"] = buddy.active;
    obj["phase"] = buddyPhaseWireName(buddy.phase);
    {
      JsonArray led = obj["led"].to<JsonArray>();
      led.add(buddy.led.r);
      led.add(buddy.led.g);
      led.add(buddy.led.b);
    }
    obj["ageMs"] = buddy.ageMs;
    obj["stateAgeMs"] = buddy.stateAgeMs;
    obj["url"] = url;
    // The token is never echoed back: the panel only says whether it is set.
  }

  JsonObject rgbObj = doc["rgb"].to<JsonObject>();
  uint8_t r, g, b;
  rgbGetColor(r, g, b);
  rgbObj["asleep"] = rgbIsSleeping();
  rgbObj["r"] = r;
  rgbObj["g"] = g;
  rgbObj["b"] = b;
  rgbObj["level"] = rgbLevel();
  rgbObj["effect"] = rgbEffect();

  uint8_t steerLat, steerUp, steerGain;
  int8_t steerSign, steerSignY;
  gameGetSteer(&steerLat, &steerUp, &steerSign, &steerSignY, &steerGain);
  JsonObject steer = doc["steer"].to<JsonObject>();
  steer["sign"] = steerSign;
  steer["signY"] = steerSignY;
  steer["latAxis"] = steerLat;
  steer["upAxis"] = steerUp;
  steer["calibrated"] = settingsGetSteerCalibration().valid;

  JsonObject scores = doc["scores"].to<JsonObject>();
  scores["game"] = cfg.gameHighScore;
  scores["slot"] = cfg.slotHighScore;

  serializeJson(doc, out);
}

static ApiResult apiWifi(const String &body) {
  JsonDocument doc;
  if (body.length() && deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  const char *ssid = doc["ssid"] | "";
  const char *pass = doc["pass"] | "";

  if (!ssid[0]) return apiFail(400, "El nombre de la red no puede estar vacio");
  if (!netSetSta(ssid, pass)) return apiFail(400, netLastError());
  return apiOk();
}

static ApiResult apiAp(const String &body) {
  JsonDocument doc;
  if (body.length() && deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  const char *ssid = doc["ssid"] | "";
  const char *pass = doc["pass"] | "";
  if (!netSetAp(ssid, pass)) return apiFail(400, netLastError());
  return apiOk();
}

// Fallback for networks that block NTP (port 123) or do not resolve names: the
// panel is already open on a device that knows the time, so a single tap sets
// the clock. The epoch is validated so a broken client cannot send the device
// back to 1970.
static ApiResult apiSetTime(const String &body) {
  JsonDocument doc;
  if (body.length() && deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  if (!doc["epoch"].is<long long>()) {
    return apiFail(400, "Falta la hora en formato epoch");
  }
  long long epoch = doc["epoch"].as<long long>();
  if (epoch < 1600000000LL || epoch > 2200000000LL) {
    return apiFail(400, "La hora enviada esta fuera de rango");
  }

  struct timeval tv;
  tv.tv_sec = (time_t)epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  refreshStatus();
  screensGoTo(screensCurrent());  // repaint the clock face right away
  return apiOk();
}

static ApiResult apiSettings(const String &body) {
  JsonDocument doc;
  if (body.length() && deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }

  Settings &cfg = settingsMutable();
  bool tzChanged = false;

  if (doc["brightness"].is<int>()) {
    int value = doc["brightness"];
    if (value < 5) value = 5;
    if (value > 100) value = 100;
    cfg.brightness = (uint8_t)value;
    displaySetBacklight(cfg.brightness);
  }
  if (doc["tz"].is<int>()) {
    int value = doc["tz"];
    if (value < -12 || value > 14) return apiFail(400, "Zona horaria invalida");
    if (value != cfg.tzOffsetHours) {
      cfg.tzOffsetHours = (int8_t)value;
      tzChanged = true;
    }
  }
  if (doc["clock24h"].is<bool>()) cfg.clock24h = doc["clock24h"];

  if (!doc["uiColor"].isNull()) {
    uint32_t rgb = 0;
    if (!doc["uiColor"].is<const char*>() ||
        !uiColorParse(doc["uiColor"] | "", &rgb)) {
      return apiFail(400, "Color de interfaz invalido");
    }
    if (!settingsSetUiColor(rgb)) {
      return apiFail(500, "No pude guardar el color");
    }
  }

  if (!doc["buddyUrl"].isNull()) {
    const char *url = doc["buddyUrl"] | "";
    if (!settingsSetBuddyUrl(url)) return apiFail(400, "URL del buddy invalida");
  }
  if (!doc["buddyToken"].isNull()) {
    const char *token = doc["buddyToken"] | "";
    if (!settingsSetBuddyToken(token)) return apiFail(400, "Token del buddy invalido");
  }

  if (doc["screenTimeoutSec"].is<int>() || doc["screenTimeoutMask"].is<int>()) {
    ScreenTimeout timeout = settingsGetScreenTimeout();
    if (doc["screenTimeoutSec"].is<int>()) {
      int seconds = doc["screenTimeoutSec"];
      if (seconds < 0 || seconds > (int)SCREEN_SLEEP_MAX_SEC) {
        return apiFail(400, "Tiempo de apagado fuera de rango");
      }
      timeout.seconds = (uint16_t)seconds;
    }
    if (doc["screenTimeoutMask"].is<int>()) {
      int mask = doc["screenTimeoutMask"];
      if (mask < 0 || mask > 0xFF) {
        return apiFail(400, "Mascara de pantallas invalida");
      }
      timeout.mask = (uint8_t)mask;
    }
    settingsSetScreenTimeout(timeout);
  }

  if (doc["steerSign"].is<int>() || doc["steerSignY"].is<int>()) {
    uint8_t lat, up, gain;
    int8_t sign, signY;
    gameGetSteer(&lat, &up, &sign, &signY, &gain);
    if (doc["steerSign"].is<int>()) sign = doc["steerSign"] < 0 ? -1 : 1;
    if (doc["steerSignY"].is<int>()) signY = doc["steerSignY"] < 0 ? -1 : 1;
    gameSetSteer(lat, up, sign, signY, gain);
  }

  // Orientation is reported separately because it changes how the finished
  // image is rotated onto the panel, not what the screens draw.
  bool orientationChanged = false;
  if (doc["orientation"].is<int>()) {
    int value = doc["orientation"];
    if (value < 0 || value > 3) {
      return apiFail(400, "Orientacion de pantalla invalida");
    }
    if (value != displayGetOrientation()) {
      displaySetOrientation((uint8_t)value);
      settingsSetOrientation((uint8_t)value);
      orientationChanged = true;
    }
  }
  (void)orientationChanged;

  if (doc["rgb"].is<JsonObject>()) {
    JsonObject rgbIn = doc["rgb"];
    if (rgbIn["r"].is<int>() && rgbIn["g"].is<int>() && rgbIn["b"].is<int>()) {
      rgbSetColor((uint8_t)(int)rgbIn["r"], (uint8_t)(int)rgbIn["g"],
                  (uint8_t)(int)rgbIn["b"]);
    }
    if (rgbIn["level"].is<int>()) {
      int value = rgbIn["level"];
      if (value < 0) value = 0;
      if (value > 255) value = 255;
      rgbSetLevel((uint8_t)value);
    }
    if (rgbIn["effect"].is<int>()) {
      int value = rgbIn["effect"];
      if (value >= 0 && value <= 2) rgbSetEffect((uint8_t)value);
    }
  }

  settingsSave();
  if (tzChanged) netApplyTimeZone();
  screensGoTo(screensCurrent());  // brightness/format changes need a repaint
  return apiOk();
}

static ApiResult apiScreen(const String &body) {
  JsonDocument doc;
  if (body.length() && deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  if (!doc["index"].is<int>()) return apiFail(400, "Falta el indice de pantalla");
  int index = doc["index"];
  if (index < 0 || index >= SCREEN_COUNT) {
    return apiFail(400, "Indice de pantalla fuera de rango");
  }
  screensGoTo((uint8_t)index);
  return apiOk();
}

static ApiResult apiPress(const String &body) {
  JsonDocument doc;
  if (body.length() && deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  bool longPress = doc["long"] | false;
  if (longPress) {
    screensLongPress();
  } else {
    screensShortPress();
  }
  return apiOk();
}

// A scan used to disconnect the station first, wait, then reconnect. That dance
// was the bug behind "Buscar redes" freezing the panel: it blocked the whole web
// server for ~7 s and dropped the very socket the panel was using (HTTP 000).
//
// The sweep is asynchronous now and never touches the station link. The contract
// is explicit so two readers cannot eat each other's results: POST starts a
// sweep, GET reads its state.
static uint32_t s_scanStartedMs = 0;
#define NET_SCAN_TIMEOUT_MS 12000

static void scanJsonState(JsonDocument &doc, int16_t state) {
  doc["ok"] = true;
  if (state == WIFI_SCAN_RUNNING) {
    doc["scanning"] = true;
    doc["count"] = -1;
    doc["networks"].to<JsonArray>();
    return;
  }
  if (state < 0) {
    // Never started, or the last sweep failed. Both mean "no list right now";
    // POST is what asks for one.
    doc["scanning"] = false;
    doc["count"] = 0;
    doc["networks"].to<JsonArray>();
    return;
  }
  doc["scanning"] = false;
  doc["count"] = state;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (int i = 0; i < state && i < 20; i++) {
    JsonObject net = list.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    // wifi_auth_mode_t: 2 WPA, 3 WPA2, 4 WPA/WPA2, 6 WPA3, 7 WPA2/WPA3. A
    // WPA3-only network cannot be joined by this chip, and telling that apart
    // from a bad password is the whole point of this field.
    net["auth"] = (int)WiFi.encryptionType(i);
  }
}

static ApiResult apiScanRead() {
  ApiResult r;
  JsonDocument doc;
  int16_t state = WiFi.scanComplete();
  if (state == WIFI_SCAN_RUNNING &&
      (uint32_t)(millis() - s_scanStartedMs) > NET_SCAN_TIMEOUT_MS) {
    // A sweep that never ends would leave the panel spinning forever.
    WiFi.scanDelete();
    state = WIFI_SCAN_FAILED;
  }
  scanJsonState(doc, state);
  serializeJson(doc, r.body);
  return r;
}

static ApiResult apiScanStart() {
  ApiResult r;
  JsonDocument doc;
  int16_t state = WiFi.scanComplete();
  if (state == WIFI_SCAN_RUNNING) {
    // Already sweeping: report it instead of stacking requests.
    scanJsonState(doc, state);
    serializeJson(doc, r.body);
    return r;
  }
  WiFi.scanDelete();
  int16_t started = WiFi.scanNetworks(true /*async*/, true /*showHidden*/,
                                     false /*passive*/, 120 /*ms per channel*/);
  if (started == WIFI_SCAN_FAILED) {
    doc["ok"] = false;
    doc["scanning"] = false;
    doc["error"] = "El equipo no pudo escanear ahora mismo. Reintenta en unos segundos.";
    serializeJson(doc, r.body);
    r.code = 503;
    return r;
  }
  s_scanStartedMs = millis();
  doc["ok"] = true;
  doc["scanning"] = true;
  doc["count"] = -1;
  doc["networks"].to<JsonArray>();
  serializeJson(doc, r.body);
  return r;
}

// --- screens ----------------------------------------------------------------

static ApiResult apiScreenOrder(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  JsonArray in = doc["order"];
  if (in.size() != SCREEN_COUNT) {
    return apiFail(400, "El orden tiene que listar todas las pantallas");
  }
  uint8_t order[SCREEN_COUNT];
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    int value = in[i] | -1;
    if (value < 0 || value >= SCREEN_COUNT) {
      return apiFail(400, "Pantalla fuera de rango");
    }
    order[i] = (uint8_t)value;
  }
  if (!screensSetOrder(order)) {
    return apiFail(400, "El orden repite o pierde pantallas");
  }
  return apiOk();
}

// Renaming all screens at once, not one at a time: the panel saves a draft the
// owner edited as a whole, and a half-applied list would leave two names that
// disagree with each other on the glass.
static ApiResult apiScreenNames(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  JsonArray in = doc["names"];
  if (in.size() != SCREEN_COUNT) {
    return apiFail(400, "Los nombres tienen que listar todas las pantallas");
  }
  // Clean every name before writing any of them, so a rejected list changes
  // nothing.
  char cleaned[SCREEN_COUNT][SCREEN_NAME_MAX + 1];
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    const char *raw = in[i] | "";
    if (!screenNameClean(raw, cleaned[i], sizeof(cleaned[i]))) {
      return apiFail(400, "Cada nombre necesita entre 1 y 12 caracteres");
    }
  }
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    if (!screensSetName(i, cleaned[i])) {
      return apiFail(500, "No se pudo guardar el nombre en la memoria");
    }
  }
  return apiOk();
}

// The panel's prompt box asks the buddy bridge to start a run. The device is
// only the relay: the bridge holds the Hermes key, the device holds the bridge
// token.
static ApiResult apiBuddyAsk(const String &body) {
  JsonDocument doc;
  if (body.length() &&
      deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  const char *prompt = doc["prompt"] | "";
  char error[64];
  if (!buddyAsk(prompt, error, sizeof(error))) {
    return apiFail(502, error[0] ? error : "No pude pedirle nada a Sofia");
  }
  return apiOk();
}

// The panel needs to know what is missing, not just that something failed: the
// three auth states map to three different things the owner has to do.
static ApiResult apiSpotifyStatus() {
  ApiResult r;
  SpotifySnapshot sp;
  spotifySnapshot(&sp);
  JsonDocument doc;
  doc["ok"] = true;
  doc["configured"] = spotifyStoreHasClientId();
  doc["clientId"] = spotifyStoreClientId();
  doc["authorized"] = spotifyStoreHasRefreshToken();
  doc["auth"] = sp.auth;
  doc["feed"] = sp.feed;
  doc["httpStatus"] = sp.httpStatus;
  doc["error"] = sp.error;
  doc["redirectUri"] = SPOTIFY_REDIRECT_URI;
  JsonObject track = doc["track"].to<JsonObject>();
  track["title"] = sp.track.title;
  track["artist"] = sp.track.artist;
  track["album"] = sp.track.album;
  track["isPlaying"] = sp.track.isPlaying;
  track["progressMs"] = sp.track.progressMs;
  track["durationMs"] = sp.track.durationMs;
  serializeJson(doc, r.body);
  return r;
}

static ApiResult apiSpotifyConfig(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  const char *clientId = doc["clientId"] | "";
  if (!*clientId) return apiFail(400, "Falta el client id");
  if (!spotifyStoreSetClientId(clientId)) {
    return apiFail(400, "Client id invalido (8 a 63 caracteres)");
  }
  return apiOk();
}

static ApiResult apiSpotifyUrl() {
  char url[640];
  if (!spotifyAuthBuildUrl(url, sizeof(url))) {
    return apiFail(400, "Guarda el client id primero");
  }
  ApiResult r;
  JsonDocument doc;
  doc["ok"] = true;
  doc["url"] = url;
  doc["redirectUri"] = SPOTIFY_REDIRECT_URI;
  serializeJson(doc, r.body);
  return r;
}

static ApiResult apiSpotifyCode(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return apiFail(400, "Los datos enviados no son validos");
  }
  const char *code = doc["code"] | "";
  char err[72] = "";
  if (!spotifyAuthExchangeCode(code, err, sizeof(err))) {
    return apiFail(400, err[0] ? err : "No se pudo autorizar");
  }
  spotifyRequestRefresh();
  return apiOk();
}

static ApiResult apiSpotifyForget() {
  spotifyStoreForgetTokens();
  spotifyAuthReset();
  return apiOk();
}

// Paths that only accept POST. Listed so a wrong method on a real endpoint
// answers 405 while a path that does not exist answers 404 - conflating them
// makes a typo in a client look like a permission problem.
// Every path that only accepts POST. This list is the single source of truth:
// netBegin() registers a route for each entry and netHandleApi() rejects a GET on
// each entry. They used to be maintained separately, and the day a new endpoint
// was added to the dispatcher but not to the route table, the panel got
// "405 Metodo no permitido" for a route that simply did not exist.
static const char *const POST_PATHS[] = {
    "/api/wifi",           "/api/wifi/forget", "/api/time",
    "/api/ap",             "/api/settings",    "/api/screen",
    "/api/press",          "/api/scores/reset", "/api/steer/reset",
    "/api/steer/swap",     "/api/restart",     "/api/factory",
    "/api/spotify/config", "/api/spotify/url", "/api/spotify/code",
    "/api/spotify/forget", "/api/screens/order", "/api/screens/names",
    "/api/panel/colors", "/api/buddy/ask",
};
static const uint32_t POST_PATH_COUNT = sizeof(POST_PATHS) / sizeof(POST_PATHS[0]);

static bool isPostOnlyPath(const char *path) {
  for (uint32_t i = 0; i < POST_PATH_COUNT; i++) {
    if (!strcmp(path, POST_PATHS[i])) return true;
  }
  return false;
}

ApiResult netHandleApi(const char *method, const char *path, const String &body) {
  const bool isPost = strcmp(method, "POST") == 0;

  if (!strcmp(path, "/api/scan")) {
    return isPost ? apiScanStart() : apiScanRead();
  }

  if (!strcmp(path, "/api/status") || !strcmp(path, "/api/spotify")) {
    if (isPost) return apiFail(405, "Metodo no permitido");
    if (!strcmp(path, "/api/status")) {
      ApiResult r;
      buildStatusJson(r.body);
      return r;
    }
    return apiSpotifyStatus();
  }

  // Everything below mutates state, so it is POST only.
  if (!isPostOnlyPath(path)) return apiFail(404, "No existe ese recurso");
  if (!isPost) return apiFail(405, "Metodo no permitido");

  if (!strcmp(path, "/api/wifi")) return apiWifi(body);
  if (!strcmp(path, "/api/buddy/ask")) return apiBuddyAsk(body);
  if (!strcmp(path, "/api/panel/colors")) {
    // Panel self-test on demand. It blocks for ~5 s while the three colour bars
    // are shown, then the current screen is painted back.
    displayColorTest();
    screensGoTo(screensCurrent());
    return apiOk();
  }
  if (!strcmp(path, "/api/screens/order")) return apiScreenOrder(body);
  if (!strcmp(path, "/api/screens/names")) return apiScreenNames(body);
  if (!strcmp(path, "/api/spotify/config")) return apiSpotifyConfig(body);
  if (!strcmp(path, "/api/spotify/url")) return apiSpotifyUrl();
  if (!strcmp(path, "/api/spotify/code")) return apiSpotifyCode(body);
  if (!strcmp(path, "/api/spotify/forget")) return apiSpotifyForget();
  if (!strcmp(path, "/api/time")) return apiSetTime(body);
  if (!strcmp(path, "/api/wifi/forget")) {
    netClearSta();
    return apiOk();
  }
  if (!strcmp(path, "/api/ap")) return apiAp(body);
  if (!strcmp(path, "/api/settings")) return apiSettings(body);
  if (!strcmp(path, "/api/screen")) return apiScreen(body);
  if (!strcmp(path, "/api/press")) return apiPress(body);
  if (!strcmp(path, "/api/scores/reset")) {
    settingsMutable().gameHighScore = 0;
    settingsMutable().slotHighScore = 0;
    settingsSave();
    screensGoTo(screensCurrent());
    return apiOk();
  }
  if (!strcmp(path, "/api/steer/reset")) {
    gameInvalidateCalibration();
    screensGoTo(SCREEN_GAME);
    return apiOk();
  }
  if (!strcmp(path, "/api/steer/swap")) {
    gameSwapSteerAxis();
    return apiOk();
  }
  if (!strcmp(path, "/api/restart")) {
    ApiResult r = apiOk();
    r.restart = true;
    return r;
  }
  if (!strcmp(path, "/api/factory")) {
    settingsResetDefaults();
    settingsSave();
    // A factory reset that left the owner's Spotify token on the flash would be
    // a reset that does not reset.
    spotifyStoreForgetAll();
    spotifyAuthReset();
    {
      uint8_t identity[SCREEN_COUNT];
      for (uint8_t i = 0; i < SCREEN_COUNT; i++) identity[i] = i;
      screensSetOrder(identity);
    }
    // Names live under their own NVS keys, so a factory reset has to clear them
    // explicitly or the next owner inherits the previous one's labels.
    screensResetNames();
    ApiResult r = apiOk();
    r.restart = true;
    return r;
  }
  return apiFail(404, "No existe ese recurso");
}

// --- transport --------------------------------------------------------------

static void sendApi(const ApiResult &result, bool allowRestart) {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(result.code, "application/json", result.body);
  if (result.restart && allowRestart) {
    delay(300);  // let the response leave the socket before the radio drops
    netRestart();
  }
}

static void handleApi() {
  String body;
  if (s_server.method() == HTTP_POST) body = s_server.arg("plain");
  ApiResult result =
      netHandleApi(s_server.method() == HTTP_POST ? "POST" : "GET",
                   s_server.uri().c_str(), body);
  sendApi(result, true);
}

// Serves the packed web app from flash, so the panel needs no internet.
static bool serveAsset(const String &path) {
  for (uint32_t i = 0; i < WEB_ASSET_COUNT; i++) {
    if (path != WEB_ASSETS[i].path) continue;
    s_server.sendHeader("Cache-Control", "no-cache");
    s_server.send_P(200, WEB_ASSETS[i].mime, (PGM_P)WEB_ASSETS[i].data,
                    WEB_ASSETS[i].len);
    return true;
  }
  return false;
}

static void handleNotFound() {
  String path = s_server.uri();
  if (path.startsWith("/api/")) {
    // Pass the method the client actually used, and its body. Hardcoding GET
    // here turned a missing route into "405 Metodo no permitido", which blames
    // the caller's method for the firmware's own routing mistake.
    const bool isPost = s_server.method() == HTTP_POST;
    ApiResult result =
        netHandleApi(isPost ? "POST" : "GET", path.c_str(),
                     isPost ? s_server.arg("plain") : String());
    sendApi(result, false);
    return;
  }
  if (path == "/") path = "/index.html";
  if (serveAsset(path)) return;
  s_server.send(404, "text/plain", "not found");
}

// --- lifecycle --------------------------------------------------------------

static void startAccessPoint() {
  const Settings &cfg = settingsGet();
  bool open = cfg.apPass[0] == '\0';
  bool ok = open ? WiFi.softAP(cfg.apSsid) : WiFi.softAP(cfg.apSsid, cfg.apPass);
  if (!ok) setError("No se pudo iniciar el punto de acceso");
}

static void startStation() {
  if (!settingsHasSta()) return;
  WiFi.begin(settingsGet().staSsid, settingsGet().staPass);
  s_lastStaAttempt = millis();
  s_staSince = millis();
}

void netBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  // Let the core retry the station link by itself; it reacts to disconnect
  // events, which is what a router reboot looks like.
  WiFi.setAutoReconnect(true);

  // Capturing the disconnect reason is the only way to tell "wrong password"
  // apart from "the router will not let this device in".
  WiFi.onEvent(
      [](WiFiEvent_t event, WiFiEventInfo_t info) {
        s_disconnectReason = (int)info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  startAccessPoint();
  startStation();
  netApplyTimeZone();
  refreshStatus();

  // Registered explicitly per path so the request body is parsed by the same
  // well-tested path the rest of the firmware uses. POST routes come from
  // POST_PATHS, so a new endpoint cannot exist for the dispatcher and be missing
  // from the transport.
  s_server.on("/api/status", HTTP_GET, handleApi);
  s_server.on("/api/scan", HTTP_GET, handleApi);
  s_server.on("/api/scan", HTTP_POST, handleApi);
  s_server.on("/api/spotify", HTTP_GET, handleApi);
  for (uint32_t i = 0; i < POST_PATH_COUNT; i++) {
    s_server.on(POST_PATHS[i], HTTP_POST, handleApi);
  }
  s_server.onNotFound(handleNotFound);
  s_server.begin();
}

bool netSetSta(const char *ssid, const char *pass) {
  setError("");
  if (!ssid || !ssid[0]) {
    setError("El nombre de la red no puede estar vacio");
    return false;
  }
  if (strlen(ssid) > SETTINGS_SSID_MAX) {
    setError("El nombre de la red es demasiado largo");
    return false;
  }
  if (pass && strlen(pass) > SETTINGS_PASS_MAX) {
    setError("La contrasena es demasiado larga");
    return false;
  }

  Settings &cfg = settingsMutable();
  snprintf(cfg.staSsid, sizeof(cfg.staSsid), "%s", ssid);
  snprintf(cfg.staPass, sizeof(cfg.staPass), "%s", pass ? pass : "");
  settingsSave();

  WiFi.disconnect(false, false);
  WiFi.begin(cfg.staSsid, cfg.staPass);
  s_lastStaAttempt = millis();
  s_staSince = millis();
  return true;
}

void netClearSta() {
  Settings &cfg = settingsMutable();
  cfg.staSsid[0] = '\0';
  cfg.staPass[0] = '\0';
  settingsSave();
  WiFi.disconnect(false, false);
  s_staSince = millis();
  refreshStatus();
}

bool netSetAp(const char *ssid, const char *pass) {
  setError("");
  if (!ssid || !ssid[0]) {
    setError("El nombre del punto de acceso no puede estar vacio");
    return false;
  }
  if (strlen(ssid) > SETTINGS_AP_SSID_MAX) {
    setError("El nombre del punto de acceso es demasiado largo");
    return false;
  }
  size_t passLen = pass ? strlen(pass) : 0;
  if (passLen > SETTINGS_AP_PASS_MAX) {
    setError("La contrasena del punto de acceso es demasiado larga");
    return false;
  }
  if (passLen > 0 && passLen < 8) {
    setError("La contrasena debe tener al menos 8 caracteres");
    return false;
  }

  Settings &cfg = settingsMutable();
  snprintf(cfg.apSsid, sizeof(cfg.apSsid), "%s", ssid);
  snprintf(cfg.apPass, sizeof(cfg.apPass), "%s", pass ? pass : "");
  settingsSave();

  WiFi.softAPdisconnect(true);
  startAccessPoint();
  return s_lastError[0] == '\0';
}

void netScanJson(String &out) {
  ApiResult r = apiScanRead();
  out = r.body;
}

uint32_t netAssetCount() { return WEB_ASSET_COUNT; }

const char *netAssetPath(uint32_t index) {
  return index < WEB_ASSET_COUNT ? WEB_ASSETS[index].path : "";
}

uint32_t netAssetLength(uint32_t index) {
  return index < WEB_ASSET_COUNT ? WEB_ASSETS[index].len : 0;
}

uint32_t netAssetHash(uint32_t index) {
  if (index >= WEB_ASSET_COUNT) return 0;
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < WEB_ASSETS[index].len; i++) {
    hash = (hash ^ WEB_ASSETS[index].data[i]) * 16777619u;
  }
  return hash;
}

void netRestart() {
  refreshStatus();
  ESP.restart();
}

void netLoop() {
  s_server.handleClient();

  // lwIP starts SNTP at boot, when the station link is still down, and backs
  // off failed polls by doubling from SNTP_RECV_TIMEOUT (15 s) up to
  // SNTP_RETRY_TIMEOUT_MAX (15 s x 10 = 150 s). Without this re-arm, saving the
  // WiFi credentials could leave the clock on "SIN HORA TODAVIA" for up to two
  // and a half minutes, which reads as broken. Restarting SNTP on the
  // disconnected -> connected edge makes the first poll happen immediately.
  static bool s_staWasConnected = false;
  bool staConnected = WiFi.status() == WL_CONNECTED;
  if (staConnected && !s_staWasConnected) {
    netApplyTimeZone();
    txReliable(true);
    Serial.println("sta conectado -> NTP re-armado, esperando la hora");
    txReliable(false);
  }
  s_staWasConnected = staConnected;

  // Keep the station link alive without ever blocking the loop. A previous
  // version called WiFi.disconnect() before each retry, which aborts the
  // association attempt in progress: on any router slower than the retry
  // interval the device could never connect at all. Now the core's own
  // auto-reconnect does the work and this only re-issues begin() as a slow
  // safety net, without tearing the interface down.
  if (settingsHasSta() && WiFi.status() != WL_CONNECTED) {
    if ((uint32_t)(millis() - s_lastStaAttempt) >= STA_RETRY_MS) {
      s_lastStaAttempt = millis();
      if (s_staSince == 0) s_staSince = millis();
      WiFi.begin(settingsGet().staSsid, settingsGet().staPass);
    }
  }

  static uint32_t lastRefresh = 0;
  if ((uint32_t)(millis() - lastRefresh) >= 1000) {
    lastRefresh = millis();
    refreshStatus();
  }
}

#include "net.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <time.h>

#include "admin_auth.h"
#include "console.h"
#include "display.h"
#include "rgb.h"
#include "rotation.h"
#include "buddy_face.h"
#include "buddy_url.h"
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

// Web panel sessions and the login throttle live in RAM only: a reboot logs
// every device out, which is the one logout this board can guarantee. Nothing
// here survives a power cycle and nothing here is worth persisting.
static AdminSessionTable s_sessions;
static AdminThrottle s_throttle;

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

// One guard and one message for every JSON body. Optional bodies are allowed to
// be empty (the endpoint then runs on defaults); required ones cannot.
static const char *const API_BAD_JSON = "Los datos enviados no son validos";

// The parser names the cause; the owner reads it in the panel. A single generic
// "invalid URL" leaves them guessing between a missing scheme and a bad port.
static const char *buddyUrlApiError(BuddyUrlError err) {
  switch (err) {
    case BUDDY_URL_NO_SCHEME: return "La URL del buddy necesita http:// o https://";
    case BUDDY_URL_BAD_SCHEME: return "Solo se acepta http:// o https://";
    case BUDDY_URL_NO_HOST: return "Falta el host en la URL del buddy";
    case BUDDY_URL_BAD_HOST: return "El host de la URL del buddy no es valido";
    case BUDDY_URL_BAD_PORT: return "El puerto de la URL del buddy no es valido";
    case BUDDY_URL_BAD_PATH: return "La URL del buddy tiene caracteres invalidos en la ruta";
    case BUDDY_URL_TOO_LONG: return "La URL del buddy es demasiado larga";
    case BUDDY_URL_EMPTY: break;
    case BUDDY_URL_OK: break;
  }
  return "La URL del buddy no puede estar vacia";
}

static bool parseOptionalJson(const String &body, JsonDocument &doc) {
  return body.length() == 0 ||
         deserializeJson(doc, body) == DeserializationError::Ok;
}

static bool parseRequiredJson(const String &body, JsonDocument &doc) {
  return deserializeJson(doc, body) == DeserializationError::Ok;
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
  // The mode and the lock are what the panel edits; `orientation` is the image
  // actually on the glass, which in automatic mode the panel did not choose.
  disp["rotationMode"] = rotationModeName();
  disp["rotationLocked"] = rotationLocked();
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
    // Why the link is where it is. The panel turns the name into one sentence per
    // cause; `linkDetail` is the raw transport reason, kept for diagnosis.
    obj["link"] = buddyLinkWireName(buddy.link);
    obj["linkDetail"] = buddy.linkDetail;
    obj["url"] = url;
    {
      char eyeHex[8];
      uiColorToHex(settingsGetBuddyEyeColor(), eyeHex);
      obj["eyeColor"] = eyeHex;
    }
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
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
  const char *ssid = doc["ssid"] | "";
  const char *pass = doc["pass"] | "";

  if (!ssid[0]) return apiFail(400, "El nombre de la red no puede estar vacio");
  if (!netSetSta(ssid, pass)) return apiFail(400, netLastError());
  return apiOk();
}

static ApiResult apiAp(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);

  Settings &cfg = settingsMutable();
  bool tzChanged = false;

  if (doc["brightness"].is<int>()) {
    // The floor and the ceiling live in settings, so the panel and the CONFIG
    // screen cannot disagree about the range. A value that is not a byte yet
    // (a negative or an obviously out-of-scale number) is clamped before the
    // cast, because casting first would wrap it into range.
    int value = doc["brightness"];
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    settingsSetLcdBrightness((uint8_t)value);
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

  if (!doc["buddyEyeColor"].isNull()) {
    // An empty string means "back to the shipped cyan"; anything else must be a
    // valid #RRGGBB, so a typo can never reach NVS.
    const char *text = doc["buddyEyeColor"] | "";
    uint32_t rgb = BUDDY_EYE_COLOR_DEFAULT;
    if (text[0] && !uiColorParse(text, &rgb)) {
      return apiFail(400, "Color de ojos invalido");
    }
    if (!settingsSetBuddyEyeColor(rgb)) {
      return apiFail(500, "No pude guardar el color de ojos");
    }
  }

  if (!doc["buddyUrl"].isNull()) {
    const char *url = doc["buddyUrl"] | "";
    if (!settingsSetBuddyUrl(url)) {
      BuddyUrl parsed;
      BuddyUrlError err = buddyUrlParse(url, &parsed);
      // A URL that parses but does not persist is a storage failure, not a bad
      // URL: the owner would otherwise keep retrying a value that is fine.
      if (err != BUDDY_URL_OK) return apiFail(400, buddyUrlApiError(err));
      return apiFail(500, "No pude guardar la URL del buddy");
    }
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

  // Rotation. The panel edits the mode (automatic, or one pinned image) and the
  // lock; `orientation` still pins a raw rotation, which is what the console
  // command and older panels speak.
  if (!doc["rotationMode"].isNull()) {
    uint8_t mode = ROT_MODE_AUTO;
    if (doc["rotationMode"].is<int>()) {
      int value = doc["rotationMode"];
      if (value < 0 || value > 255 || !rotModeValid((uint8_t)value)) {
        return apiFail(400, "Modo de rotacion invalido");
      }
      mode = (uint8_t)value;
    } else {
      const char *text = doc["rotationMode"] | "";
      if (!strcmp(text, "auto")) {
        mode = ROT_MODE_AUTO;
      } else if (!strcmp(text, "horizontal")) {
        mode = ROT_MODE_CW;
      } else if (!strcmp(text, "inverted")) {
        mode = ROT_MODE_CCW;
      } else {
        return apiFail(400, "Modo de rotacion invalido");
      }
    }
    rotationSetMode(mode);
  }
  if (doc["rotationLocked"].is<bool>()) {
    rotationSetLocked(doc["rotationLocked"]);
  }
  if (doc["orientation"].is<int>()) {
    int value = doc["orientation"];
    if (value < 0 || value > 3) {
      return apiFail(400, "Orientacion de pantalla invalida");
    }
    rotationSetMode((uint8_t)value);
  }

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
      // The bead level survives a power cycle now: it is the second row of the
      // CONFIG screen, and a value that only lived in RAM would make the two
      // controls disagree after a reboot.
      settingsSetRgbLevel((uint8_t)value);
    }
    if (rgbIn["effect"].is<int>()) {
      int value = rgbIn["effect"];
      if (value >= 0 && value <= 2) rgbSetEffect((uint8_t)value);
    }
    // The bead answers the request before the response goes out, and it wakes: the
    // panel used to store the level while the bead stayed dark from the sleep
    // timer, which read as a slider that did nothing.
    rgbApplyNow();
  }

  settingsSave();
  if (tzChanged) netApplyTimeZone();
  screensGoTo(screensCurrent());  // brightness/format changes need a repaint
  return apiOk();
}

static ApiResult apiScreen(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);
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
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);
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

// --- web credentials (API) --------------------------------------------------

// Logging in is the only endpoint that hands out authority, so it is also the
// only one that answers the same way to a wrong password and to a lockout: an
// attacker learns nothing, and the owner reads the panel's own message.
static const char *const LOGIN_REJECTED = "Usuario o contrasena incorrectos";

static ApiResult apiSessionLogin(const String &body) {
  JsonDocument doc;
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);

  uint32_t now = millis();
  if (adminThrottleBlocked(&s_throttle, now)) {
    return apiFail(401, LOGIN_REJECTED);
  }

  const char *user = doc["user"] | "";
  const char *pass = doc["pass"] | "";
  if (!settingsAdminMatches(user, pass)) {
    adminThrottleFail(&s_throttle, now);
    return apiFail(401, LOGIN_REJECTED);
  }
  adminThrottleReset(&s_throttle);

  // The token is the only secret the browser holds, and it is random rather than
  // derived from the password, so it cannot be guessed from a leak of the seed.
  uint8_t raw[ADMIN_HASH_LEN];
  for (size_t i = 0; i < sizeof(raw); i++) raw[i] = (uint8_t)esp_random();
  adminSessionOpen(&s_sessions, now, raw);

  const AdminCredentials &creds = settingsGetAdmin();
  ApiResult result;
  JsonDocument out;
  out["ok"] = true;
  out["user"] = creds.user;
  // The panel cannot be used while the fallback pair is in place (S6).
  out["mustChange"] = creds.isDefault;
  serializeJson(out, result.body);
  result.sessionOpened = true;
  adminHexEncode(raw, sizeof(raw), result.sessionToken,
                 sizeof(result.sessionToken));
  memset(raw, 0, sizeof(raw));
  return result;
}

// Changes the pair the panel accepts. The current password is required, so a
// session left open on a desk cannot be used to lock the owner out of their own
// device. It is POST-only and protected like every other write; the transport
// then drops the other sessions, which is why this answer says nothing about
// them.
static ApiResult apiAdminCredentials(const String &body) {
  JsonDocument doc;
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);

  const AdminCredentials &creds = settingsGetAdmin();
  const char *current = doc["current"] | "";
  const char *user = doc["user"] | "";
  const char *pass = doc["pass"] | "";

  if (!settingsAdminMatches(creds.user, current)) {
    return apiFail(403, "La contrasena actual no es correcta");
  }
  if (!adminUserValid(user)) {
    return apiFail(400, "El usuario necesita entre 1 y 32 caracteres sin espacios");
  }
  if (!adminPassValid(pass)) {
    return apiFail(400, "La contrasena nueva necesita entre 8 y 64 caracteres");
  }

  uint8_t salt[ADMIN_SALT_LEN];
  for (size_t i = 0; i < sizeof(salt); i++) salt[i] = (uint8_t)esp_random();
  uint8_t hash[ADMIN_HASH_LEN];
  adminHashDerive(salt, user, pass, hash);
  bool ok = settingsSetAdminCredentials(user, salt, hash, false);
  memset(hash, 0, sizeof(hash));
  if (!ok) return apiFail(500, "No se pudo guardar la credencial");
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
    "/api/panel/colors", "/api/buddy/ask", "/api/admin/credentials",
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

  // The only endpoint reachable without a session, because it is the one that
  // creates it. It is also the serial console's answer to "do the credentials I
  // stored actually work?", which is why it lives in the dispatcher rather than
  // in the HTTP layer.
  if (!strcmp(path, "/api/session")) {
    if (!isPost) return apiFail(405, "Metodo no permitido");
    return apiSessionLogin(body);
  }

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

  if (!strcmp(path, "/api/admin/credentials")) {
    return apiAdminCredentials(body);
  }
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
    // Clearing the axes used to drop the owner into the game, which then asked
    // for the gesture. The wizard lives in CONFIG now, so the button takes them
    // there and opens it directly.
    gameInvalidateCalibration();
    screensGoTo(SCREEN_SETTINGS);
    configStartCalibration();
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
    // The panel's credentials go back to the built-in seed, and every session
    // opened with the old pair dies: after a reset the device belongs to whoever
    // holds it, not to whoever was logged in.
    settingsResetAdminCredentials();
    netSessionForgetAll();
    {
      uint8_t identity[SCREEN_COUNT];
      for (uint8_t i = 0; i < SCREEN_COUNT; i++) identity[i] = i;
      screensSetOrder(identity);
    }
    // Names live under their own NVS keys, so a factory reset has to clear them
    // explicitly or the next owner inherits the previous one's labels.
    screensResetNames();
    // Same for the bead level: back to the firmware default, in RAM and in NVS.
    rgbSetLevel(SETTINGS_RGB_LEVEL_DEFAULT);
    settingsSetRgbLevel(SETTINGS_RGB_LEVEL_DEFAULT);
    ApiResult r = apiOk();
    r.restart = true;
    return r;
  }
  return apiFail(404, "No existe ese recurso");
}

// --- transport --------------------------------------------------------------

// --- web session (transport) ------------------------------------------------

// The cookie name. Short because it travels in every request, and there is
// exactly one: "sid" cannot be confused with a form field.
static const char SESSION_COOKIE[] = "sid";

static void sendApi(const ApiResult &result, bool allowRestart) {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(result.code, "application/json", result.body);
  if (result.restart && allowRestart) {
    delay(300);  // let the response leave the socket before the radio drops
    netRestart();
  }
}

// One value out of the request's Cookie header. Hand-parsed because the panel
// ships exactly one cookie and pulling in a parser for it would be more code
// than the thing it parses.
static String requestCookie(const char *name) {
  String header = s_server.header("Cookie");
  if (header.length() == 0) return String();
  size_t start = 0;
  while (start < header.length()) {
    int end = header.indexOf(';', start);
    if (end < 0) end = (int)header.length();
    String pair = header.substring(start, end);
    pair.trim();
    int eq = pair.indexOf('=');
    if (eq > 0 && pair.substring(0, eq) == name) {
      return pair.substring(eq + 1);
    }
    start = (size_t)end + 1;
  }
  return String();
}

// HttpOnly keeps the token away from any script on the page and SameSite=Strict
// keeps another site from riding the session. There is no Secure flag on purpose:
// the panel is served over the access point's plain HTTP, and a Secure cookie
// would never be stored, leaving the owner unable to log in at all. That is a
// known limit of serving a panel from a device with no TLS, and it is why the
// session is bounded in time and the credentials are stored hashed.
static void sendSessionCookie(const char *token) {
  String value = String(SESSION_COOKIE) + "=" + token +
                 "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
                 String((unsigned long)(ADMIN_SESSION_TTL_MS / 1000));
  s_server.sendHeader("Set-Cookie", value);
}

static void clearSessionCookie() {
  String value = String(SESSION_COOKIE) +
                 "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
  s_server.sendHeader("Set-Cookie", value);
}

static String requestSessionToken() { return requestCookie(SESSION_COOKIE); }

static bool requestAuthorized() {
  String token = requestSessionToken();
  if (token.length() == 0) return false;
  return adminSessionValid(&s_sessions, token.c_str(), millis());
}

void netSessionForgetAll() { adminSessionReset(&s_sessions); }

// The login page's own status call. It is public because it has to answer before
// there is a session; it therefore says only whether this browser is logged in,
// and reveals the user and the forced-change flag only to that browser.
static void handleSessionState() {
  const AdminCredentials &creds = settingsGetAdmin();
  bool authenticated = requestAuthorized();
  JsonDocument doc;
  doc["ok"] = true;
  doc["authenticated"] = authenticated;
  if (authenticated) {
    doc["user"] = creds.user;
    doc["mustChange"] = creds.isDefault;
  }
  String body;
  serializeJson(doc, body);
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "application/json", body);
}

// Logging out is closing a cookie: it needs no session of its own, and answering
// it without one keeps a stale tab from showing an error when the owner leaves.
static void handleSessionLogout() {
  String token = requestSessionToken();
  if (token.length() > 0) adminSessionClose(&s_sessions, token.c_str());
  clearSessionCookie();
  ApiResult result = apiOk();
  sendApi(result, false);
}

static void sendUnauthorized() {
  ApiResult result = apiFail(401, "Sesion requerida");
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(result.code, "application/json", result.body);
}

// A browser that opens /admin without a session gets the login page and comes
// back to where it was going. Remembering the target is what makes "log in and
// land on the page I asked for" work instead of dumping the owner at the root.
static void redirectToLogin(const String &path) {
  String location = "/login";
  if (path != "/admin" && path != "/admin/" && path != "/admin.html") {
    location += "?next=";
    location += path;
  }
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.sendHeader("Location", location);
  s_server.send(302, "text/plain", "");
}

// The single place that decides whether a request may reach the dispatcher or
// the asset table. Everything that answers without going through here would be a
// hole, which is why the API handler, the not-found handler and the session
// state handler are the only three entry points.
static bool authorize(const char *method, const String &path) {
  if (!adminRequiresSession(method, path.c_str())) return true;
  if (requestAuthorized()) return true;
  if (path.startsWith("/api/")) {
    sendUnauthorized();
  } else {
    redirectToLogin(path);
  }
  return false;
}

static void handleApi() {
  const bool isPost = s_server.method() == HTTP_POST;
  const char *method = isPost ? "POST" : "GET";
  if (!authorize(method, s_server.uri())) return;

  String body;
  if (isPost) body = s_server.arg("plain");
  ApiResult result = netHandleApi(method, s_server.uri().c_str(), body);

  if (result.sessionOpened) {
    sendSessionCookie(result.sessionToken);
    // Only the browser that just logged in keeps its session, so a changed
    // password or a fresh login on another device cannot be shadowed by an old
    // cookie that someone else still holds.
    adminSessionCloseOthers(&s_sessions, result.sessionToken);
  }
  // Rotating the credentials logs out every other device; the caller keeps the
  // session it used to make the change, which is the one that still works.
  if (result.code == 200 && s_server.uri() == "/api/admin/credentials") {
    adminSessionCloseOthers(&s_sessions, requestSessionToken().c_str());
  }
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
  const bool isPost = s_server.method() == HTTP_POST;
  if (!authorize(isPost ? "POST" : "GET", path)) return;
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
  // The panel's pages are one file each, and the routes are the names the owner
  // types: "/" is the public placeholder, "/admin" is the configuration area,
  // "/login" is the door.
  if (path == "/" || path == "/index") path = "/index.html";
  if (path == "/admin" || path == "/admin/") path = "/admin.html";
  if (path == "/login" || path == "/login/") path = "/login.html";
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

  // Sessions and the login throttle start empty on every boot: a session that
  // survives a power cycle is a session nobody can revoke by unplugging.
  adminSessionReset(&s_sessions);
  adminThrottleReset(&s_throttle);
  // Seed the credentials now, not on the first login: the fallback pair has to
  // be in NVS for the panel to report that it must be changed.
  settingsGetAdmin();

  // Registered explicitly per path so the request body is parsed by the same
  // well-tested path the rest of the firmware uses. POST routes come from
  // POST_PATHS, so a new endpoint cannot exist for the dispatcher and be missing
  // from the transport.
  s_server.on("/api/status", HTTP_GET, handleApi);
  s_server.on("/api/scan", HTTP_GET, handleApi);
  s_server.on("/api/scan", HTTP_POST, handleApi);
  s_server.on("/api/spotify", HTTP_GET, handleApi);

  // The two session endpoints the dispatcher does not own. Logging in is the one
  // POST that has to work without a session, and the state call answers from the
  // cookie the transport holds, so neither is listed in POST_PATHS.
  s_server.on("/api/session", HTTP_GET, handleSessionState);
  s_server.on("/api/session", HTTP_POST, handleApi);
  s_server.on("/api/session/logout", HTTP_POST, handleSessionLogout);

  for (uint32_t i = 0; i < POST_PATH_COUNT; i++) {
    s_server.on(POST_PATHS[i], HTTP_POST, handleApi);
  }
  s_server.onNotFound(handleNotFound);

  // This WebServer keeps only the request headers it was told to keep: by
  // default that is Authorization and ETag, so `header("Cookie")` answered an
  // empty string and every session looked expired the moment it was issued. A
  // device-only defect: nothing above the transport can see it, and the host
  // tests pass either way. One entry is enough, because the panel has one cookie.
  static const char *REQUEST_HEADERS[] = {"Cookie"};
  s_server.collectHeaders(REQUEST_HEADERS, 1);

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

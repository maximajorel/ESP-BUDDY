#include "net.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <time.h>

#include "admin_auth.h"
#include "admin_session_store.h"
#include "config_value.h"
#include "console.h"
#include "display.h"
#include "net_ap_policy.h"
#include "rgb.h"
#include "rotation.h"
#include "buddy_face.h"
#include "buddy_focus_state.h"
#include "calendar.h"
#include "calendar_events.h"
#include "calendar_month.h"
#include "calendar_plan.h"
#include "calendar_store.h"
#include "buddy_url.h"
#include "screen_buddy.h"
#include "screen_sleep.h"
#include "screens.h"
#include "settings.h"
#include "spotify.h"
#include "spotify_auth.h"
#include "spotify_store.h"
#include "ui_color.h"
#include "weather.h"
#include "weather_code.h"
#include "weather_parse.h"
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

// The access point's duty cycle. `auto` is the shipped behaviour: the AP carries
// the panel while the home network is missing and steps aside once the station
// link has proven itself. See net_ap_policy.h for why.
static uint8_t s_apMode = NET_AP_AUTO;
// What the radio is doing right now, so a transition happens once per crossing
// instead of on every poll.
static bool s_apRunning = true;
// When the current station link came up, which is what earns the silence.
static uint32_t s_staUpSince = 0;

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

// Defined with the weather handlers below; `buildStatusJson` is above them and
// only needs the kind's wire name.
static const char *weatherKindName(WeatherStatusKind kind);

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

  {
    // Concentración. This is state, not configuration: it is the countdown the
    // public entrance can start anyway, so publishing it on the public status
    // costs nothing and saves the landing a second round trip.
    const BuddyFocus &focus = buddyFocusView();
    JsonObject f = doc["focus"].to<JsonObject>();
    f["active"] = focus.active;
    f["finished"] = focus.finished;
    // Duration in whole seconds is the exact number; `minutes` stays for callers
    // that only ever think in blocks.
    f["seconds"] = (uint32_t)(focus.durationMs / BUDDY_FOCUS_SECOND_MS);
    f["minutes"] = (uint16_t)(focus.durationMs / BUDDY_FOCUS_MINUTE_MS);
    f["remainingMs"] = buddyFocusRemainingMs(&focus, (uint32_t)millis());
  }

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
  char stamp[24] = "--";
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmNow);
  }
  t["local"] = stamp;

  {
    // Compact weather summary so the panel's first paint and a serial `api` dump
    // can see the state without a second request. The full detail (search,
    // detect, refresh) lives in `/api/weather`.
    const WeatherStatusInfo w = weatherStatusInfo();
    JsonObject obj = doc["weather"].to<JsonObject>();
    obj["configured"] = w.configured;
    obj["hasData"] = w.hasData;
    obj["syncing"] = w.syncing;
    obj["kind"] = weatherKindName(w.kind);
    obj["place"] = String(w.place.name);
    obj["lastError"] = String(w.lastError);
    obj["refreshMin"] = w.refreshMin;
    obj["tempC"] = w.now.tempC;
    obj["label"] = weatherCodeLabel(w.now.code);
  }

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
      // Igual que el token de lectura: se publica si está, nunca el valor. El
      // panel lo usa para saber si puede guardar el calendario o pedir cosas.
      char ask[SETTINGS_BUDDY_ASK_TOKEN_MAX + 1];
      settingsGetBuddyAskToken(ask, sizeof(ask));
      obj["hasAskToken"] = ask[0] != '\0';
    }
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
    // The range lives in config_value.h and nowhere else. This handler used to
    // clamp to 0..255 first — a scale the firmware does not have — and then
    // settings clamped again to 5..100, so a request for 200 became 100 while
    // the code read as if 200 had been accepted.
    int value = doc["brightness"];
    settingsSetLcdBrightness(cfgClampLcd(value));
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
  if (!doc["buddyAskToken"].isNull()) {
    const char *token = doc["buddyAskToken"] | "";
    if (!settingsSetBuddyAskToken(token)) {
      return apiFail(400, "Token de administracion invalido");
    }
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

// The two writes the tray needs without a session: the glass's brightness and the
// RGB bead.
//
// Both are cosmetic and reversible, like the screen picker and the face, and both
// are deliberately narrower than `/api/settings`: that endpoint also stores the
// timezone, the buddy URL and two tokens, so publishing it to save a login would
// hand those out as well. Unknown fields are ignored here, never forwarded.
static ApiResult apiLcd(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
  if (!doc["brightness"].is<int>()) return apiFail(400, "Falta el brillo");

  // The range lives in `config_value.h` and nowhere else, exactly as in
  // `/api/settings`: a second copy of "5..100" would drift.
  settingsSetLcdBrightness(cfgClampLcd(doc["brightness"] | 0));
  displaySetBacklight(settingsGet().brightness);
  screensGoTo(screensCurrent());  // a brightness change needs a repaint
  return apiOk();
}

static ApiResult apiLed(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);

  bool touched = false;
  if (doc["r"].is<int>() && doc["g"].is<int>() && doc["b"].is<int>()) {
    rgbSetColor(cfgClampByte(doc["r"] | 0), cfgClampByte(doc["g"] | 0),
                cfgClampByte(doc["b"] | 0));
    touched = true;
  }
  if (doc["level"].is<int>()) {
    const uint8_t level = cfgClampByte(doc["level"] | 0);
    rgbSetLevel(level);
    // The level survives a power cycle, like the panel's slider: the CONFIG
    // screen shows the same number and the two cannot disagree after a reboot.
    settingsSetRgbLevel(level);
    touched = true;
  }
  if (doc["effect"].is<int>()) {
    const int value = doc["effect"];
    if (value < 0 || value > 2) return apiFail(400, "Efecto de LED invalido");
    rgbSetEffect((uint8_t)value);
    touched = true;
  }
  if (!touched) return apiFail(400, "Falta el color, el nivel o el efecto");

  // The bead answers before the response goes out, and it wakes: a level that is
  // stored while the bead stays dark from the sleep timer reads as a slider that
  // does nothing.
  rgbApplyNow();
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

// The web game names a mood; buddy_game.h decides what it means on the glass.
// The overlay expires on its own, so an abandoned tab cannot leave a face on.
static ApiResult apiFace(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
  const char *mood = doc["mood"] | "";
  uint32_t holdMs = doc["ms"] | 0u;
  // The dance is not a game mood: it is a pin on the body with its own synthetic
  // beat, so the panel can prove the music reaction without a track playing.
  if (!strcmp(mood, "dance")) {
    buddyPinDance(holdMs ? holdMs : 8000u);
    return apiOk();
  }
  if (!mood[0]) buddyPinDance(0);
  buddyGameFaceMood(mood, holdMs);
  return apiOk();
}

// Concentración: the public entrance starts a study countdown on the SOFIA
// screen with no session, the same authority it already has to choose the
// screen. This handler only writes a deadline against millis(); it never draws,
// never switches screens and never takes the buddy lock, so a slow client cannot
// hold the face loop. Zero cancels, above the maximum clamps.
//
// The countdown is stored in seconds. `minutes` stays for every caller that
// already exists, `seconds` is the exact form. Sending both is a client bug, not
// a preference, so it is refused instead of silently picking one.
static ApiResult apiFocus(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
  JsonVariant minutesIn = doc["minutes"];
  JsonVariant secondsIn = doc["seconds"];
  if (!minutesIn.isNull() && !secondsIn.isNull()) {
    return apiFail(400, "manda minutes o seconds, no los dos");
  }
  const uint32_t now = (uint32_t)millis();
  // Three different requests, three different answers: no field at all means the
  // usual session, zero means cancel, and anything that is not an integer is a
  // client bug that must not silently stop a session that is already running.
  if (!secondsIn.isNull()) {
    if (!secondsIn.is<int>()) return apiFail(400, "seconds tiene que ser un entero");
    const int value = secondsIn.as<int>();
    if (value < 0) return apiFail(400, "seconds no puede ser negativo");
    uint32_t seconds = (uint32_t)value;
    if (seconds > BUDDY_FOCUS_MAX_SECONDS) seconds = BUDDY_FOCUS_MAX_SECONDS;
    buddyFocusRequestSeconds(seconds, now);
    return apiOk();
  }
  uint16_t minutes = BUDDY_FOCUS_DEFAULT_MINUTES;
  JsonVariant in = minutesIn;
  if (!in.isNull()) {
    if (!in.is<int>()) return apiFail(400, "minutes tiene que ser un entero");
    const int value = in.as<int>();
    if (value < 0) return apiFail(400, "minutes no puede ser negativo");
    if (value == 0) {
      buddyFocusRequestSeconds(0, now);
      return apiOk();
    }
    minutes = (uint16_t)value;
    if (minutes > BUDDY_FOCUS_MAX_MINUTES) minutes = BUDDY_FOCUS_MAX_MINUTES;
  }
  buddyFocusRequest(minutes, now);
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

// The calendar lives on this board now: it holds the Google credentials, does
// the OAuth device flow and fetches the events itself. None of these handlers
// talks to the bridge, and all of them need a session: the event titles and
// which credentials are stored are the owner's business.
static const char *calendarKindName(CalendarStatusKind kind) {
  switch (kind) {
    case CALENDAR_UNCONFIGURED: return "unconfigured";
    case CALENDAR_NEEDS_AUTH: return "needs_auth";
    case CALENDAR_AUTH_PENDING: return "auth_pending";
    case CALENDAR_READY: break;
  }
  return "ready";
}

// El struct se recibe por referencia a propósito: ArduinoJson guarda punteros a
// los `char[]`, no copias, así que tiene que seguir vivo hasta serializar. Un
// `c` local acá adentro producía bytes basura en el panel (verificado en placa).
static void fillCalendarJson(JsonDocument &doc, const CalendarStatusInfo &c) {
  doc["ok"] = true;
  doc["kind"] = calendarKindName(c.kind);
  doc["configured"] = c.configured;
  doc["authorized"] = c.authorized;
  doc["pending"] = c.pending;
  doc["syncing"] = c.syncing;
  doc["userCode"] = String(c.userCode);
  doc["verificationUrl"] = String(c.verificationUrl);
  doc["pendingSecondsLeft"] = c.pendingSecondsLeft;
  doc["selectedId"] = String(c.selectedId);
  doc["selectedName"] = String(c.selectedName);
  doc["fallbackLeadMin"] = c.fallbackLeadMin;
  doc["graceMin"] = c.graceMin;
  doc["lastError"] = String(c.lastError);
  doc["lastSyncMs"] = c.lastSyncMs;
  doc["calendarCount"] = c.calendarCount;
  doc["eventCount"] = c.eventCount;
  doc["hasNext"] = c.hasNext;
  doc["nextSummary"] = String(c.nextSummary);
  doc["nextInMin"] = c.nextMsToStart / 60000;
  doc["nextStarted"] = c.nextMsToStart <= 0;
  doc["nextLeadMin"] = c.nextLeadMin;
  const CalendarReaction r = calendarReactionNow();
  JsonObject reaction = doc["reaction"].to<JsonObject>();
  reaction["active"] = r.active;
  reaction["phase"] = calendarPhaseName(r.phase);
  reaction["intensity"] = r.intensity;
  reaction["msToStart"] = r.msToStart;
}

static ApiResult apiCalendarStatus() {
  ApiResult r;
  JsonDocument doc;
  // The struct lives here, in the same scope as `serializeJson`, so every string
  // ArduinoJson points at is still valid when the body is written.
  const CalendarStatusInfo c = calendarStatusInfo();
  fillCalendarJson(doc, c);
  serializeJson(doc, r.body);
  return r;
}

static ApiResult apiCalendarCalendars() {
  ApiResult r;
  JsonDocument doc;
  JsonArray items = doc["items"].to<JsonArray>();
  CalendarInfo info;
  for (size_t i = 0; calendarCalendarAt(i, &info); i++) {
    JsonObject item = items.add<JsonObject>();
    // Copia explícita: si se guardara el puntero, los tres calendarios
    // apuntarían al último que escribió `info`.
    item["id"] = String(info.id);
    item["name"] = String(info.name);
    item["primary"] = info.primary;
  }
  serializeJson(doc, r.body);
  return r;
}

// The day's agenda, as the device already has it in memory. The copy lives here,
// in the same scope as `serializeJson`, so every string ArduinoJson points at is
// still valid when the body is written.
static ApiResult apiCalendarEvents() {
  ApiResult r;
  CalendarEvent events[CALENDAR_MAX_EVENTS];
  const size_t count = calendarEventsCopy(events, CALENDAR_MAX_EVENTS);
  JsonDocument doc;
  calendarEventsJson(events, count, settingsGet().tzOffsetHours,
                     (int64_t)time(nullptr), doc);
  serializeJson(doc, r.body);
  return r;
}

// The month grid, fetched on demand. The face's poll only ever holds today plus a
// day and lives in internal RAM, so a month is not something the board keeps: it
// is asked for, drawn and dropped. The rows live in PSRAM and the answer is a few
// kilobytes, which the internal heap already handles.
//
// It blocks the web task for one TLS round trip. That is the cost of asking Google
// from the request that needs the answer, and it is bounded by the same timeout as
// every other calendar request.
static ApiResult apiCalendarMonth() {
  const int year = s_server.arg("y").toInt();
  const int month = s_server.arg("m").toInt();
  if (year <= 0 || month <= 0) {
    return apiFail(400, "Falta el mes: /api/calendar/month?y=2026&m=11");
  }

  CalendarMonthEvent *events = (CalendarMonthEvent *)heap_caps_malloc(
      CALENDAR_MONTH_MAX_EVENTS * sizeof(CalendarMonthEvent),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!events) return apiFail(503, "No hay memoria para el mes");
  struct Release {
    CalendarMonthEvent *rows;
    ~Release() { heap_caps_free(rows); }
  } release{events};

  CalendarMonthInfo info;
  size_t count = 0;
  char error[96] = "";
  if (!calendarFetchMonth(year, month, events, CALENDAR_MONTH_MAX_EVENTS, &count,
                          &info, error, sizeof(error))) {
    return apiFail(502, error[0] ? error : "No pude pedir el mes");
  }

  ApiResult r;
  JsonDocument doc;
  calendarMonthJson(events, count, info, doc);
  serializeJson(doc, r.body);
  return r;
}

// Empty means "leave what is stored", exactly like the Spotify card: the panel
// never gets a secret back, so it cannot resend it. A changed client pair drops
// the old grant, because a refresh token belongs to the client that minted it.
static ApiResult apiCalendarConfig(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
  const char *clientId = doc["clientId"] | "";
  const char *clientSecret = doc["clientSecret"] | "";

  bool changed = false;
  if (*clientId) {
    changed = strcmp(clientId, calendarStoreClientId()) != 0;
    if (!calendarStoreSetClientId(clientId)) {
      return apiFail(400, "Client ID invalido (8 a 120 caracteres sin espacios)");
    }
  }
  if (*clientSecret) {
    changed = changed || strcmp(clientSecret, calendarStoreClientSecret()) != 0;
    if (!calendarStoreSetClientSecret(clientSecret)) {
      return apiFail(400,
                     "Client secret invalido (8 a 120 caracteres sin espacios)");
    }
  }
  if (doc["fallbackLeadMin"].is<int>()) {
    if (!calendarStoreSetFallbackLeadMin(doc["fallbackLeadMin"] | 0)) {
      return apiFail(400, "El aviso por defecto va de 1 a 720 minutos");
    }
  }
  if (doc["graceMin"].is<int>()) {
    if (!calendarStoreSetGraceMin(doc["graceMin"] | 0)) {
      return apiFail(400, "La gracia va de 0 a 120 minutos");
    }
  }
  if (changed) {
    calendarStoreForgetToken();
    calendarAuthResetAccess();
    calendarCancelFlow();
  }
  return apiCalendarStatus();
}

static ApiResult apiCalendarConnect() {
  if (!calendarStoreConfigured()) {
    return apiFail(409, "Guarda el Client ID y el secret primero");
  }
  char error[96];
  if (!calendarStartFlow(error, sizeof(error))) {
    return apiFail(502, error[0] ? error : "No pude empezar la conexion con Google");
  }
  return apiCalendarStatus();
}

static ApiResult apiCalendarCancel() {
  calendarCancelFlow();
  return apiCalendarStatus();
}

static ApiResult apiCalendarSelect(const String &body) {
  JsonDocument doc;
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);
  const char *id = doc["calendarId"] | "";
  if (!*id) return apiFail(400, "Falta el calendario");
  const char *name = doc["name"] | "";
  if (!calendarSelect(id, name)) {
    return apiFail(400, "El calendario no se pudo guardar");
  }
  return apiCalendarStatus();
}

static ApiResult apiCalendarForget() {
  calendarCancelFlow();
  calendarAuthResetAccess();
  calendarStoreForgetAll();
  return apiCalendarStatus();
}

static ApiResult apiCalendarRefresh() {
  if (!calendarRequestRefresh()) {
    return apiFail(409, "Falta conectar Google");
  }
  return apiOk();
}

// --- weather ----------------------------------------------------------------
//
// The weather card lives in Integraciones. The place is the owner's, so every
// handler needs a session; Open-Meteo itself is keyless, which is why there is no
// credential to store or hide here.
static const char *weatherKindName(WeatherStatusKind kind) {
  switch (kind) {
    case WEATHER_UNCONFIGURED: return "unconfigured";
    case WEATHER_NO_DATA: return "no_data";
    case WEATHER_ERROR: return "error";
    case WEATHER_READY: break;
  }
  return "ready";
}

// Received by reference on purpose: ArduinoJson keeps pointers to the `char[]`
// inside, so the struct has to stay alive until `serializeJson` runs.
static void fillWeatherJson(JsonDocument &doc, const WeatherStatusInfo &w) {
  doc["ok"] = true;
  doc["kind"] = weatherKindName(w.kind);
  doc["configured"] = w.configured;
  doc["hasData"] = w.hasData;
  doc["syncing"] = w.syncing;
  doc["refreshMin"] = w.refreshMin;
  doc["lastAttemptMs"] = w.lastAttemptMs;
  doc["lastSuccessMs"] = w.lastSuccessMs;
  doc["lastError"] = String(w.lastError);

  JsonObject place = doc["place"].to<JsonObject>();
  place["valid"] = w.place.valid;
  place["name"] = String(w.place.name);
  place["lat"] = w.place.lat;
  place["lon"] = w.place.lon;

  JsonObject now = doc["now"].to<JsonObject>();
  now["valid"] = w.hasData;
  now["tempC"] = w.now.tempC;
  now["feelsC"] = w.now.feelsC;
  now["humidity"] = w.now.humidity;
  now["windKph"] = w.now.windKph;
  now["tmaxC"] = w.now.tmaxC;
  now["tminC"] = w.now.tminC;
  now["code"] = w.now.code;
  now["label"] = weatherCodeLabel(w.now.code);
  now["icon"] = weatherCodeIcon(w.now.code);
  now["isDay"] = w.now.isDay;
}

static ApiResult apiWeatherStatus() {
  ApiResult r;
  JsonDocument doc;
  const WeatherStatusInfo w = weatherStatusInfo();
  fillWeatherJson(doc, w);
  serializeJson(doc, r.body);
  return r;
}

static ApiResult apiWeatherLocation(const String &body) {
  JsonDocument doc;
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);
  if (!doc["lat"].is<float>() || !doc["lon"].is<float>()) {
    return apiFail(400, "Faltan las coordenadas");
  }
  const char *name = doc["name"] | "";
  char error[96];
  if (!weatherSavePlace(doc["lat"].as<float>(), doc["lon"].as<float>(), name,
                        error, sizeof(error))) {
    return apiFail(400, error[0] ? error : "No pude guardar la ubicacion");
  }
  return apiWeatherStatus();
}

static ApiResult apiWeatherDetect() {
  char error[96];
  if (!weatherDetectPlace(error, sizeof(error))) {
    return apiFail(502, error[0] ? error : "No pude detectar la ubicacion");
  }
  return apiWeatherStatus();
}

static ApiResult apiWeatherSearch(const String &body) {
  JsonDocument doc;
  if (!parseRequiredJson(body, doc)) return apiFail(400, API_BAD_JSON);
  const char *query = doc["q"] | "";
  WeatherGeoResult results[WEATHER_GEOCODE_MAX];
  char error[96];
  const size_t count =
      weatherSearch(query, results, WEATHER_GEOCODE_MAX, error, sizeof(error));
  if (count == 0) {
    return apiFail(404, error[0] ? error : "No encontre esa ciudad");
  }
  ApiResult r;
  JsonDocument out;
  out["ok"] = true;
  JsonArray items = out["results"].to<JsonArray>();
  for (size_t i = 0; i < count; i++) {
    JsonObject item = items.add<JsonObject>();
    // Copies, not pointers: the same struct would be reused for every result.
    item["name"] = String(results[i].name);
    item["region"] = String(results[i].region);
    item["country"] = String(results[i].country);
    item["lat"] = results[i].lat;
    item["lon"] = results[i].lon;
  }
  serializeJson(out, r.body);
  return r;
}

static ApiResult apiWeatherRefresh() {
  if (!weatherRequestRefresh()) {
    return apiFail(409, "Falta guardar la ubicacion");
  }
  return apiWeatherStatus();
}

static ApiResult apiWeatherConfig(const String &body) {
  JsonDocument doc;
  if (!parseOptionalJson(body, doc)) return apiFail(400, API_BAD_JSON);
  if (doc["refreshMin"].is<int>()) {
    if (!weatherSetRefreshMin(doc["refreshMin"] | 0)) {
      return apiFail(400, "El refresco va de 5 a 180 minutos");
    }
  }
  return apiWeatherStatus();
}

static ApiResult apiWeatherForget() {
  weatherForget();
  return apiWeatherStatus();
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
  adminSessionOpen(&s_sessions, now, (int64_t)time(nullptr), raw);
  adminSessionStoreSave(&s_sessions);

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
    "/api/face",
    "/api/focus",
    "/api/lcd",            "/api/led",
    "/api/press",          "/api/scores/reset", "/api/steer/reset",
    "/api/steer/swap",     "/api/restart",     "/api/factory",
    "/api/spotify/config", "/api/spotify/url", "/api/spotify/code",
    "/api/spotify/forget", "/api/screens/order", "/api/screens/names",
    "/api/panel/colors", "/api/buddy/ask", "/api/admin/credentials",
    "/api/calendar/config", "/api/calendar/connect", "/api/calendar/cancel",
    "/api/calendar/select", "/api/calendar/refresh", "/api/calendar/forget",
    "/api/weather/location", "/api/weather/detect", "/api/weather/search",
    "/api/weather/refresh", "/api/weather/config", "/api/weather/forget",
};
static const uint32_t POST_PATH_COUNT = sizeof(POST_PATHS) / sizeof(POST_PATHS[0]);

static bool isPostOnlyPath(const char *path) {
  for (uint32_t i = 0; i < POST_PATH_COUNT; i++) {
    if (!strcmp(path, POST_PATHS[i])) return true;
  }
  return false;
}

static ApiResult dispatchApi(const char *method, const char *path,
                             const String &body) {
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

  if (!strcmp(path, "/api/status") || !strcmp(path, "/api/spotify") ||
      !strcmp(path, "/api/calendar") ||
      !strcmp(path, "/api/calendar/calendars") ||
      !strcmp(path, "/api/calendar/events") ||
      !strcmp(path, "/api/calendar/month") ||
      !strcmp(path, "/api/weather")) {
    if (isPost) return apiFail(405, "Metodo no permitido");
    if (!strcmp(path, "/api/status")) {
      ApiResult r;
      buildStatusJson(r.body);
      return r;
    }
    if (!strcmp(path, "/api/calendar")) return apiCalendarStatus();
    if (!strcmp(path, "/api/calendar/calendars")) return apiCalendarCalendars();
    if (!strcmp(path, "/api/calendar/events")) return apiCalendarEvents();
    if (!strcmp(path, "/api/calendar/month")) return apiCalendarMonth();
    if (!strcmp(path, "/api/weather")) return apiWeatherStatus();
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
  if (!strcmp(path, "/api/calendar/config")) return apiCalendarConfig(body);
  if (!strcmp(path, "/api/calendar/connect")) return apiCalendarConnect();
  if (!strcmp(path, "/api/calendar/cancel")) return apiCalendarCancel();
  if (!strcmp(path, "/api/calendar/select")) return apiCalendarSelect(body);
  if (!strcmp(path, "/api/calendar/refresh")) return apiCalendarRefresh();
  if (!strcmp(path, "/api/calendar/forget")) return apiCalendarForget();
  if (!strcmp(path, "/api/weather/location")) return apiWeatherLocation(body);
  if (!strcmp(path, "/api/weather/detect")) return apiWeatherDetect();
  if (!strcmp(path, "/api/weather/search")) return apiWeatherSearch(body);
  if (!strcmp(path, "/api/weather/refresh")) return apiWeatherRefresh();
  if (!strcmp(path, "/api/weather/config")) return apiWeatherConfig(body);
  if (!strcmp(path, "/api/weather/forget")) return apiWeatherForget();
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
  if (!strcmp(path, "/api/lcd")) return apiLcd(body);
  if (!strcmp(path, "/api/led")) return apiLed(body);
  if (!strcmp(path, "/api/screen")) return apiScreen(body);
  if (!strcmp(path, "/api/face")) return apiFace(body);
  if (!strcmp(path, "/api/focus")) return apiFocus(body);
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

ApiResult netHandleApi(const char *method, const char *path, const String &body) {
  ApiResult result = dispatchApi(method, path, body);
  // The one place the POST router's outcome is known, so no branch can forget it
  // and none can count a request that was rejected. A POST that went through is
  // the owner using the board (playing the web game, picking a screen), which is
  // a stimulus like a shake. A GET never is: the landing polls /api/status every
  // ten seconds, and a poll is not a person.
  if (strcmp(method, "POST") == 0 && result.code == 200) {
    buddyNoteUserActivity();
  }
  return result;
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
  return adminSessionValid(&s_sessions, token.c_str(), millis(),
                           (int64_t)time(nullptr));
}

void netSessionForgetAll() {
  adminSessionReset(&s_sessions);
  adminSessionStoreClear();
}

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
  if (token.length() > 0) {
    adminSessionClose(&s_sessions, token.c_str());
    adminSessionStoreSave(&s_sessions);
  }
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
    adminSessionStoreSave(&s_sessions);
  }
  // Rotating the credentials logs out every other device; the caller keeps the
  // session it used to make the change, which is the one that still works.
  if (result.code == 200 && s_server.uri() == "/api/admin/credentials") {
    adminSessionCloseOthers(&s_sessions, requestSessionToken().c_str());
    adminSessionStoreSave(&s_sessions);
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

// Puts the AP where the policy says it belongs, and says so on the console when
// it moves. Toggling the AP interface is used instead of WiFi.mode(), because
// changing the global mode tears the station link down and the whole point of
// the change is a link that stays up.
static void applyApPolicy(bool staConnected, bool justConnected,
                          uint32_t nowMs) {
  if (justConnected) s_staUpSince = nowMs;
  uint32_t upFor = staConnected ? (uint32_t)(nowMs - s_staUpSince) : 0;

  bool want = netApShouldRun(s_apMode, staConnected, upFor);
  if (want == s_apRunning) return;
  s_apRunning = want;

  txReliable(true);
  if (want) {
    WiFi.enableAP(true);
    startAccessPoint();
    Serial.printf("ap on   (%s, sta %s): http://%s\n",
                  netApModeName(s_apMode), staConnected ? "conectado" : "caido",
                  NET_AP_IP);
  } else {
    // Every client is dropped, sessions included: the panel continues on the
    // station address, which the NETWORK screen and /api/status already show.
    WiFi.softAPdisconnect(true);
    WiFi.enableAP(false);
    Serial.printf("ap off  (%s, sta conectado %lus): panel en http://%s\n",
                  netApModeName(s_apMode), (unsigned long)(upFor / 1000),
                  s_status.staIp);
  }
  txReliable(false);
}

uint8_t netApMode() { return s_apMode; }

void netSetApMode(uint8_t mode) {
  if (mode > NET_AP_FORCE_OFF) mode = NET_AP_AUTO;
  s_apMode = mode;
  // Applied here and not on the next tick: the console is the way back when the
  // AP is off, and a command that prints "activo=no" right after `ap on` reads
  // as a command that did nothing. The policy is idempotent, so netLoop's own
  // call is a no-op when nothing changed.
  applyApPolicy(s_status.staConnected, false, millis());
  refreshStatus();
}

void netBegin() {
  // La sesión del panel sobrevive el reboot: la app de bandeja no tiene por qué
  // pedir usuario y clave cada vez que el equipo se reinicia. Se carga acá, y
  // nada la vuelve a vaciar en el arranque (sólo un logout o el reset de
  // fábrica, que pasan por `netSessionForgetAll`).
  adminSessionStoreBegin();
  adminSessionStoreLoad(&s_sessions);

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

  // Las sesiones y el throttle del login NO arrancan vacíos: la tabla se cargó
  // desde NVS al principio de netBegin (`adminSessionStoreLoad`), que es lo que
  // hace que el popover no pida usuario y clave después de cada reboot. El
  // throttle sí es por arranque: los intentos fallidos no se guardan.
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
  s_server.on("/api/calendar", HTTP_GET, handleApi);
  s_server.on("/api/calendar/calendars", HTTP_GET, handleApi);
  s_server.on("/api/calendar/events", HTTP_GET, handleApi);
  s_server.on("/api/calendar/month", HTTP_GET, handleApi);
  s_server.on("/api/weather", HTTP_GET, handleApi);

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
  bool justConnected = staConnected && !s_staWasConnected;
  if (justConnected) {
    netApplyTimeZone();
    txReliable(true);
    Serial.println("sta conectado -> NTP re-armado, esperando la hora");
    txReliable(false);
  }
  s_staWasConnected = staConnected;
  applyApPolicy(staConnected, justConnected, millis());

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

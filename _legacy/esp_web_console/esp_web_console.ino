/*
 * esp_web_console - WiFi control console for the Waveshare ESP32-S3-LCD-1.47B.
 *
 * The board raises its own access point and serves a web app from flash, so
 * there is nothing to install and nothing to build on the PC: join
 * "ESP-LCD-1.47B" and open http://192.168.4.1.
 *
 * What the web app can do:
 *   - paint the 172x320 panel (solid colour, test bars, or its own picture)
 *   - set the display backlight
 *   - drive the WS2812 bead: colour, intensity, solid / rainbow / off
 *   - watch live accelerometer and gyroscope data pushed over the socket
 *
 * Transport layout:
 *   port 80 - HTTP: the web app itself, /api/state, /api/image, /api/wifi
 *   port 81 - WebSocket: commands in, telemetry out
 *
 * Images are decoded by the browser (canvas, 172x320, cover fit) and uploaded
 * as raw RGB565 in panel byte order, so the board needs no decoder at all.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <esp32-hal-psram.h>

#include "Display_ST7789.h"
#include "imu.h"
#include "rgb.h"
#include "web_assets.h"

static const char *AP_SSID = "ESP-LCD-1.47B";
static const char *AP_PASS = "lcdlab1234";

WebServer http(80);
WebSocketsServer ws(81);

// Full frame in panel byte order (high byte first per pixel).
static uint8_t *s_frame = nullptr;
static uint16_t *s_framePx = nullptr;

static uint8_t s_backlight = 80;
static uint32_t s_lastTelemetry = 0;
static uint32_t s_bootedAt = 0;

// --------------------------------------------------------------- display ---

static void pushFrame() { LCD_PushFullFrameBytes(s_frame); }

static void fillScreen(uint16_t rgb565) {
  uint16_t swapped = LCD_Swap16(rgb565);
  for (uint32_t i = 0; i < LCD_PIXELS; i++) s_framePx[i] = swapped;
  pushFrame();
}

static void drawTestBars() {
  static const uint16_t bars[8] = {0xF800, 0x07E0, 0x001F, 0x07FF,
                                   0xFFE0, 0xF81F, 0xFFFF, 0x0000};
  const int barH = LCD_HEIGHT / 8;
  for (int b = 0; b < 8; b++) {
    uint16_t sw = LCD_Swap16(bars[b]);
    int y0 = b * barH;
    int y1 = (b == 7) ? LCD_HEIGHT : y0 + barH;
    for (int y = y0; y < y1; y++) {
      uint16_t *row = s_framePx + y * LCD_WIDTH;
      for (int x = 0; x < LCD_WIDTH; x++) row[x] = sw;
    }
  }
  pushFrame();
}

static void drawBootSplash() {
  // Vertical gradient so the panel is obviously alive before any client joins.
  for (int y = 0; y < LCD_HEIGHT; y++) {
    uint8_t t = (uint8_t)((uint32_t)y * 255 / (LCD_HEIGHT - 1));
    uint16_t c = (uint16_t)(((20 + t / 4) & 0xF8) << 8);  // dark blue ramp
    uint16_t sw = LCD_Swap16(c);
    uint16_t *row = s_framePx + y * LCD_WIDTH;
    for (int x = 0; x < LCD_WIDTH; x++) row[x] = sw;
  }
  pushFrame();
}

static bool parseRgb(const char *hex, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (!hex) return false;
  if (*hex == '#') hex++;
  char *end = nullptr;
  unsigned long value = strtoul(hex, &end, 16);
  if (end == hex) return false;
  r = (uint8_t)((value >> 16) & 0xFF);
  g = (uint8_t)((value >> 8) & 0xFF);
  b = (uint8_t)(value & 0xFF);
  return true;
}

// #rrggbb -> RGB565.
// The previous version truncated the 24-bit value to 16 bits, which is not a
// colour conversion at all: #00ff00 became 0xFF00 and the panel showed the
// wrong colour. Each channel has to be cut down to its RGB565 width.
static uint16_t parseHexColor(const char *hex) {
  uint8_t r, g, b;
  if (!parseRgb(hex, r, g, b)) return 0;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// --------------------------------------------------------------- http ------

static void sendAsset(const WebAsset &asset) {
  http.sendHeader("Cache-Control", "max-age=600");
  http.send_P(200, asset.mime, (PGM_P)asset.data, asset.len);
}

static void handleRoot() {
  for (uint32_t i = 0; i < WEB_ASSET_COUNT; i++) {
    if (!strcmp(WEB_ASSETS[i].path, "/index.html")) {
      sendAsset(WEB_ASSETS[i]);
      return;
    }
  }
  http.send(500, "text/plain", "index.html missing from the packed assets");
}

static void handleNotFound() {
  String path = http.uri();
  for (uint32_t i = 0; i < WEB_ASSET_COUNT; i++) {
    if (path == WEB_ASSETS[i].path) {
      sendAsset(WEB_ASSETS[i]);
      return;
    }
  }
  http.send(404, "text/plain", "not found: " + path);
}

static void handleState() {
  uint8_t r, g, b;
  rgbGetColor(r, g, b);

  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"r\":%u,\"g\":%u,\"b\":%u,\"level\":%u,\"effect\":%u,"
           "\"bl\":%u,\"uptime\":%lu,\"heap\":%u,\"clients\":%u,\"ap\":\"%s\"}",
           r, g, b, rgbLevel(), rgbEffect(), s_backlight,
           (unsigned long)((millis() - s_bootedAt) / 1000),
           (unsigned)ESP.getFreeHeap(), (unsigned)ws.connectedClients(),
           WiFi.softAPIP().toString().c_str());
  http.send(200, "application/json", String(buf));
}

// Image upload: the browser posts LCD_PIXELS*2 raw panel-order bytes.
static uint32_t s_uploaded = 0;
static bool s_uploadOk = false;

static void handleImageUpload() {
  HTTPUpload &up = http.upload();
  const uint32_t expected = (uint32_t)LCD_PIXELS * 2;

  if (up.status == UPLOAD_FILE_START) {
    s_uploaded = 0;
    s_uploadOk = true;
    Serial.printf("[img] upload start (%u bytes announced)\n", (unsigned)up.totalSize);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_uploadOk && s_uploaded + up.currentSize <= expected) {
      memcpy(s_frame + s_uploaded, up.buf, up.currentSize);
      s_uploaded += up.currentSize;
    } else {
      s_uploadOk = false;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    Serial.printf("[img] upload end, %u of %u bytes\n", (unsigned)s_uploaded,
                  (unsigned)expected);
  }
}

static void handleImagePost() {
  const uint32_t expected = (uint32_t)LCD_PIXELS * 2;
  if (!s_uploadOk || s_uploaded != expected) {
    http.send(400, "application/json",
              String("{\"ok\":false,\"got\":") + s_uploaded + ",\"want\":" +
                  expected + "}");
    return;
  }
  pushFrame();
  http.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiPost() {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, http.arg("plain")) != DeserializationError::Ok) {
    http.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  const char *ssid = doc["ssid"] | "";
  const char *pass = doc["pass"] | "";
  if (!*ssid) {
    http.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  http.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  delay(300);
  ESP.restart();
}

// ---------------------------------------------------------------- ws -------

static void sendTelemetry(bool force) {
  uint32_t now = millis();
  if (!force && now - s_lastTelemetry < 200) return;
  s_lastTelemetry = now;

  float a[3], g[3];
  imuReadAll(a, g);
  uint8_t r, gg, b;
  rgbGetColor(r, gg, b);

  char buf[288];
  int n = snprintf(buf, sizeof(buf),
                   "{\"t\":\"state\",\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
                   "\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,"
                   "\"r\":%u,\"g\":%u,\"b\":%u,\"level\":%u,\"effect\":%u,"
                   "\"bl\":%u,\"up\":%lu,\"heap\":%u,\"clients\":%u}",
                   a[0], a[1], a[2], g[0], g[1], g[2], r, gg, b, rgbLevel(),
                   rgbEffect(), s_backlight,
                   (unsigned long)((now - s_bootedAt) / 1000),
                   (unsigned)ESP.getFreeHeap(), (unsigned)ws.connectedClients());
  if (n > 0) ws.broadcastTXT((const uint8_t *)buf, (size_t)n);
}

static void applyCommand(const char *json, size_t len) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    Serial.printf("[ws] bad json: %s\n", err.c_str());
    return;
  }
  const char *type = doc["t"] | "";
  Serial.printf("[ws] cmd %s\n", type);

  if (!strcmp(type, "rgb")) {
    rgbSetColor(doc["r"] | 0, doc["g"] | 0, doc["b"] | 0);
    rgbSetEffect(RGB_EFFECT_SOLID);
  } else if (!strcmp(type, "level")) {
    rgbSetLevel((uint8_t)(doc["v"] | 0));
  } else if (!strcmp(type, "effect")) {
    const char *name = doc["v"] | "solid";
    if (!strcmp(name, "rainbow")) {
      rgbSetEffect(RGB_EFFECT_RAINBOW);
    } else if (!strcmp(name, "off")) {
      rgbSetEffect(RGB_EFFECT_OFF);
    } else {
      rgbSetEffect(RGB_EFFECT_SOLID);
      rgbSetColor(doc["r"] | 0, doc["g"] | 0, doc["b"] | 0);
    }
  } else if (!strcmp(type, "bl")) {
    int v = doc["v"] | 80;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_backlight = (uint8_t)v;
    Set_Backlight(s_backlight);
  } else if (!strcmp(type, "fill")) {
    fillScreen(parseHexColor(doc["c"] | "#000000"));
  } else if (!strcmp(type, "bars")) {
    drawTestBars();
  } else if (!strcmp(type, "ping")) {
    ws.broadcastTXT("{\"t\":\"pong\"}");
  } else {
    Serial.printf("[ws] unknown command '%s'\n", type);
    return;
  }
  sendTelemetry(true);
}

static void onWsEvent(uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = ws.remoteIP(client);
      Serial.printf("[ws] client %u connected from %s\n", client, ip.toString().c_str());
      char hello[192];
      int n = snprintf(hello, sizeof(hello),
                       "{\"t\":\"hello\",\"fw\":\"esp_web_console\",\"w\":%d,\"h\":%d,"
                       "\"clients\":%u}",
                       LCD_WIDTH, LCD_HEIGHT, (unsigned)ws.connectedClients());
      ws.sendTXT(client, (const uint8_t *)hello, (size_t)n);
      sendTelemetry(true);
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("[ws] client %u disconnected\n", client);
      break;
    case WStype_TEXT:
      applyCommand((const char *)payload, length);
      break;
    default:
      break;
  }
}

// -------------------------------------------------------------- startup ----

static bool startWifi() {
  Preferences prefs;
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.length()) {
    Serial.printf("trying STA on '%s'\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("STA connected, ip=%s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("STA failed, falling back to the access point");
  }

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP '%s' %s, ip=%s, rssi n/a\n", AP_SSID, ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());
  return ok;
}

// ------------------------------------------------------- serial console ----
//
// The same handful of things the web app can do, reachable over USB. It turns
// the panel into something a test can drive without a browser attached: lower
// the backlight so a camera stops clipping, draw the test pattern, fill a
// colour. Small on purpose - the web app is the real interface.
static char s_cmd[64];
static uint8_t s_cmdLen = 0;

static void handleSerialLine(char *line) {
  while (*line == ' ') line++;
  if (!*line) return;

  if (!strncmp(line, "bl ", 3)) {
    int v = atoi(line + 3);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_backlight = (uint8_t)v;
    Set_Backlight(s_backlight);
    Serial.printf("bl=%u\n", s_backlight);
  } else if (!strcmp(line, "bars")) {
    drawTestBars();
    Serial.println("drew test bars");
  } else if (!strncmp(line, "fill ", 5)) {
    fillScreen(parseHexColor(line + 5));
    Serial.printf("filled %s\n", line + 5);
  } else if (!strcmp(line, "state")) {
    Serial.printf("bl=%u clients=%u heap=%u\n", s_backlight,
                  (unsigned)ws.connectedClients(), (unsigned)ESP.getFreeHeap());
  } else if (!strncmp(line, "rgb ", 4)) {
    uint8_t r, g, b;
    if (parseRgb(line + 4, r, g, b)) {
      rgbSetColor(r, g, b);
      rgbSetEffect(RGB_EFFECT_SOLID);
      rgbTick();
      Serial.printf("rgb=%u,%u,%u\n", r, g, b);
    }
  } else if (!strncmp(line, "hex ", 4)) {
    // Echoes what the firmware computes for a colour, so the conversion can be
    // checked without looking at the panel.
    Serial.printf("%s -> rgb565 0x%04X\n", line + 4, parseHexColor(line + 4));
  } else if (!strcmp(line, "?")) {
    Serial.println("commands: bl <0-100> | bars | fill <rrggbb> | rgb <rrggbb> | hex <rrggbb> | state");
  } else {
    Serial.println("unknown, try ?");
  }
}

static void pollSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_cmd[s_cmdLen] = '\0';
      handleSerialLine(s_cmd);
      s_cmdLen = 0;
      continue;
    }
    if (s_cmdLen < sizeof(s_cmd) - 1) s_cmd[s_cmdLen++] = c;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block the loop on an absent host
  delay(200);
  Serial.println("\nesp_web_console: starting");
  s_bootedAt = millis();

  s_frame = (uint8_t *)ps_malloc((uint32_t)LCD_PIXELS * 2);
  if (!s_frame) {
    Serial.println("FATAL: no PSRAM for the frame buffer");
    while (true) delay(1000);
  }
  s_framePx = (uint16_t *)s_frame;

  LCD_Init();
  Set_Backlight(s_backlight);
  drawBootSplash();
  Serial.println("display ready");

  rgbBegin();
  rgbSetColor(80, 120, 255);
  rgbTick();

  if (imuBegin()) {
    Serial.println("QMI8658 ready");
  } else {
    Serial.println("WARN: QMI8658 did not answer");
  }

  if (!startWifi()) {
    Serial.println("FATAL: could not start networking");
    while (true) delay(1000);
  }

  http.on("/", HTTP_GET, handleRoot);
  http.on("/api/state", HTTP_GET, handleState);
  http.on("/api/image", HTTP_POST, handleImagePost, handleImageUpload);
  http.on("/api/wifi", HTTP_POST, handleWifiPost);
  http.onNotFound(handleNotFound);
  http.begin();

  ws.begin();
  ws.onEvent(onWsEvent);

  Serial.printf("http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("ws://%s:81\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("web assets packed: %u\n", (unsigned)WEB_ASSET_COUNT);
}

void loop() {
  http.handleClient();
  ws.loop();
  rgbTick();
  sendTelemetry(false);
  pollSerial();

  static uint32_t lastBeat = 0;
  if (millis() - lastBeat >= 10000) {
    lastBeat = millis();
    Serial.printf("[hb] clients=%u heap=%u bl=%u\n", (unsigned)ws.connectedClients(),
                  (unsigned)ESP.getFreeHeap(), s_backlight);
  }
}

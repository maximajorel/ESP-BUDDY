// Serial console implementation: command line, dumps and diagnostics.
//
// The console is how the gadget is verified without a readable panel bus.
// Keeping it out of the sketch means `esp32_gadget.ino` is only boot, button
// reading and the main loop, and this file can be read as one unit when a
// console command misbehaves.
#include "console.h"

#include <Arduino.h>
#include <WiFi.h>
#include <sys/time.h>

#include "button.h"
#include "display.h"
#include "gfx.h"
#include "imu.h"
#include "net.h"
#include "net_ap_policy.h"
#include "rgb.h"
#include "rotation.h"
#include "buddy_phase.h"
#include "calendar.h"
#include "calendar_plan.h"
#include "screen_buddy.h"
#include "screens.h"
#include "settings.h"
#include "spotify.h"
#include "spotify_art.h"
#include "spotify_parse.h"

static uint32_t s_synthDownUntil = 0;

static char s_line[128];
static size_t s_lineLen = 0;

// --- helpers ----------------------------------------------------------------

bool consoleSyntheticPressActive(uint32_t nowMs) {
  if (s_synthDownUntil == 0) return false;
  if ((uint32_t)(s_synthDownUntil - nowMs) > 0x7FFFFFFFul) {
    s_synthDownUntil = 0;  // window elapsed
    return false;
  }
  return true;
}

static void formatLocalTime(char *out, size_t cap) {
  time_t now = time(nullptr);
  if (now < 1600000000) {
    snprintf(out, cap, "not synced");
    return;
  }
  struct tm tmNow;
  localtime_r(&now, &tmNow);
  strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmNow);
}

static void dumpFramebuffer(int step) {
  if (step < 1) step = 1;
  const uint16_t *fb = gfxBuffer();
  if (!fb) {
    Serial.println("no framebuffer");
    return;
  }
  Serial.printf("frame %dx%d step %d\n", DISPLAY_W, DISPLAY_H, step);
  // The firmware normally discards console writes so a closed port can never
  // wedge it. A frame dump is 3.5 KB, which that policy would shred: for the
  // duration of the dump the write is allowed to block instead, so every byte
  // reaches the host. Offsetting it costs at most a fraction of a second while
  // someone is actively reading the console.
  txReliable(true);
  int lines = 0;
  for (int y = 0; y < DISPLAY_H; y += step) {
    char row[DISPLAY_W + 1];
    int n = 0;
    for (int x = 0; x < DISPLAY_W && n < DISPLAY_W; x += step) {
      uint16_t c = fb[(size_t)y * DISPLAY_W + x];
      int r = (c >> 11) & 0x1F;
      int g = (c >> 5) & 0x3F;
      int b = c & 0x1F;
      int lum = r * 2 + g + b * 2;  // roughly 0..255
      row[n++] = lum > 150 ? '#' : lum > 70 ? '+' : lum > 14 ? '.' : ' ';
    }
    row[n] = '\0';
    Serial.println(row);
    lines++;
  }
  // The caller can compare this against what it actually received, so a
  // truncated dump is detectable instead of silently wrong.
  Serial.printf("end step=%d lines=%d cols=%d\n", step, lines,
                (DISPLAY_W + step - 1) / step);
  txReliable(false);
}

// Plain-language reading of esp_wifi's disconnect reasons, limited to the ones
// a home network actually produces.
static const char *reasonHint(int reason) {
  switch (reason) {
    case 2:    // AUTH_EXPIRE
    case 15:   // 4WAY_HANDSHAKE_TIMEOUT
    case 202:  // AUTH_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
      return "clave incorrecta";
    case 8:    // ASSOC_NOT_AUTHED
    case 203:  // ASSOC_FAIL
      return "el router rechazo la asociacion (posible filtro MAC)";
    case 200:  // BEACON_TIMEOUT
      return "senal perdida";
    case 201:  // NO_AP_FOUND
    case 210:
    case 211:
      return "no se encontro la red";
    case 205:  // CONNECTION_FAIL
    case 39:   // TIMEOUT
      return "tiempo agotado sin respuesta";
    case 0:
      return "sin datos todavia";
    default:
      return "ver codigo";
  }
}

static void printStatus() {
  char stamp[32];
  formatLocalTime(stamp, sizeof(stamp));
  uint8_t lat, up;
  int8_t sign, signY;
  uint8_t gain;
  gameGetSteer(&lat, &up, &sign, &signY, &gain);

  const NetStatus &st = netStatus();
  txReliable(true);
  Serial.println("--- status ---");
  Serial.printf("screen      %u (%s)\n", (unsigned)screensCurrent(),
                screenName(screensCurrent()));
  Serial.printf("orient      %u (%s%s)\n", (unsigned)displayGetOrientation(),
                rotationModeName(), rotationLocked() ? ", locked" : "");
  Serial.printf("backlight   %u\n", (unsigned)displayGetBacklight());
  Serial.printf("imu         %s\n", g_imuOk ? "ok" : "MISSING");
  Serial.printf("steer       lat=%u up=%u sign=%d signY=%d gain=%u\n",
                (unsigned)lat, (unsigned)up, (int)sign, (int)signY,
                (unsigned)gain);
  Serial.printf("ap          %s  key=%s  active=%s  http://%s  clients=%u\n",
                settingsGet().apSsid, settingsGet().apPass,
                st.apActive ? "yes" : "NO", NET_AP_IP,
                (unsigned)st.apClients);
  Serial.printf("sta mac     %s\n", WiFi.macAddress().c_str());
  Serial.printf("sta reason  %d (%s)\n", st.disconnectReason,
                reasonHint(st.disconnectReason));
  Serial.printf("sta state   %s (wifi code %d)\n",
                st.staState == NET_STA_OK         ? "conectado"
                : st.staState == NET_STA_FAILED   ? "FALLO"
                : st.staState == NET_STA_IDLE     ? "sin configurar"
                                                  : "conectando",
                st.wifiStatusCode);
  if (netLastError()[0]) Serial.printf("ap error    %s\n", netLastError());
  Serial.printf("sta         %s  ssid=%s  ip=%s  rssi=%d\n",
                st.staConnected ? "connected" : "down", st.staSsid, st.staIp,
                (int)st.staRssi);
  Serial.printf("time        %s  tz=%+d\n", stamp,
                (int)settingsGet().tzOffsetHours);
  Serial.printf("scores      game=%lu slot=%u\n",
                (unsigned long)settingsGet().gameHighScore,
                (unsigned)settingsGet().slotHighScore);
  Serial.printf("heap        %lu free, psram %lu free\n",
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getFreePsram());
  txReliable(false);
}

// Thermal and power diagnostics. The board is sealed and has no readable bus, so
// the radio and backlight state plus the heap are the only honest way to tell
// "the firmware is burning cycles" apart from "the radio is on and the glass is
// lit". One line on purpose: a probe samples it every few seconds and the values
// only mean something when compared across runs.
//
// No temperatureRead() here, and it must not come back. The Arduino core reads
// the internal sensor through the same hardware the WiFi PHY uses: polling it
// while the radio is up wedged two boards hard enough that even the ROM
// bootloader stopped answering, and unplugging was the only way out. The chip's
// number was also out of the sensor's configured range (the core installs it for
// 10-50 C and this board idles above 75), so it was never a thermometer. Heat is
// measured from outside now.
static void printTemp() {
  const NetStatus &st = netStatus();
  Serial.printf(
      "temp cpu=%lud  heap=%lu psram=%lu  ap=%s clients=%u  "
      "sta=%s rssi=%d  mode=%u  bl=%u rgb=%u\n",
      (unsigned long)getCpuFrequencyMhz(),
      (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram(),
      st.apActive ? "on" : "off", (unsigned)st.apClients,
      st.staConnected ? "ok" : (st.staConfigured ? "down" : "unset"),
      (int)st.staRssi, (unsigned)(WiFi.getMode()),
      (unsigned)displayGetBacklight(), (unsigned)rgbLevel());
}

static void printImu() {
  if (!g_imuOk) {
    Serial.println("imu missing");
    return;
  }
  float a[3], g[3];
  imuReadAll(a, g);
  Serial.printf("accel x=%+.2fg y=%+.2fg z=%+.2fg\n", a[0], a[1], a[2]);
  Serial.printf("gyro  x=%+.1f y=%+.1f z=%+.1f dps\n", g[0], g[1], g[2]);
}

static void printHelp() {
  Serial.println("commands:");
  Serial.println("  help                       this list");
  Serial.println("  status                     device state");
  Serial.println("  screen <0-6>               reloj, tragamonedas, meteoritos, red, musica, sofia, config");
  Serial.println("  press [ms]                 synthetic short press (default 120)");
  Serial.println("  doubletap                  two quick short presses, routed to the current screen");
  Serial.println("  tap                        short press without button timing");
  Serial.println("  setship <col> <row>        mueve la nave (pruebas de punteria)");
  Serial.println("  closemenu                  cierra el menu de pantallas");
  Serial.println("  gameage <s>                adelanta el reloj del juego (pruebas)");
  Serial.println("  spawnenemy                 crea un caza ahora (pruebas)");
  Serial.println("  longpress                  1s hold without button timing");
  Serial.println("  hold                       synthetic 1s hold: abre/entra el menu");
  Serial.println("  orient <0-3>               landscape rotation onto the panel (pins it)");
  Serial.println("  rot [auto|0-3|lock on|off] automatic rotation, a pinned image, or the lock");
  Serial.println("  steer <lat> <up> <sign> <signY> <gain>  ejes del juego (0..2, x/y/z)");
  Serial.println("  steercal                   borra la calibracion (se rehace en CONFIG)");
  Serial.println("  imufake <ax> <ay> <az> <gx> <gy> <gz>   sensor simulado");
  Serial.println("  imureal                    vuelve al sensor real");
  Serial.println("  gamestate                  estado, nave, puntaje, nivel, meteoritos y cazas");
  Serial.println("  imu                        one sensor reading");
  Serial.println("  temp                       consumo y estado: CPU, heap, radios, backlight y LED (sin sensor térmico: cuelga la placa)");
  Serial.println("  ap [auto|on|off]           si el punto de acceso emite (auto: se apaga con la red de casa)");
  Serial.println("  fonttest                   glifos que resuelve el binario");
  Serial.println("  parsetest                  parser contra un JSON fijo");
  Serial.println("  arttest                    redecodifica la caratula e informa");
  Serial.println("  fbpix [x] [y] [w] [h]      pixeles del framebuffer en hex");
  Serial.println("  buddyface <state> [ageS]   fija una cara para dump (thinking 25)");
  Serial.println("  wake                       repite el despertar de arranque (dump)");
  Serial.println("  buddyphase <phase> [sec]   fija el HUD/LED (ask, ok, busy, idle)");
  Serial.println("  buddyhappy [sec]           fija la cara feliz (dump de la sonrisa)");
  Serial.println("  order [ids...]             orden del menu de pantallas");
  Serial.println("  bl <0-100>                 backlight");
  Serial.println("  colors                     three bars: red, green, blue");
  Serial.println("  assets                     checksum of the web panel shipped in flash");
  Serial.println("  rgb <r> <g> <b> [level]    LED bead colour");
  Serial.println("  calendar [refresh]         Google Calendar: estado, proximo evento y reaccion");
  Serial.println("  calendar fake <min> <lead> inyecta una reaccion para dump (sin Google ni NTP)");
  Serial.println("  spotify [status]           credenciales y ultima pista");
  Serial.println("  spotify id <client_id>     guarda el client id de Spotify");
  Serial.println("  spotify url                URL de autorizacion para abrir en la PC");
  Serial.println("  spotify code <code|url>    canjea el code o la URL entera por un token");
  Serial.println("  spotify forget [all]       borra el token (o todo)");
  Serial.println("  spotify poll               fuerza una consulta");
  Serial.println("  time                       clock and sync state");
  Serial.println("  settime <epoch>            set the clock (verify the face offline)");
  Serial.println("  tz <hours>                 UTC offset, e.g. -3 for Tucuman");
  Serial.println("  api <METHOD> <path> [json] call the HTTP API in-process");
  Serial.println("  dump [step]                ASCII render of the framebuffer");
  Serial.println("  buddydraw                  geometria de la ultima cara dibujada");
  Serial.println("  buddysleep                 reloj de suenio local (stage, idle, hint)");
  Serial.println("  bug [seg]                  fuerza la distraccion del bicho");
  Serial.println("  ball [on|off]              la pelotita roja del estado idle");
  Serial.println("  reboot                     restart");
}

static void handleCommand(char *line) {
  // Trim trailing whitespace so a command typed with a stray space still works.
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
    line[--len] = '\0';
  }
  if (len == 0) return;

  // `api` needs its raw remainder as a JSON body, so it is handled before the
  // whitespace split that would otherwise cut the body apart.
  if (!strncmp(line, "api ", 4)) {
    char *rest = line + 4;
    char *sep = strchr(rest, ' ');
    if (!sep) {
      Serial.println("api <GET|POST> <path> [json]");
      return;
    }
    *sep = '\0';
    const char *method = rest;
    char *target = sep + 1;
    char *bodyStart = strchr(target, ' ');
    String body;
    if (bodyStart) {
      *bodyStart = '\0';
      body = bodyStart + 1;
    }
    ApiResult result = netHandleApi(method, target, body);
    txReliable(true);
    Serial.printf("HTTP %d %s\n", result.code, result.body.c_str());
    txReliable(false);
    if (result.restart) Serial.println("(the device would restart after this)");
    return;
  }

  char *argv[8] = {0};
  int argc = 0;
  for (char *p = strtok(line, " "); p && argc < 8; p = strtok(NULL, " ")) {
    argv[argc++] = p;
  }

  if (!strcmp(argv[0], "assets")) {
    txReliable(true);
    for (uint32_t i = 0; i < netAssetCount(); i++) {
      Serial.printf("%-14s %7lu bytes  fnv %08lx\n", netAssetPath(i),
                    (unsigned long)netAssetLength(i),
                    (unsigned long)netAssetHash(i));
    }
    txReliable(false);
    return;
  }
  if (!strcmp(argv[0], "colors")) {
    displayColorTest();
    screensGoTo(screensCurrent());
    return;
  }
  if (!strcmp(argv[0], "help")) {
    printHelp();
    return;
  }
  if (!strcmp(argv[0], "calendar")) {
    if (argc >= 2 && !strcmp(argv[1], "fake")) {
      if (argc >= 3 && !strcmp(argv[2], "off")) {
        calendarDebugFakeOff();
        Serial.println("calendar fake off");
      } else if (argc >= 4) {
        calendarDebugFakeEvent(atoi(argv[2]), atoi(argv[3]));
        Serial.printf("calendar fake: evento en %s min, aviso %s min\n", argv[2],
                      argv[3]);
      } else {
        Serial.println("uso: calendar fake <minutos> <aviso> | calendar fake off");
      }
      return;
    }
    if (argc >= 2 && !strcmp(argv[1], "refresh")) {
      bool ok = calendarRequestRefresh();
      Serial.printf("calendar refresh: %s\n", ok ? "pedido" : "falta autorizar");
      return;
    }
    CalendarStatusInfo c = calendarStatusInfo();
    const char *kind = c.kind == CALENDAR_UNCONFIGURED ? "sin configurar"
                       : c.kind == CALENDAR_NEEDS_AUTH ? "falta autorizar"
                       : c.kind == CALENDAR_AUTH_PENDING ? "esperando Google"
                                                         : "conectado";
    txReliable(true);
    Serial.printf("calendar: %s  configurado=%s autorizado=%s pendiente=%s\n",
                  kind, c.configured ? "si" : "no", c.authorized ? "si" : "no",
                  c.pending ? "si" : "no");
    Serial.printf("  calendario: %s (%s)  aviso por defecto=%d min  gracia=%d min\n",
                  c.selectedName[0] ? c.selectedName : "-",
                  c.selectedId[0] ? c.selectedId : "-", c.fallbackLeadMin,
                  c.graceMin);
    if (c.pending) {
      Serial.printf("  codigo: %s  en %s  vence en %d s\n", c.userCode,
                    c.verificationUrl, c.pendingSecondsLeft);
    }
    if (c.hasNext) {
      long mins = c.nextMsToStart / 60000;
      Serial.printf("  proximo: %s en %ld min (recordatorio %d min)\n",
                    c.nextSummary, mins,
                    c.nextLeadMin < 0 ? -1 : c.nextLeadMin);
    } else {
      Serial.println("  proximo: sin eventos a la vista");
    }
    CalendarReaction r = calendarReactionNow();
    Serial.printf("  reaccion: %s%s intensidad=%u ms=%ld\n",
                  calendarPhaseName(r.phase), r.active ? " (activa)" : "",
                  (unsigned)r.intensity, (long)r.msToStart);
    if (c.calendarCount || c.eventCount) {
      Serial.printf("  cache: %u calendarios, %u eventos\n",
                    (unsigned)c.calendarCount, (unsigned)c.eventCount);
    }
    if (c.lastError[0]) Serial.printf("  ultimo error: %s\n", c.lastError);
    txReliable(false);
    return;
  }
  if (!strcmp(argv[0], "spotify")) {
    spotifyConsole(argc, argv);
    return;
  }
  if (!strcmp(argv[0], "status")) {
    printStatus();
    return;
  }
  if (!strcmp(argv[0], "imu")) {
    printImu();
    return;
  }
  if (!strcmp(argv[0], "temp")) {
    txReliable(true);
    printTemp();
    txReliable(false);
    return;
  }
  if (!strcmp(argv[0], "ap")) {
    // The console is the way back when the router's LAN is unreachable: with the
    // AP quiet there is no page to ask for it. `auto` returns the radio to the
    // shipped rule (up while the station link is down), `on` raises it now.
    if (argc > 1) {
      if (!strcmp(argv[1], NET_AP_MODE_AUTO_NAME)) {
        netSetApMode(NET_AP_AUTO);
      } else if (!strcmp(argv[1], NET_AP_MODE_ON_NAME)) {
        netSetApMode(NET_AP_FORCE_ON);
      } else if (!strcmp(argv[1], NET_AP_MODE_OFF_NAME)) {
        netSetApMode(NET_AP_FORCE_OFF);
      } else {
        Serial.println("uso: ap [auto|on|off]");
        return;
      }
    }
    const NetStatus &st = netStatus();
    Serial.printf("ap modo %s  activo=%s  http://%s  clients=%u\n",
                  netApModeName(netApMode()), st.apActive ? "si" : "no",
                  NET_AP_IP, (unsigned)st.apClients);
    return;
  }
  if (!strcmp(argv[0], "parsetest")) {    // Runs the real parser on a fixed body, so a parse problem is separated from
    // a drawing problem with the same binary and no network.
    static const char *json =
        "{\"is_playing\":true,\"item\":{\"type\":\"track\",\"id\":\"x\","
        "\"name\":\"California Sober\",\"duration_ms\":1000,"
        "\"artists\":[{\"name\":\"Post Malone\"}],"
        "\"album\":{\"name\":\"F-1 Trillion\",\"images\":[]}}}";
    SpotifyTrack t;
    SpotifyParseResult r =
        spotifyParseCurrentlyPlaying(json, strlen(json), &t);
    txReliable(true);
    Serial.printf("parsetest r=%d title=[%s] artist=[%s]\n", (int)r, t.title,
                  t.artist);
    txReliable(false);
    return;
  }
  if (!strcmp(argv[0], "order")) {
    uint8_t order[SCREEN_COUNT];
    screensGetOrder(order);
    if (argc >= 2) {
      // `order 0 2 1 3 4` sets it; anything else just prints it.
      uint8_t want[SCREEN_COUNT];
      if (argc - 1 != SCREEN_COUNT) {
        Serial.printf("uso: order <%d ids>\n", SCREEN_COUNT);
        return;
      }
      for (int i = 0; i < SCREEN_COUNT; i++) want[i] = (uint8_t)atoi(argv[i + 1]);
      Serial.println(screensSetOrder(want) ? "order: guardado" : "order: invalido");
      screensGoTo(screensCurrent());
      return;
    }
    Serial.print("order ");
    for (int i = 0; i < SCREEN_COUNT; i++) {
      Serial.printf("%d:%s ", order[i], screenName(order[i]));
    }
    Serial.println();
    return;
  }
  if (!strcmp(argv[0], "buddyphase") && argc >= 2) {
    BuddyPhase ph = buddyPhaseFromName(argv[1]);
    uint32_t hold = argc >= 3 ? (uint32_t)atoi(argv[2]) * 1000u : 15000u;
    buddyPinPhase(ph, hold);
    Serial.printf("buddy phase pinned: %s for %lus\n", buddyPhaseWireName(ph),
                  (unsigned long)(hold / 1000));
    return;
  }
  if (!strcmp(argv[0], "buddyhappy")) {
    uint32_t hold = argc >= 2 ? (uint32_t)atoi(argv[1]) * 1000u : 10000u;
    if (hold < 500) hold = 500;
    buddyPinHappy(hold);
    Serial.printf("buddy happy pinned: %lus\n", (unsigned long)(hold / 1000));
    return;
  }
  if (!strcmp(argv[0], "buddyface") && argc >= 2) {
    // Debug pin for the face language: dump an expression without waiting for
    // the bridge to reach that age (for example `buddyface thinking 25`).
    BuddyState st = buddyFacePinFromName(argv[1]);
    uint32_t age = argc >= 3 ? (uint32_t)atoi(argv[2]) * 1000u : 0;
    buddyPinFace(st, age, 20000);
    Serial.printf("buddy face pinned: %s age %lus\n", buddyStateWireName(st),
                  (unsigned long)(age / 1000));
    return;
  }
  if (!strcmp(argv[0], "wake")) {
    // The wake is a boot-only animation, so replaying it is the only way to see
    // it again without unplugging the board: `wake` then `dump` is the probe.
    buddyWakeBegin(millis());
    Serial.println("wake: despertar de arranque en curso");
    return;
  }
  if (!strcmp(argv[0], "ball")) {
    // Same toggle the double tap drives: on by default so `ball` alone shows it.
    bool on = !(argc >= 2 && !strcmp(argv[1], "off"));
    buddySetBall(on);
    Serial.printf("ball: %s\n", on ? "on" : "off");
    return;
  }
  if (!strcmp(argv[0], "bug")) {
    uint32_t hold = argc >= 2 ? (uint32_t)atoi(argv[1]) * 1000u : 6000u;
    buddyForceBug(hold);
    Serial.printf("bug: %lus en pantalla\n", (unsigned long)(hold / 1000));
    return;
  }
  if (!strcmp(argv[0], "buddydraw")) {
    buddyDumpDraw();
    return;
  }
  if (!strcmp(argv[0], "buddysleep")) {
    // The sleep decision is local, so the only honest way to check "a shake wakes
    // it in 100 ms" without a camera is to read the clock it decides on.
    buddyDumpSleep();
    return;
  }
  if (!strcmp(argv[0], "fbpix")) {
    // Framebuffer pixels in hex, so the colours on the glass can be compared with
    // the source image instead of trusting a photograph of the panel.
    int x = argc > 1 ? atoi(argv[1]) : 10;
    int y = argc > 2 ? atoi(argv[2]) : 20;
    int w = argc > 3 ? atoi(argv[3]) : 8;
    int h = argc > 4 ? atoi(argv[4]) : 3;
    const uint16_t *fb = gfxBuffer();
    txReliable(true);
    for (int yy = 0; yy < h; yy++) {
      Serial.printf("fb y%03d ", y + yy);
      for (int xx = 0; xx < w; xx++) {
        Serial.printf("%04x ", fb[(size_t)(y + yy) * DISPLAY_W + (x + xx)]);
      }
      Serial.println();
    }
    txReliable(false);
    return;
  }
  if (!strcmp(argv[0], "arttest")) {
    // Runs in the poll task on purpose: the TLS client is shared and one task at
    // a time must own it. This only arms the diagnostics and asks for a poll.
    spotifyArtSetDebug(true);
    spotifyArtInvalidate();
    Serial.println("art: pedido, la proxima consulta informa");
    spotifySetActive(true);
    spotifyRequestRefresh();
    return;
  }
  if (!strcmp(argv[0], "fonttest")) {
    // Prints the glyph the renderer actually resolves, so a font/lookup bug can
    // be told apart from corrupted data without a network or a working track.
    txReliable(true);
    Serial.printf("font first=%d last=%d rows=%d\n", gfxFontFirst(),
                  gfxFontLast(), gfxFontRows());
    for (const char *p = "aeB7"; *p; p++) {
      Serial.printf("font '%c' code=%d row=%d bytes=%02x %02x %02x %02x %02x\n",
                    *p, (int)*p, gfxGlyphIndex(*p), gfxGlyphBytes(*p)[0],
                    gfxGlyphBytes(*p)[1], gfxGlyphBytes(*p)[2],
                    gfxGlyphBytes(*p)[3], gfxGlyphBytes(*p)[4]);
    }
    int from = argc > 1 ? atoi(argv[1]) : 62;
    int to = argc > 2 ? atoi(argv[2]) : from + 6;
    gfxFontDumpRows(from, to);
    txReliable(false);
    return;
  }
  if (!strcmp(argv[0], "dump")) {
    dumpFramebuffer(argc > 1 ? atoi(argv[1]) : 4);
    return;
  }
  if (!strcmp(argv[0], "reboot")) {
    Serial.println("rebooting");
    delay(50);
    ESP.restart();
  }
  if (!strcmp(argv[0], "screen") && argc > 1) {
    int index = atoi(argv[1]);
    if (index < 0 || index >= SCREEN_COUNT) {
      Serial.printf("screen: 0..%d\n", SCREEN_COUNT - 1);
      return;
    }
    screensGoTo((uint8_t)index);
    Serial.printf("screen -> %s\n", screenName((uint8_t)index));
    return;
  }
  if (!strcmp(argv[0], "gameage") && argc > 1) {
    gameSetAgeSeconds((uint32_t)atoi(argv[1]));
    Serial.printf("game age -> %ds\n", atoi(argv[1]));
    return;
  }
  if (!strcmp(argv[0], "spawnenemy")) {
    Serial.println(gameSpawnEnemyForTest() ? "enemy spawned" : "no spawn");
    return;
  }
  if (!strcmp(argv[0], "closemenu")) {
    // Test hook: the picker is modal and `screen` does not dismiss it.
    screensMenuClose();
    Serial.println("menu closed");
    return;
  }
  if (!strcmp(argv[0], "setship") && argc > 2) {
    gameSetShip((float)atof(argv[1]), (float)atof(argv[2]));
    Serial.printf("ship -> %.2f,%.2f\n", gameShipColumn(), gameShipRow());
    return;
  }
  if (!strcmp(argv[0], "doubletap")) {
    // The probe cannot send two taps 120 ms apart, so this calls the same short
    // press path twice. Used to drive a screen's double-tap gesture end to end.
    screensShortPress();
    delay(120);
    screensShortPress();
    Serial.println("double tap");
    return;
  }
  if (!strcmp(argv[0], "tap")) {
    // Direct short-press call, with no button timing: the FSM turns a synthetic
    // press into a hold when the main loop stalls for a second (a network task),
    // which made `press` unreliable for checking the tap gestures.
    screensShortPress();
    Serial.println("tap");
    return;
  }
  if (!strcmp(argv[0], "longpress")) {
    screensLongPress();
    Serial.println("longpress");
    return;
  }
  if (!strcmp(argv[0], "press")) {
    uint32_t ms = argc > 1 ? (uint32_t)atoi(argv[1]) : 120;
    if (ms < 40) ms = 40;
    if (ms > 5000) ms = 5000;
    s_synthDownUntil = millis() + ms;
    Serial.printf("press %lums\n", (unsigned long)ms);
    return;
  }
  if (!strcmp(argv[0], "hold")) {
    s_synthDownUntil = millis() + BTN_LONG_PRESS_MS + 250;
    Serial.println("hold 1s");
    return;
  }
  if (!strcmp(argv[0], "orient") && argc > 1) {
    int value = atoi(argv[1]);
    if (value < 0 || value > 3) {
      Serial.println("orient: 0..3");
      return;
    }
    // Pinning an image is now a mode: the automatic policy must not undo it on
    // the next sample. Everything else about this command is unchanged.
    rotationSetMode((uint8_t)value);
    screensGoTo(screensCurrent());  // force a fresh frame in the new rotation
    Serial.printf("orient -> %d (pinned)\n", value);
    return;
  }
  if (!strcmp(argv[0], "rot")) {
    if (argc > 1 && !strcmp(argv[1], "lock")) {
      bool on = !(argc > 2 && !strcmp(argv[2], "off"));
      rotationSetLocked(on);
      Serial.printf("rot lock %s\n", on ? "on" : "off");
      return;
    }
    if (argc > 1) {
      if (!strcmp(argv[1], "auto")) {
        rotationSetMode(ROT_MODE_AUTO);
        Serial.println("rot auto");
        return;
      }
      int value = atoi(argv[1]);
      if (value < 0 || value > 3) {
        Serial.println("rot: auto | 0..3 | lock on|off");
        return;
      }
      rotationSetMode((uint8_t)value);
      Serial.printf("rot -> %d (pinned)\n", value);
      return;
    }
    Serial.printf("rot mode=%s (%u) lock=%s image=%u sign=%d\n",
                  rotationModeName(), (unsigned)rotationMode(),
                  rotationLocked() ? "on" : "off",
                  (unsigned)rotationApplied(), (int)rotationInPlaneSign());
    return;
  }
  if (!strcmp(argv[0], "steer")) {
    uint8_t lat, up;
    int8_t sign, signY;
    uint8_t gain;
    gameGetSteer(&lat, &up, &sign, &signY, &gain);
    if (argc > 1) lat = (uint8_t)atoi(argv[1]);
    if (argc > 2) up = (uint8_t)atoi(argv[2]);
    if (argc > 3) sign = (int8_t)atoi(argv[3]);
    if (argc > 4) signY = (int8_t)atoi(argv[4]);
    if (argc > 5) gain = (uint8_t)atoi(argv[5]);
    gameSetSteer(lat, up, sign, signY, gain);
    Serial.printf("steer lat=%u up=%u sign=%d signY=%d gain=%u\n",
                  (unsigned)lat, (unsigned)up, (int)sign, (int)signY,
                  (unsigned)gain);
    return;
  }
  if (!strcmp(argv[0], "steercal")) {
    gameInvalidateCalibration();
    screensGoTo(SCREEN_SETTINGS);
    configStartCalibration();
    Serial.println("calibracion borrada: wizard abierto en CONFIG");
    return;
  }
  if (!strcmp(argv[0], "imufake") && argc >= 7) {
    float a[3] = {(float)atof(argv[1]), (float)atof(argv[2]),
                  (float)atof(argv[3])};
    float g[3] = {(float)atof(argv[4]), (float)atof(argv[5]),
                  (float)atof(argv[6])};
    imuSetFake(a, g, true);
    Serial.printf("imu fake accel %.2f %.2f %.2f gyro %.1f %.1f %.1f\n", a[0],
                  a[1], a[2], g[0], g[1], g[2]);
    return;
  }
  if (!strcmp(argv[0], "imureal")) {
    imuSetFake(nullptr, nullptr, false);
    Serial.println("imu real");
    return;
  }
  if (!strcmp(argv[0], "shippos") || !strcmp(argv[0], "gamestate")) {
    uint32_t score, level;
    int meteors = 0, bullets = 0, enemies = 0, enemyShots = 0;
    gameGetStats(&score, &level, &meteors, &bullets, &enemies, &enemyShots);
    Serial.printf(
        "state %s  ship %.2f,%.2f  score %lu  level %lu  meteors %d  shots %d  "
        "enemies %d  enemyShots %d\n",
        gameStateName(), gameShipColumn(), gameShipRow(),
        (unsigned long)score, (unsigned long)level, meteors, bullets, enemies,
        enemyShots);
    for (int i = 0; i < enemies; i++) {
      Serial.printf("  enemy %d at x=%.2f vx=%.2f hp=%d/%d\n", i,
                    gameEnemyColumn(i), gameEnemyDrift(i), gameEnemyHp(i),
                    gameEnemyMaxHp(i));
    }
    return;
  }
  if (!strcmp(argv[0], "bl") && argc > 1) {
    int value = atoi(argv[1]);
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    // Persisted like the panel's slider, so `bl` and CONFIG cannot disagree
    // about what the device boots with.
    settingsSetLcdBrightness((uint8_t)value);
    displaySetBacklight(settingsGet().brightness);
    screensGoTo(screensCurrent());
    Serial.printf("backlight %u\n", (unsigned)settingsGet().brightness);
    return;
  }
  if (!strcmp(argv[0], "rgb") && argc > 3) {
    uint8_t r = (uint8_t)atoi(argv[1]);
    uint8_t g = (uint8_t)atoi(argv[2]);
    uint8_t b = (uint8_t)atoi(argv[3]);
    rgbSetColor(r, g, b);
    rgbSetEffect(RGB_EFFECT_SOLID);
    if (argc > 4) {
      rgbSetLevel((uint8_t)atoi(argv[4]));
      // The level is stored now: a console tweak that vanished on reboot would
      // contradict the CONFIG screen that shows it.
      settingsSetRgbLevel(rgbLevel());
    }
    // Applied now, and the bead wakes: a console command that only took effect on
    // the next tick looked like a light that ignored the number.
    rgbApplyNow();
    Serial.printf("rgb %u %u %u level %u\n", (unsigned)r, (unsigned)g,
                  (unsigned)b, (unsigned)rgbLevel());
    return;
  }
  if (!strcmp(argv[0], "tz") && argc > 1) {
    int hours = atoi(argv[1]);
    if (hours < -12 || hours > 14) {
      Serial.println("tz: -12..14");
      return;
    }
    settingsMutable().tzOffsetHours = (int8_t)hours;
    settingsSave();
    netApplyTimeZone();
    screensGoTo(screensCurrent());
    Serial.printf("tz %+d\n", hours);
    return;
  }
  if (!strcmp(argv[0], "settime") && argc > 1) {
    // Setting the clock directly is how the seven-segment face can be checked
    // on a desk with no network: NTP never runs, the face still has to render
    // real digits in the configured offset.
    struct timeval tv;
    tv.tv_sec = (time_t)strtoul(argv[1], nullptr, 10);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    char stamp[32];
    formatLocalTime(stamp, sizeof(stamp));
    screensGoTo(screensCurrent());
    Serial.printf("clock set -> %s\n", stamp);
    return;
  }
  if (!strcmp(argv[0], "time")) {
    char stamp[32];
    formatLocalTime(stamp, sizeof(stamp));
    txReliable(true);
    Serial.printf("local %s  tz=%+d\n", stamp,
                  (int)settingsGet().tzOffsetHours);
    txReliable(false);
    return;
  }
  Serial.printf("unknown: %s (try help)\n", argv[0]);
}

void consolePoll() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      s_line[s_lineLen] = '\0';
      handleCommand(s_line);
      s_lineLen = 0;
    } else if (c != '\r' && s_lineLen < sizeof(s_line) - 1) {
      s_line[s_lineLen++] = c;
    }
  }
}

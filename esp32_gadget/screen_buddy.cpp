#include "screen_buddy.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <math.h>
#include <time.h>

#include "buddy_anim.h"
#include "buddy_imu.h"
#include "gfx.h"
#include "imu.h"
#include "rgb.h"
#include "settings.h"
#include "text_fold.h"

// --- snapshot shared with the poller task -----------------------------------

#define BUDDY_POLL_MS 1000
#define BUDDY_HTTP_TIMEOUT_MS 2000

struct BuddySnapshot {
  BuddyState state = BUDDY_UNCONFIGURED;
  char label[BUDDY_LABEL_MAX + 1] = {0};
  int active = 0;
  BuddyPhase phase = BUDDY_PHASE_IDLE;
  BuddyRgb led = {0, 180, 255};
  bool ledValid = false;
  bool online = false;
  bool configured = false;
  uint32_t fetchedAt = 0;
  uint32_t stateSinceMs = 0;  // millis of the first observation of this state
  uint32_t sinceAgeMs = 0;    // state age reported by the bridge at fetchedAt
  uint32_t revision = 0;
};

static BuddySnapshot s_snap;
static SemaphoreHandle_t s_lock = nullptr;
static TaskHandle_t s_task = nullptr;

// Drawing state lives in the loop; only the snapshot crosses tasks.
static uint32_t s_drawnRevision = 0;
static BuddyState s_drawnState = BUDDY_UNCONFIGURED;
static uint32_t s_shownAt = 0;       // millis when the shown state was adopted
static uint32_t s_shownBaseAge = 0;  // its age at that moment
static uint32_t s_seed = 0x5eed1234;
static uint32_t s_lastDraw = 0;
static uint32_t s_pokeUntil = 0;
static uint32_t s_surpriseUntil = 0;
static BuddyImu s_imu;
static uint32_t s_imuAt = 0;
static bool s_pin = false;
static BuddyState s_pinState = BUDDY_IDLE;
static uint32_t s_pinAge = 0;
static uint32_t s_pinStart = 0;
static uint32_t s_pinHold = 0;
static bool s_phasePinned = false;
static BuddyPhase s_phasePinValue = BUDDY_PHASE_IDLE;
static uint32_t s_phasePinUntil = 0;
static bool s_dirty = true;

static void publish(BuddyState state, const char *label, int active,
                    bool online, bool configured, uint32_t stateAgeMs = 0,
                    BuddyPhase phase = BUDDY_PHASE_IDLE,
                    BuddyRgb led = {0, 180, 255}, bool ledValid = false) {
  if (!s_lock) return;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
  bool changed = s_snap.state != state || s_snap.active != active ||
                 s_snap.online != online ||
                 s_snap.configured != configured ||
                 s_snap.phase != phase || s_snap.ledValid != ledValid ||
                 s_snap.led.r != led.r || s_snap.led.g != led.g ||
                 s_snap.led.b != led.b ||
                 strcmp(s_snap.label, label ? label : "") != 0;
  if (s_snap.state != state) {
    s_snap.stateSinceMs = millis();
    s_snap.sinceAgeMs = stateAgeMs;
  } else if (stateAgeMs) {
    s_snap.sinceAgeMs = stateAgeMs;
  }
  s_snap.state = state;
  snprintf(s_snap.label, sizeof(s_snap.label), "%s", label ? label : "");
  s_snap.active = active < 0 ? 0 : active;
  s_snap.phase = phase;
  s_snap.led = led;
  s_snap.ledValid = ledValid;
  s_snap.online = online;
  s_snap.configured = configured;
  s_snap.fetchedAt = millis();
  if (changed) s_snap.revision++;
  xSemaphoreGive(s_lock);
}

// GET /state with the bridge token. Plain HTTP on the LAN on purpose: the
// bridge is LAN-only in this phase and the device never holds the Hermes key.
static void pollOnce() {
  char url[SETTINGS_BUDDY_URL_MAX + 1];
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  settingsGetBuddyUrl(url, sizeof(url));
  settingsGetBuddyToken(token, sizeof(token));
  if (!url[0] || !token[0]) {
    publish(BUDDY_UNCONFIGURED, "", 0, false, false);
    return;
  }

  HTTPClient http;
  http.setTimeout(BUDDY_HTTP_TIMEOUT_MS);
  String endpoint = String(url) + "/state";
  if (!http.begin(endpoint)) {
    publish(BUDDY_OFFLINE, "", 0, false, true);
    return;
  }
  http.addHeader("X-Buddy-Token", token);
  int code = http.GET();
  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err) {
      const char *name = doc["state"] | "";
      const char *label = doc["label"] | "";
      int active = doc["active"] | 0;
      // `since` is an epoch float. The device clock may not be synced yet, so
      // only trust the difference when both sides look like real time; the
      // local observation time is the fallback.
      float since = doc["since"] | 0.0f;
      uint32_t age = 0;
      time_t now = time(nullptr);
      if (since > 1600000000.0f && now > 1600000000) {
        double d = (double)now - (double)since;
        if (d >= 0.0 && d < 86400.0) age = (uint32_t)(d * 1000.0);
      }
      // `elapsed` is the state age already computed by the bridge; prefer it,
      // because the device clock may be off (seen: +37 s).
      float elapsed = doc["elapsed"] | -1.0f;
      if (elapsed >= 0.0f && elapsed < 86400.0f) {
        age = (uint32_t)(elapsed * 1000.0f);
      }

      // The HUD/LED channel: phase is authoritative and never derived from
      // state. The bridge resolves the LED colour; if the field is missing, use
      // the published table so the bead still means something.
      BuddyPhase phase = buddyPhaseFromName(doc["phase"] | "");
      BuddyRgb led = buddyPhaseDefaultLed(phase);
      bool ledValid = false;
      JsonArray ledArr = doc["led"].as<JsonArray>();
      if (!ledArr.isNull() && ledArr.size() == 3) {
        int r = ledArr[0] | 0;
        int g = ledArr[1] | 0;
        int b = ledArr[2] | 0;
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        if (g < 0) g = 0;
        if (g > 255) g = 255;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        led.r = (uint8_t)r;
        led.g = (uint8_t)g;
        led.b = (uint8_t)b;
        ledValid = true;
      }
      char clean[BUDDY_LABEL_MAX + 1];
      buddyLabelClean(label, clean, sizeof(clean));
      // The glass has a 5x7 ASCII font: fold accents here, the panel keeps the
      // folded label too (it is the same text the owner reads on the device).
      char folded[BUDDY_LABEL_MAX + 1];
      textFoldAscii(clean, folded, sizeof(folded));
      publish(buddyStateFromName(name), folded, active, true, true, age,
              phase, led, ledValid);
    } else {
      publish(BUDDY_OFFLINE, "", 0, false, true);
    }
  } else {
    publish(BUDDY_OFFLINE, "", 0, false, true);
  }
  http.end();
}

static void pollTask(void *arg) {
  (void)arg;
  for (;;) {
    pollOnce();
    vTaskDelay(pdMS_TO_TICKS(BUDDY_POLL_MS));
  }
}

void buddyBegin() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  s_seed = esp_random();
  publish(BUDDY_UNCONFIGURED, "", 0, false, false);
  if (!s_task) {
    // Same pattern as the Spotify poller: its own small task, so the eyes never
    // stall the button or the RGB while the HTTP request is in flight.
    xTaskCreatePinnedToCore(pollTask, "buddy", 8192, nullptr, 1, &s_task, 1);
  }
}

void buddyEnter() {
  s_dirty = true;
  s_lastDraw = 0;
}

void buddyPinPhase(BuddyPhase phase, uint32_t holdMs) {
  s_phasePinned = true;
  s_phasePinValue = phase;
  s_phasePinUntil = millis() + holdMs;
}

void buddyPinFace(BuddyState state, uint32_t ageMs, uint32_t holdMs) {
  s_pin = true;
  s_pinState = state;
  s_pinAge = ageMs;
  s_pinStart = millis();
  s_pinHold = holdMs;
  s_dirty = true;
}

void buddyShortPress() {
  // A tap is a boop: happy eyes for a moment even if Sofía is busy.
  s_pokeUntil = millis() + 1100;
  s_dirty = true;
  rgbPulse(255, 180, 220, 500);
}

void buddyGetView(BuddyView *out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->state = BUDDY_UNCONFIGURED;
  if (!s_lock) return;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
  out->state = s_snap.state;
  out->configured = s_snap.configured;
  out->online = s_snap.online;
  out->active = s_snap.active;
  out->phase = s_snap.phase;
  out->led = s_snap.led;
  out->ledValid = s_snap.ledValid;
  out->revision = s_snap.revision;
  snprintf(out->label, sizeof(out->label), "%s", s_snap.label);
  if (s_snap.fetchedAt) out->ageMs = millis() - s_snap.fetchedAt;
  // The bridge's `since` is preferred, but the device clock can be off by
  // seconds (observed: +37 s) and then the absolute age lies. The local time
  // since the first observation is the tie-breaker: when the two disagree by
  // more than ten seconds, the clock is the suspect, not the poll.
  uint32_t localAge = millis() - s_snap.stateSinceMs;
  if (s_phasePinned) {
    if ((int32_t)(s_phasePinUntil - millis()) > 0) {
      out->phase = s_phasePinValue;
      out->led = buddyPhaseDefaultLed(s_phasePinValue);
      out->ledValid = true;
    } else {
      s_phasePinned = false;
    }
  }
  if (s_snap.sinceAgeMs) {
    uint32_t epochAge = s_snap.sinceAgeMs + (millis() - s_snap.fetchedAt);
    uint32_t diff = epochAge > localAge ? epochAge - localAge : localAge - epochAge;
    out->stateAgeMs = diff < 10000 ? epochAge : localAge;
  } else {
    out->stateAgeMs = localAge;
  }
  xSemaphoreGive(s_lock);
}

// --- eyes only ---------------------------------------------------------------
//
// The whole canvas is two big circular cyan eyes, no text and no chrome. The
// mood is carried by the lids, the gaze drift, the happy arcs, the X in the eyes and
// the RGB bead. Reference: RoboEyes-style animated eyes (FluxGarage), reduced to
// what a 320x172 panel does well.

// Bluer cyan: the owner saw the first cyan as green on the panel, so the
// green channel gives way to blue while staying cyan.
static const uint16_t FACE = gfxHex(0x00B4FF);
static const uint16_t FACE_DIM = gfxHex(0x06455C);

static void thickLine(int x0, int y0, int x1, int y1, uint16_t color) {
  gfxLine(x0, y0, x1, y1, color);
  gfxLine(x0, y0 + 1, x1, y1 + 1, color);
}

// A happy closed eye: a wide caret. No fill, so it reads as a smile line.
static void happyEye(int cx, int cy, uint16_t color) {
  thickLine(cx - 30, cy + 10, cx, cy - 16, color);
  thickLine(cx, cy - 16, cx + 30, cy + 10, color);
}

// Filled ellipse rotated by `deg` (screen pixels). For each row the implicit
// ellipse equation is a quadratic in dx, so the row is one span. At 46x50 the
// whole eye is a few thousand pixels; this is cheap enough for 10 fps.
static void fillEllipseRot(int cx, int cy, int rx, int ry, float deg,
                           uint16_t color) {
  if (fabsf(deg) < 0.5f) {
    gfxFillEllipse(cx, cy, rx, ry, color);
    return;
  }
  float a = deg * 0.0174532925f;
  float ca = cosf(a), sa = sinf(a);
  float irx = 1.0f / (float)rx, iry = 1.0f / (float)ry;
  float A = ca * ca * irx * irx + sa * sa * iry * iry;
  float Bc = 2.0f * ca * sa * (irx * irx - iry * iry);
  float Cc = sa * sa * irx * irx + ca * ca * iry * iry;
  int ey = (int)(fabsf((float)rx * sa) + fabsf((float)ry * ca)) + 1;
  for (int dy = -ey; dy <= ey; dy++) {
    int y = cy + dy;
    if (y < 0 || y >= 172) continue;
    float B = Bc * (float)dy;
    float C = Cc * (float)dy * (float)dy - 1.0f;
    float disc = B * B - 4.0f * A * C;
    if (disc < 0) continue;
    float root = sqrtf(disc);
    int x0 = (int)ceilf((float)cx + (-B - root) / (2.0f * A));
    int x1 = (int)floorf((float)cx + (-B + root) / (2.0f * A));
    if (x1 < 0 || x0 >= 320 || x1 < x0) continue;
    if (x0 < 0) x0 = 0;
    if (x1 > 319) x1 = 319;
    gfxHLine(x0, y, x1 - x0 + 1, color);
  }
}

// --- HUD: the functional channel --------------------------------------------
//
// A 20 px badge in the top-right corner. The face may be ambiguous (focused vs
// bored); the HUD never lies. `phase` is authoritative; `busy` is motion, `ok`
// is a static check after a 250 ms pop, `ask` pulses, and idle leaves the glass
// clean. The synthesized check (busy -> idle) covers Desktop/Telegram, where the
// bridge never sends `ok`.

#define HUD_CX 302
#define HUD_CY 18
#define SYNTH_OK_MS 3000

static BuddyPhase s_hudPhase = BUDDY_PHASE_IDLE;
static bool s_hudHave = false;
static uint32_t s_hudCheckAt = 0;
static uint32_t s_synthOkUntil = 0;

static uint16_t hudRgb(BuddyRgb c, float scale) {
  int r = (int)((float)c.r * scale);
  int g = (int)((float)c.g * scale);
  int b = (int)((float)c.b * scale);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return gfxHex(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

static void hudTrack(BuddyPhase phase, uint32_t nowMs) {
  if (!s_hudHave) {
    s_hudHave = true;
    s_hudPhase = phase;
    if (phase == BUDDY_PHASE_OK) s_hudCheckAt = nowMs;
    return;
  }
  if (phase == s_hudPhase) return;
  if (phase == BUDDY_PHASE_OK) {
    s_hudCheckAt = nowMs;
    s_synthOkUntil = 0;
  } else if (buddyPhaseFinished(s_hudPhase, phase) && phase == BUDDY_PHASE_IDLE) {
    // Desktop/Telegram never send `ok`: the drop from busy to idle is the cue.
    s_hudCheckAt = nowMs;
    s_synthOkUntil = nowMs + SYNTH_OK_MS;
  } else if (phase != BUDDY_PHASE_IDLE) {
    s_synthOkUntil = 0;
  }
  s_hudPhase = phase;
}

static void hudBusy(BuddyRgb color, uint32_t nowMs) {
  for (int i = 0; i < 3; i++) {
    float phase = (float)((nowMs + (uint32_t)i * 180u) % 1200u) / 1200.0f *
                  6.2831853f;
    float opacity = 0.35f + 0.65f * (0.5f + 0.5f * cosf(phase));
    gfxFillCircle(HUD_CX - 7 + i * 7, HUD_CY, 2, hudRgb(color, opacity));
  }
}

static void hudCheck(uint32_t nowMs) {
  uint32_t t = nowMs - s_hudCheckAt;
  float scale = 1.0f;
  if (t < 125) {
    scale = 0.8f + 0.3f * ((float)t / 125.0f);
  } else if (t < 250) {
    scale = 1.1f - 0.1f * ((float)(t - 125) / 125.0f);
  }
  int x1 = HUD_CX + (int)(-7.0f * scale);
  int y1 = HUD_CY + (int)(1.0f * scale);
  int x2 = HUD_CX + (int)(-2.0f * scale);
  int y2 = HUD_CY + (int)(6.0f * scale);
  int x3 = HUD_CX + (int)(8.0f * scale);
  int y3 = HUD_CY + (int)(-7.0f * scale);
  thickLine(x1, y1, x2, y2, gfxHex(0x07E0));
  thickLine(x2, y2, x3, y3, gfxHex(0x07E0));
}

static void hudAsk(uint32_t nowMs) {
  float pulse = 0.5f + 0.5f * sinf((float)nowMs * 0.010471976f - 1.5707963f);
  float scale = 1.0f + 0.15f * pulse;
  uint16_t red = gfxHex(0xF800);
  int top = HUD_CY - (int)(9.0f * scale);
  int bottom = HUD_CY + (int)(4.0f * scale);
  gfxFillRect(HUD_CX - 1, top, 3, bottom - top, red);
  gfxFillCircle(HUD_CX, HUD_CY + (int)(9.0f * scale), 2, red);
}

static void drawHud(BuddyPhase phase, BuddyRgb led, uint32_t nowMs) {
  if (phase == BUDDY_PHASE_BUSY) {
    hudBusy(led, nowMs);
    return;
  }
  if (phase == BUDDY_PHASE_ASK) {
    hudAsk(nowMs);
    return;
  }
  if (phase == BUDDY_PHASE_OK) {
    hudCheck(nowMs);
    return;
  }
  if ((int32_t)(s_synthOkUntil - nowMs) > 0) hudCheck(nowMs);
}

static void drawFace(BuddyState state, const BuddyPose &pose, uint32_t nowMs,
                     bool poke) {
  gfxClear(GFX_BLACK);

  const int cx[2] = {105, 215};
  const int cy = 88;
  int bounce =
      (state == BUDDY_WAITING) ? (int)(sinf(nowMs * 0.004f) * 3.0f) : 0;
  int bob = pose.closed ? (int)(sinf(nowMs * 0.0015f) * 2.0f) : 0;
  uint16_t color = pose.dim ? FACE_DIM : FACE;
  bool happy = pose.happy || poke;

  for (int i = 0; i < 2; i++) {
    // Solid eyes, no pupils: life comes from the gaze and the lids. The error
    // face adds a per-eye offset so the eyes point at each other.
    int x = cx[i] + (int)(pose.gazeX + (i == 0 ? pose.eyeGazeL : pose.eyeGazeR));
    int y = cy + bounce + bob + (int)pose.gazeY;
    if (happy) {
      happyEye(x, y, color);
      continue;
    }
    if (pose.blink || pose.closed) {
      gfxFillRect(x - pose.rx, y - 3, pose.rx * 2, 6, color);
      continue;
    }
    fillEllipseRot(x, y, pose.rx, pose.ry, pose.rot, color);
    // A black band over the top of the eye: the eyelid. Black on black is free,
    // so the eye is simply clipped. The two eyes can have different lids (the
    // EMO thinking wink) and a bottom lid can cut the tired look.
    float top = pose.lid + (i == 0 ? pose.lidL : pose.lidR);
    if (top > 0.02f) {
      int h = (int)(pose.ry * 2 * top) + 2;
      gfxFillRect(x - pose.rx - 4, y - pose.ry - 2, pose.rx * 2 + 8, h,
                  GFX_BLACK);
    }
    if (pose.bottom > 0.02f) {
      // Cover the rotated ellipse's bottom edge too, or a 2 px sliver survives.
      int h = (int)(pose.ry * 2 * pose.bottom) + 4;
      int y0 = y + pose.ry - (int)(pose.ry * 2 * pose.bottom);
      gfxFillRect(x - pose.rx - 4, y0, pose.rx * 2 + 8, h, GFX_BLACK);
    }
    if (state == BUDDY_ERROR) {
      // Two crossed black strokes inside the cyan eye: the classic x x.
      thickLine(x - 20, y - 22, x + 20, y + 22, GFX_BLACK);
      thickLine(x - 20, y + 22, x + 20, y - 22, GFX_BLACK);
    }
  }
}

bool buddyTick(uint32_t nowMs) {
  if (s_pin) {
    if ((int32_t)(s_pinStart + s_pinHold - nowMs) <= 0) {
      s_pin = false;
    } else {
      if ((uint32_t)(nowMs - s_lastDraw) < 100) return false;
      s_drawnState = s_pinState;
      BuddyPose pose =
          buddyPose(s_pinState, s_pinAge + (nowMs - s_pinStart), nowMs, s_seed);
      drawFace(s_pinState, pose, nowMs, false);
      s_lastDraw = nowMs;
      s_dirty = false;
      return true;
    }
  }

  BuddyView view;
  buddyGetView(&view);

  bool poke = (int32_t)(s_pokeUntil - nowMs) > 0;
  bool news = s_dirty || view.revision != s_drawnRevision;

  // Hold back a state that just appeared: if it disappears on the next poll,
  // the glass never flashed it. The first configured state paints at once.
  BuddyState shown = s_drawnState;
  if (s_drawnState == BUDDY_UNCONFIGURED) {
    shown = view.state;
  } else {
    shown = buddyShownState(s_drawnState, view.state, view.stateAgeMs);
  }
  if (shown != s_drawnState) {
    s_drawnState = shown;
    s_shownAt = nowMs;
    s_shownBaseAge = (shown == view.state) ? view.stateAgeMs : 0;
    // The anti-flicker hold delays the tool state by up to 300 ms, which would
    // hide the 200 ms surprise window; fire it from the adoption instead.
    if (shown == BUDDY_TOOL) s_surpriseUntil = nowMs + 200;
    news = true;
  }

  bool animate = (uint32_t)(nowMs - s_lastDraw) >= 100;
  if (!news && !animate) return false;

  uint32_t shownAge = s_shownBaseAge + (uint32_t)(nowMs - s_shownAt);
  BuddyPose pose = buddyPose(shown, shownAge, nowMs, s_seed);
  if (shown == BUDDY_TOOL && (int32_t)(s_surpriseUntil - nowMs) > 0) {
    pose.surprise = true;
    pose.rx = 54;
    pose.ry = 58;
    pose.lid = 0.0f;
  }

  // Physical layer: the IMU is local and the bridge never sees it. A missing
  // sensor reads zero, which the feed ignores, and every reaction expires, so
  // the face can always come back to the state pose.
  float accel[3], gyro[3];
  imuReadAll(accel, gyro);
  uint32_t dt = s_imuAt ? (nowMs - s_imuAt) : 100;
  s_imuAt = nowMs;
  buddyImuFeed(&s_imu, accel, gyro, nowMs, dt);
  BuddyImuPose ip = buddyImuPose(&s_imu, nowMs);
  pose.gazeX += ip.gazeX;
  pose.gazeY += ip.gazeY;
  if (ip.lid > pose.lid) pose.lid = ip.lid;
  if (ip.squint && pose.lid < 0.45f) pose.lid = 0.45f;
  if (ip.wide) {
    pose.surprise = true;
    pose.rx = 54;
    pose.ry = 58;
  }
  if (ip.faceDown) pose.lid = 0.6f;
  // Lifted off the table while bored asleep: wake with a start, then let the
  // state take over again.
  if (ip.wake && shown == BUDDY_BORED) {
    pose.closed = false;
    pose.lid = 0.0f;
    pose.surprise = true;
    pose.rx = 54;
    pose.ry = 58;
  }

  drawFace(shown, pose, nowMs, poke);
  hudTrack(view.phase, nowMs);
  drawHud(view.phase,
          view.ledValid ? view.led : buddyPhaseDefaultLed(view.phase), nowMs);
  s_drawnRevision = view.revision;
  s_lastDraw = nowMs;
  s_dirty = false;
  return true;
}

bool buddyAsk(const char *prompt, char *err, size_t errCap) {
  if (err && errCap) err[0] = '\0';
  if (!prompt || !*prompt) {
    if (err && errCap) snprintf(err, errCap, "Escribi un pedido");
    return false;
  }
  char url[SETTINGS_BUDDY_URL_MAX + 1];
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  settingsGetBuddyUrl(url, sizeof(url));
  settingsGetBuddyToken(token, sizeof(token));
  if (!url[0] || !token[0]) {
    if (err && errCap) snprintf(err, errCap, "Falta la URL o el token");
    return false;
  }

  HTTPClient http;
  http.setTimeout(4000);
  String endpoint = String(url) + "/buddy/ask";
  if (!http.begin(endpoint)) {
    if (err && errCap) snprintf(err, errCap, "No pude conectar al bridge");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Buddy-Token", token);
  JsonDocument doc;
  doc["prompt"] = prompt;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  if (code < 200 || code >= 300) {
    if (err && errCap) snprintf(err, errCap, "El bridge respondio %d", code);
    return false;
  }
  // The bridge answers 202 and runs asynchronously. Paint thinking locally so
  // the eyes react at once; the next poll corrects with the real state.
  publish(BUDDY_THINKING, "", 0, true, true, 0, BUDDY_PHASE_BUSY,
          buddyPhaseDefaultLed(BUDDY_PHASE_BUSY), true);
  return true;
}

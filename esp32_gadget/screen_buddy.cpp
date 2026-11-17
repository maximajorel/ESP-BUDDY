#include "screen_buddy.h"

#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

#include "buddy_anim.h"
#include "buddy_face.h"
#include "buddy_http.h"
#include "buddy_imu.h"
#include "buddy_link.h"
#include "buddy_url.h"
#include "gfx.h"
#include "imu.h"
#include "net.h"
#include "rgb.h"
#include "rotation.h"
#include "settings.h"
#include "text_fold.h"

// --- snapshot shared with the poller task -----------------------------------

#define BUDDY_POLL_MS 1000
// The same budget the Spotify poller gives its TLS work (spotify.cpp): a
// handshake over https spends stack in mbedtls, and 8 KB was sized for plain
// HTTP on the LAN.
#define BUDDY_TASK_STACK 12288
// 20 fps: the eased pose covers a channel in a few frames, so the motion reads
// as continuous instead of a slideshow. The SPI push fits comfortably at this
// rate; the poller and the button stay in their own contexts.
#define BUDDY_FRAME_MS 50

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
  // Why the last attempt ended the way it did. The glass has one "no signal"
  // face, but the panel can say which of the several causes it was.
  BuddyLink link = BUDDY_LINK_UNCONFIGURED;
  char linkDetail[96] = {0};
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
// The eased face. The ladders produce a target every frame; this filter is what
// actually moves the eyes, so no field ever jumps between two frames.
static BuddyPoseFilter s_filter;
static uint32_t s_filterAt = 0;

static void publish(BuddyState state, const char *label, int active,
                    bool online, bool configured, uint32_t stateAgeMs = 0,
                    BuddyPhase phase = BUDDY_PHASE_IDLE,
                    BuddyRgb led = {0, 180, 255}, bool ledValid = false,
                    BuddyLink link = BUDDY_LINK_OK, const char *detail = nullptr) {
  if (!s_lock) return;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
  const char *safeDetail = detail ? detail : "";
  bool changed = s_snap.state != state || s_snap.active != active ||
                 s_snap.online != online ||
                 s_snap.configured != configured ||
                 s_snap.phase != phase || s_snap.ledValid != ledValid ||
                 s_snap.led.r != led.r || s_snap.led.g != led.g ||
                 s_snap.led.b != led.b || s_snap.link != link ||
                 strcmp(s_snap.linkDetail, safeDetail) != 0 ||
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
  s_snap.link = link;
  snprintf(s_snap.linkDetail, sizeof(s_snap.linkDetail), "%s", safeDetail);
  s_snap.fetchedAt = millis();
  if (changed) s_snap.revision++;
  xSemaphoreGive(s_lock);
}

// GET /state with the bridge token. The transport picks the scheme: plain HTTP
// for the LAN address, validated TLS for the public https URL. The device never
// holds the Hermes key, only the bridge token.
static void publishFailure(BuddyLink link, const char *detail, bool configured) {
  publish(BUDDY_OFFLINE, "", 0, false, configured, 0, BUDDY_PHASE_IDLE,
          BuddyRgb{0, 180, 255}, false, link, detail);
}

// The bridge's JSON, turned into the snapshot. Unchanged from the LAN-only days:
// the transport moved, the contract did not.
static void parseState(JsonDocument &doc) {
  const char *name = doc["state"] | "";
  const char *label = doc["label"] | "";
  int active = doc["active"] | 0;
  // `since` is an epoch float. The device clock may not be synced yet, so only
  // trust the difference when both sides look like real time; the local
  // observation time is the fallback.
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

  // The HUD/LED channel: phase is authoritative and never derived from state.
  // The bridge resolves the LED colour; if the field is missing, use the
  // published table so the bead still means something.
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
  publish(buddyStateFromName(name), folded, active, true, true, age, phase, led,
          ledValid, BUDDY_LINK_OK);
}

static void pollOnce() {
  char urlText[SETTINGS_BUDDY_URL_MAX + 1];
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  settingsGetBuddyUrl(urlText, sizeof(urlText));
  settingsGetBuddyToken(token, sizeof(token));
  if (!urlText[0] || !token[0]) {
    publish(BUDDY_UNCONFIGURED, "", 0, false, false, 0, BUDDY_PHASE_IDLE,
            BuddyRgb{0, 180, 255}, false, BUDDY_LINK_UNCONFIGURED);
    return;
  }

  // A stored URL that cannot be used is its own cause: the request would fail
  // anyway and "sin señal" would hide the typo the owner has to fix.
  BuddyUrl url;
  BuddyUrlError urlErr = buddyUrlParse(urlText, &url);
  if (urlErr != BUDDY_URL_OK) {
    publishFailure(BUDDY_LINK_BAD_URL, buddyUrlErrorName(urlErr), true);
    return;
  }

  // HTTPS validates the certificate against the clock. Without SNTP the chain
  // reads as not-yet-valid, so say "no clock" instead of blaming the network.
  if (url.tls && !netStatus().timeSynced) {
    publishFailure(BUDDY_LINK_NO_TIME, "sntp", true);
    return;
  }

  BuddyHttpResult res;
  buddyHttpGetState(url, token, &res);
  BuddyLinkFacts facts;
  facts.configured = true;
  facts.urlValid = true;
  facts.tls = url.tls;
  facts.timeValid = netStatus().timeSynced;
  facts.status = res.status;
  facts.tlsFailed = res.tlsFailed;
  facts.redirected = res.redirected;
  BuddyLink link = buddyLinkFromFacts(facts);

  if (res.status == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, res.body);
    // A 200 with a body that is not the bridge's JSON is not a healthy link: the
    // face would show a state nobody sent.
    if (err) {
      publishFailure(BUDDY_LINK_HTTP, "body", true);
      return;
    }
    parseState(doc);
    return;
  }

  publishFailure(link, res.detail[0] ? res.detail : "", true);
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
  buddyHttpBegin();
  s_seed = esp_random();
  publish(BUDDY_UNCONFIGURED, "", 0, false, false);
  if (!s_task) {
    // Same pattern as the Spotify poller: its own small task, so the eyes never
    // stall the button or the RGB while the HTTP request is in flight.
    xTaskCreatePinnedToCore(pollTask, "buddy", BUDDY_TASK_STACK, nullptr, 1,
                            &s_task, 1);
  }
}

void buddyEnter() {
  s_dirty = true;
  s_lastDraw = 0;
  // Adopt the pose the next tick computes instead of easing in from the face
  // left behind by another screen.
  buddyPoseFilterReset(&s_filter);
  s_filterAt = 0;
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
  out->link = s_snap.link;
  snprintf(out->linkDetail, sizeof(out->linkDetail), "%s", s_snap.linkDetail);
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

// The eyes glow: three dithered rings around the shape, each one blended
// against the ring outside it, so the owner colour bleeds into the black like a
// lit bead instead of stepping at one hard outline. The panel is RGB565 with no
// alpha and no readable bus: ordered dithering is the honest way to fade here,
// and it keeps the mixture stable between frames. BUDDY_GLOW_PAD is also how far
// the lid bands reach past the eye.
#define BUDDY_GLOW_SHADE_INNER 32
#define BUDDY_GLOW_SHADE_MID 17
#define BUDDY_GLOW_SHADE_OUTER 7

static void drawEyeGlow(int x, int y, int halfW, int halfH, float deg,
                        uint32_t eyeRgb) {
  uint16_t outer = gfxHex(buddyEyeScale(eyeRgb, BUDDY_GLOW_SHADE_OUTER));
  uint16_t mid = gfxHex(buddyEyeScale(eyeRgb, BUDDY_GLOW_SHADE_MID));
  uint16_t inner = gfxHex(buddyEyeScale(eyeRgb, BUDDY_GLOW_SHADE_INNER));
  gfxFillSquircleRotAA(x, y, halfW + BUDDY_GLOW_PAD, halfH + BUDDY_GLOW_PAD,
                       deg, outer, GFX_BLACK);
  gfxFillSquircleRotAA(x, y, halfW + BUDDY_GLOW_PAD - 2,
                       halfH + BUDDY_GLOW_PAD - 2, deg, mid, outer);
  gfxFillSquircleRotAA(x, y, halfW + 2, halfH + 2, deg, inner, mid);
}

static void thickLine(int x0, int y0, int x1, int y1, uint16_t color) {
  gfxLine(x0, y0, x1, y1, color);
  gfxLine(x0, y0 + 1, x1, y1 + 1, color);
}

// A happy closed eye: a wide caret. No fill, so it reads as a smile line.
static void happyEye(int cx, int cy, uint16_t color) {
  thickLine(cx - 30, cy + 10, cx, cy - 16, color);
  thickLine(cx, cy - 16, cx + 30, cy + 10, color);
}

// A caret drawn as a halo: the glow has no filled shape to sit behind here, so
// the line itself gets the soft edge.
static void happyEyeGlow(int cx, int cy, uint16_t glow) {
  for (int d = -2; d <= 2; d++) {
    if (d == 0) continue;
    thickLine(cx - 30, cy + 10 + d, cx, cy - 16 + d, glow);
    thickLine(cx, cy - 16 + d, cx + 30, cy + 10 + d, glow);
  }
}

// One dithered row at a lid line. The band already painted the row black, so
// this only puts the eye colour back where the dither leaves it visible; only
// pixels inside the rotated eye are touched, so nothing leaks into the halo.
static void ditherLidRow(int x, int y, int halfW, int halfH, float deg,
                         int halfX, int row, uint16_t color, float black) {
  if (!(black > 0.0f) || black >= 1.0f) return;
  BuddySquircle sq = buddySquircle((float)halfW, (float)halfH);
  float a = deg * 0.0174532925f;
  float ca = cosf(a), sa = sinf(a);
  int dy = row - y;
  for (int dx = -halfX; dx <= halfX; dx++) {
    float lx = (float)dx * ca + (float)dy * sa;
    float ly = -(float)dx * sa + (float)dy * ca;
    if (!buddySquircleInside(lx, ly, sq)) continue;
    if (!buddyDitherLit(black, x + dx, row)) gfxPixel(x + dx, row, color);
  }
}

// Diagnostic snapshot of the last face the glass actually drew. The panel has
// no readable bus, so when a pixel looks wrong the question is always "which
// numbers produced it"; `buddydraw` on the console answers it.
struct BuddyDrawInfo {
  bool have = false;
  int x[2] = {0, 0};
  int rx[2] = {0, 0};
  int ry[2] = {0, 0};
  int extentY[2] = {0, 0};
  float deg[2] = {0, 0};
  float grow = 1.0f;
  float lid = 0;
  float hot = 0;
  int push = 0;
};
static BuddyDrawInfo s_draw;
// What the bead and the face are feeling right now, for buddy_led.cpp.
static BuddyMood s_mood = BUDDY_MOOD_CALM;
// The distraction currently on the glass, and the one the owner cancelled by
// talking to Sofía (it never comes back for that event).
static BuddyBug s_bug;
static uint32_t s_bugCancelled = 0xFFFFFFFFu;
static uint32_t s_bugForcedUntil = 0;
static bool s_zzz = false;
// True while the face is coming out of sleep: the lid was seeded shut and the
// ease is opening it.
static bool s_waking = false;

// A bug for the idle face to watch: an amber body, six legs and an antennae
// pair, wiggling. It is the only warm colour on the glass, so the eye finds it
// at once.
static void drawBug(const BuddyBug &b, uint32_t nowMs) {
  int x = (int)lroundf(b.x);
  int y = (int)lroundf(b.y);
  uint16_t body = gfxHex(0xFFB020);
  uint16_t leg = gfxHex(0x7A4A10);
  int wig = ((nowMs / 90u) % 2u) ? 1 : -1;
  gfxLine(x - 2, y + 2, x - 7, y + 5 + wig, leg);
  gfxLine(x - 1, y + 3, x - 6, y + 7 - wig, leg);
  gfxLine(x + 2, y + 2, x + 7, y + 5 - wig, leg);
  gfxLine(x + 1, y + 3, x + 6, y + 7 + wig, leg);
  gfxFillEllipse(x + 1, y, 6, 4, body);
  gfxFillCircle(x - 6, y - 1, 2, body);
  gfxLine(x - 7, y - 3, x - 12, y - 6, body);
  gfxLine(x - 7, y, x - 12, y + 2, body);
}

// The sleeping animation: three Z letters drifting up and fading. Nothing else
// moves while the face sleeps, so the letters are the whole show.
static void drawZzz(uint32_t nowMs, uint32_t eyeRgb) {
  for (int i = 0; i < BUDDY_ZZZ_COUNT; i++) {
    int x = 0, y = 0, alpha = 0;
    buddyZzz(nowMs, i, &x, &y, &alpha);
    gfxChar(x, y, 2, 'Z', gfxHex(buddyEyeScale(eyeRgb, alpha)));
  }
}

// --- HUD: the functional channel --------------------------------------------
//
// A 20 px badge in the top-right corner. The face may be ambiguous (focused vs
// bored); the HUD never lies. `busy` is motion, `ok` is a check with a green
// twinkle after a 250 ms pop, `ask` pulses, and idle leaves the glass clean.
// The synthesis (busy -> idle on Desktop/Telegram, where the bridge never sends
// `ok`) and the previous-phase guard live in buddy_phase.h so a host test can
// pin them; this file only draws what the machine says.

#define HUD_CX 302
#define HUD_CY 18

static BuddyHud s_hud;

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
  buddyHudStep(&s_hud, phase, nowMs);
}

static void hudBusy(BuddyRgb color, uint32_t nowMs) {
  for (int i = 0; i < 3; i++) {
    float phase = (float)((nowMs + (uint32_t)i * 180u) % 1200u) / 1200.0f *
                  6.2831853f;
    float opacity = 0.35f + 0.65f * (0.5f + 0.5f * cosf(phase));
    gfxFillCircle(HUD_CX - 7 + i * 7, HUD_CY, 2, hudRgb(color, opacity));
  }
}

// The check: a 250 ms pop, then a green twinkle that dies away into the steady
// mark. The twinkle is the part that reads as "done" from across the room; a
// static check is just a glyph.
static void hudCheck(uint32_t ageMs) {
  float scale = 1.0f;
  if (ageMs < 125) {
    scale = 0.8f + 0.3f * ((float)ageMs / 125.0f);
  } else if (ageMs < 250) {
    scale = 1.1f - 0.1f * ((float)(ageMs - 125) / 125.0f);
  }
  float twinkle = 0.0f;
  if (ageMs < 900) {
    float decay = 1.0f - (float)ageMs / 900.0f;
    twinkle = (0.5f + 0.5f * cosf((float)ageMs * 0.021f)) * decay;
  }
  uint16_t green = gfxColor(0, (uint8_t)(150.0f + 105.0f * twinkle),
                            (uint8_t)(30.0f + 60.0f * twinkle));
  int x1 = HUD_CX + (int)(-7.0f * scale);
  int y1 = HUD_CY + (int)(1.0f * scale);
  int x2 = HUD_CX + (int)(-2.0f * scale);
  int y2 = HUD_CY + (int)(6.0f * scale);
  int x3 = HUD_CX + (int)(8.0f * scale);
  int y3 = HUD_CY + (int)(-7.0f * scale);
  thickLine(x1, y1, x2, y2, green);
  thickLine(x2, y2, x3, y3, green);
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
  buddyHudStep(&s_hud, phase, nowMs);
  BuddyPhase shown = buddyHudShown(&s_hud, nowMs);
  if (shown == BUDDY_PHASE_BUSY) {
    hudBusy(led, nowMs);
  } else if (shown == BUDDY_PHASE_ASK) {
    hudAsk(nowMs);
  } else if (shown == BUDDY_PHASE_OK) {
    hudCheck(buddyHudCheckAge(&s_hud, nowMs));
  }
}

static void drawFace(BuddyState state, const BuddyPose &pose, uint32_t nowMs,
                     bool poke) {
  (void)nowMs;
  gfxClear(GFX_BLACK);

  const int cx[2] = {105, 215};
  const int cy = 88;
  bool happy = pose.happy || poke;
  // The owner's colour is read once per frame (the getter is cached in RAM).
  // Offline dims it; anger and dizziness blend it towards hot red on top, and
  // the owner's choice comes back as soon as the reaction expires.
  uint32_t owner = settingsGetBuddyEyeColor();
  uint32_t eyeRgb = pose.dim ? buddyEyeDim(owner) : buddyEyeHot(owner, pose.hot);
  uint16_t color = gfxHex(eyeRgb);
  uint16_t glowInner = gfxHex(buddyEyeScale(eyeRgb, BUDDY_GLOW_SHADE_INNER));
  uint16_t glowMid = gfxHex(buddyEyeScale(eyeRgb, BUDDY_GLOW_SHADE_MID));
  int bob = (int)pose.bob;

  // Where each eye lands, and how much screen it covers once rotated. The pair
  // is pushed apart when the surprise widens them or a tilt eats the gap; the
  // owner should never see the two eyes (or their glow) merge into one blob.
  bool shut = pose.blink || pose.closed;
  int eyeX[2];
  int eyeRx[2];
  int eyeExtentX[2];
  int eyeExtentY[2];
  float eyeDeg[2];
  float grow = pose.grow > 0.2f ? pose.grow : 1.0f;
  int baseRx = (int)lroundf((float)pose.rx * grow);
  int baseRy = shut ? 4 : (int)lroundf((float)pose.ry * grow);
  BuddyPerspective per = buddyPerspective(pose.gazeX);
  for (int i = 0; i < 2; i++) {
    float widen = i == 0 ? per.widenL : per.widenR;
    float lead = i == 0 ? per.pushL : per.pushR;
    eyeRx[i] = (int)lroundf((float)baseRx * widen);
    eyeX[i] = cx[i] + (int)(pose.gazeX + lead +
                            (i == 0 ? pose.eyeGazeL : pose.eyeGazeR));
    eyeDeg[i] = pose.rot + (i == 0 ? pose.rotL : pose.rotR);
    eyeExtentX[i] = (int)ceilf(
        buddyEyeExtentX((float)eyeRx[i], (float)baseRy, eyeDeg[i]));
    eyeExtentY[i] = (int)ceilf(
        buddyEyeExtentY((float)eyeRx[i], (float)baseRy, eyeDeg[i]));
  }
  int push = buddyEyeGapPush(
      (float)(eyeX[1] - eyeX[0]),
      (float)(eyeExtentX[0] + BUDDY_GLOW_PAD),
      (float)(eyeExtentX[1] + BUDDY_GLOW_PAD), (float)BUDDY_EYE_GAP_MIN);
  eyeX[0] -= push;
  eyeX[1] += push;
  s_draw.have = true;
  s_draw.push = push;
  s_draw.grow = grow;
  s_draw.lid = pose.lid;
  s_draw.hot = pose.hot;
  for (int i = 0; i < 2; i++) {
    s_draw.x[i] = eyeX[i];
    s_draw.rx[i] = eyeRx[i];
    s_draw.ry[i] = baseRy;
    s_draw.extentY[i] = eyeExtentY[i];
    s_draw.deg[i] = eyeDeg[i];
  }

  for (int i = 0; i < 2; i++) {
    // Solid eyes, no pupils: life comes from the gaze and the lids. The error
    // face adds a per-eye offset so the eyes point at each other.
    int x = eyeX[i];
    int y = cy + bob + (int)pose.gazeY;
    if (happy) {
      happyEyeGlow(x, y, glowMid);
      happyEye(x, y, color);
      continue;
    }
    float deg = shut ? 0.0f : eyeDeg[i];
    drawEyeGlow(x, y, eyeRx[i], baseRy, deg, eyeRgb);
    if (shut) {
      // A shut eye is a short rounded bar: it keeps the squircle language and
      // reads as a lid rather than a stray rectangle.
      gfxFillSquircleRotAA(x, y, eyeRx[i], 4, 0, color, glowInner);
      continue;
    }
    gfxFillSquircleRotAA(x, y, eyeRx[i], baseRy, deg, color, glowInner);
    // A black band over the top of the eye: the eyelid. Black on black is free,
    // so the eye is simply clipped. It reaches past the rotated eye and its
    // glow, because a 16 deg angry tilt makes the eye taller than pose.ry and
    // the hot edge used to show above the lid. The two eyes can have different
    // lids (the EMO thinking wink) and a bottom lid cuts the tired look.
    float top = pose.lid + (i == 0 ? pose.lidL : pose.lidR);
    int bandHalfX = eyeExtentX[i] + BUDDY_GLOW_PAD + 2;
    // The eyelid is a black block whose edge is parallel to the eye's top edge:
    // flat on a straight eye, slanted when the eye is turned. It also covers the
    // halo above it, so no tinted sliver survives.
    gfxLidCutRot(x, y, eyeRx[i], baseRy, deg, top, BUDDY_GLOW_PAD, GFX_BLACK);
    BuddyLidBand bottomBand =
        buddyLidBandBottom(y, eyeExtentY[i], BUDDY_GLOW_PAD, pose.bottom);
    if (bottomBand.h > 0) {
      gfxFillRect(x - bandHalfX, bottomBand.y0, bandHalfX * 2, bottomBand.h,
                  GFX_BLACK);
      float line = buddyLidLineBottom(y, eyeExtentY[i], pose.bottom);
      ditherLidRow(x, y, eyeRx[i], baseRy, deg, bandHalfX, bottomBand.y0, color,
                   1.0f - (line - floorf(line)));
    }
    if (state == BUDDY_ERROR) {
      // Two crossed black strokes inside the eye: the classic x x.
      thickLine(x - 20, y - 22, x + 20, y + 22, GFX_BLACK);
      thickLine(x - 20, y + 22, x + 20, y - 22, GFX_BLACK);
    }
  }

  // The two things that can share the glass with the eyes: the sleeping letters
  // and the bug the face is watching. Both are drawn last, on top.
  if (s_zzz) drawZzz(nowMs, eyeRgb);
  if (s_bug.active) drawBug(s_bug, nowMs);
}

// One sensor sample into the reaction machine. The live path and the debug pin
// share it, so a pinned face still knows whether the board is lying down.
static BuddyImuPose readImu(uint32_t nowMs) {
  float accel[3], gyro[3];
  imuReadAll(accel, gyro);
  // The face reads the board, not the glass: when the image is the other one of
  // the pair she has to lean the other way to keep leaning downhill to the owner.
  rotMirrorInPlane(accel, gyro, rotationInPlaneSign());
  uint32_t dt = s_imuAt ? (nowMs - s_imuAt) : BUDDY_FRAME_MS;
  s_imuAt = nowMs;
  buddyImuFeed(&s_imu, accel, gyro, nowMs, dt);
  return buddyImuPose(&s_imu, nowMs);
}

bool buddyTick(uint32_t nowMs) {
  if (s_pin) {
    if ((int32_t)(s_pinStart + s_pinHold - nowMs) <= 0) {
      s_pin = false;
      // The pinned expression is not part of the live pose: re-adopt the real
      // one instead of easing across two unrelated faces.
      buddyPoseFilterReset(&s_filter);
      s_filterAt = 0;
      s_dirty = true;
    } else {
      if ((uint32_t)(nowMs - s_lastDraw) < BUDDY_FRAME_MS) return false;
      s_drawnState = s_pinState;
      BuddyImuPose ip = readImu(nowMs);
      BuddyPose pose =
          buddyPose(s_pinState, s_pinAge + (nowMs - s_pinStart), nowMs, s_seed);
      if (ip.wake && pose.closed) buddyWakeEyes(&pose);
      s_bug.active = false;
      s_zzz = pose.closed && ip.lying && ip.lyingMs > 6000;
      s_mood = buddyMoodFrom(pose.closed, ip.angry, pose.tremble);
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

  bool animate = (uint32_t)(nowMs - s_lastDraw) >= BUDDY_FRAME_MS;
  if (!news && !animate) return false;

  uint32_t shownAge = s_shownBaseAge + (uint32_t)(nowMs - s_shownAt);
  BuddyPose pose = buddyPose(shown, shownAge, nowMs, s_seed);
  if (shown == BUDDY_TOOL && (int32_t)(s_surpriseUntil - nowMs) > 0) {
    pose.surprise = true;
    pose.rx = 50;
    pose.ry = 60;
    pose.lid = 0.0f;
  }

  // Physical layer: the IMU is local and the bridge never sees it. A missing
  // sensor reads zero, which the feed ignores, and every reaction expires, so
  // the face can always come back to the state pose.
  BuddyImuPose ip = readImu(nowMs);
  pose.gazeX += ip.gazeX;
  pose.gazeY += ip.gazeY;
  if (ip.lid > pose.lid) pose.lid = ip.lid;
  if (ip.squint && pose.lid < 0.45f) pose.lid = 0.45f;
  pose.rot += ip.rot;
  pose.rotL += ip.rotL;
  pose.rotR += ip.rotR;
  if (ip.hot > pose.hot) pose.hot = ip.hot;
  // The expression anatomy the IMU adds: anger grows the eye, turns the lids
  // into the angled blocks and shakes; dizziness only droops.
  if (ip.grow > 1.0f) pose.grow = ip.grow;
  if (ip.bottom > pose.bottom) pose.bottom = ip.bottom;
  if (ip.tremble > pose.tremble) pose.tremble = ip.tremble;
  if (ip.wide) {
    pose.surprise = true;
    pose.rx = 50;
    pose.ry = 60;
  }
  if (ip.faceDown) pose.lid = 0.6f;
  // Lifted off the table, or the board moved while the face was asleep: wake
  // with a start, then let the state take over again.
  // Waking: seed the lid shut once so the opening travels through the ease
  // instead of popping, then let the state take over.
  if (ip.wake) {
    if (pose.closed && !s_waking) {
      s_waking = true;
      buddyPoseFilterSeedLid(&s_filter, 1.0f);
    }
    if (pose.closed || shown == BUDDY_BORED) buddyWakeEyes(&pose);
  } else {
    s_waking = false;
  }
  if (poke) pose.happy = true;

  // Distractions: only while the face is genuinely idle and Sofía is doing
  // nothing. The moment she is spoken to (any other state, phase or a tap) the
  // event is cancelled for good; the eyes follow the bug otherwise.
  BuddyBug bug = buddyBugAt(nowMs, s_seed);
  // Console override: `bug <seconds>` puts the distraction on the glass now, so
  // the probe can prove it without waiting for the hashed window.
  if ((int32_t)(s_bugForcedUntil - nowMs) > 0) {
    bug.active = true;
    bug.eventId = 0xFFFFFFFEu;
    bug.x = 40.0f + (float)((nowMs / 20u) % 240u);
    bug.y = 70.0f;
  }
  // "Idle" here means awake and not being talked to: the idle face and the bored
  // ladder (which is exactly when the owner expects her to find something to
  // play with). A face whose eyes are shut is asleep, and sleeps through it.
  bool awakeIdle = (shown == BUDDY_IDLE || shown == BUDDY_BORED) && !pose.closed;
  bool quiet = awakeIdle && view.phase == BUDDY_PHASE_IDLE && !poke;
  if (!quiet && bug.active) s_bugCancelled = bug.eventId;
  if (bug.active && bug.eventId == s_bugCancelled) bug.active = false;
  if (bug.active) {
    float gx = 0, gy = 0;
    buddyBugGaze(bug, &gx, &gy);
    pose.gazeX = gx;
    pose.gazeY = gy;
  }
  s_bug = bug;

  // The sleeping letters need both halves: eyes shut (deep boredom) and the
  // board lying on the table for a while.
  s_zzz = pose.closed && ip.lying && ip.lyingMs > 6000;
  s_mood = buddyMoodFrom(pose.closed, ip.angry, pose.tremble);

  // Ease the whole pose. dt is clamped so a stalled frame (or returning from a
  // pinned face) never turns one step into a jump.
  uint32_t poseDt = s_filterAt ? (nowMs - s_filterAt) : BUDDY_FRAME_MS;
  if (poseDt > 200) poseDt = 200;
  s_filterAt = nowMs;
  BuddyPose eased = buddyPoseFilterStep(&s_filter, pose, poseDt);

  // The tremble is noise, not intent: it is applied after the ease, because a
  // 100 ms filter would flatten a 25 Hz shake into a slow sway.
  if (eased.tremble > 0.01f) {
    eased.gazeX += buddyTremble(nowMs, s_seed, 11u, 1.6f);
    eased.gazeY += buddyTremble(nowMs, s_seed, 23u, 1.0f);
    eased.rot += buddyTremble(nowMs, s_seed, 37u, 2.2f);
  }

  drawFace(shown, eased, nowMs, poke);
  hudTrack(view.phase, nowMs);
  drawHud(view.phase,
          view.ledValid ? view.led : buddyPhaseDefaultLed(view.phase), nowMs);
  s_drawnRevision = view.revision;
  s_lastDraw = nowMs;
  s_dirty = false;
  return true;
}

BuddyMood buddyMood() { return s_mood; }

void buddyForceBug(uint32_t holdMs) {
  s_bugForcedUntil = millis() + holdMs;
  s_bugCancelled = 0xFFFFFFFFu;
}

void buddyDumpDraw() {
  if (!s_draw.have) {
    Serial.println("buddydraw: todavia no dibujo");
    return;
  }
  static const char *kMoods[] = {"calm", "angry", "sleeping"};
  Serial.printf("draw mood=%s grow=%.2f lid=%.2f hot=%.2f push=%d\n",
                kMoods[s_mood & 3], s_draw.grow, s_draw.lid, s_draw.hot,
                s_draw.push);
  for (int i = 0; i < 2; i++) {
    Serial.printf("eye%d x=%d rx=%d ry=%d extY=%d deg=%.1f\n", i, s_draw.x[i],
                  s_draw.rx[i], s_draw.ry[i], s_draw.extentY[i],
                  s_draw.deg[i]);
  }
}

// One sentence per cause, because that is what the owner gets in the panel
// toast: the transport knows the code, the owner needs the next step.
static const char *buddyAskError(BuddyLink link, int status) {
  switch (link) {
    case BUDDY_LINK_UNCONFIGURED: return "Falta la URL o el token";
    case BUDDY_LINK_BAD_URL: return "La URL del bridge no es valida";
    case BUDDY_LINK_NO_TIME: return "El reloj todavia no se sincronizo";
    case BUDDY_LINK_TLS: return "Rechazaron el certificado del bridge";
    case BUDDY_LINK_BLOCKED: return "Cloudflare bloqueo el pedido";
    case BUDDY_LINK_UNAUTHORIZED: return "El token del bridge no es valido";
    case BUDDY_LINK_HTTP: break;
    case BUDDY_LINK_OFFLINE: return "No pude conectar al bridge";
    case BUDDY_LINK_OK: break;
  }
  (void)status;
  return "El bridge respondio con un error";
}

bool buddyAsk(const char *prompt, char *err, size_t errCap) {
  if (err && errCap) err[0] = '\0';
  if (!prompt || !*prompt) {
    if (err && errCap) snprintf(err, errCap, "Escribi un pedido");
    return false;
  }
  char urlText[SETTINGS_BUDDY_URL_MAX + 1];
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  settingsGetBuddyUrl(urlText, sizeof(urlText));
  settingsGetBuddyToken(token, sizeof(token));
  if (!urlText[0] || !token[0]) {
    if (err && errCap) snprintf(err, errCap, "Falta la URL o el token");
    return false;
  }

  BuddyUrl url;
  BuddyUrlError urlErr = buddyUrlParse(urlText, &url);
  if (urlErr != BUDDY_URL_OK) {
    if (err && errCap) snprintf(err, errCap, "La URL del bridge no es valida");
    return false;
  }
  // The same order the poll uses: a clock-less https attempt cannot validate the
  // certificate, so the honest answer is about time, not about the network.
  if (url.tls && !netStatus().timeSynced) {
    if (err && errCap) snprintf(err, errCap, "El reloj todavia no se sincronizo");
    return false;
  }

  JsonDocument doc;
  doc["prompt"] = prompt;
  String body;
  serializeJson(doc, body);

  BuddyHttpResult res;
  buddyHttpAsk(url, token, body, &res);
  BuddyLinkFacts facts;
  facts.configured = true;
  facts.urlValid = true;
  facts.tls = url.tls;
  facts.timeValid = netStatus().timeSynced;
  facts.status = res.status;
  facts.tlsFailed = res.tlsFailed;
  facts.redirected = res.redirected;
  BuddyLink link = buddyLinkFromFacts(facts);
  if (link != BUDDY_LINK_OK) {
    if (err && errCap) {
      snprintf(err, errCap, "%s", buddyAskError(link, res.status));
    }
    return false;
  }
  // The bridge answers 202 and runs asynchronously. Paint thinking locally so
  // the eyes react at once; the next poll corrects with the real state.
  publish(BUDDY_THINKING, "", 0, true, true, 0, BUDDY_PHASE_BUSY,
          buddyPhaseDefaultLed(BUDDY_PHASE_BUSY), true, BUDDY_LINK_OK);
  return true;
}

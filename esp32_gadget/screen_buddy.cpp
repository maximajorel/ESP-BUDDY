#include "screen_buddy.h"
#include "buddy_game.h"

#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

#include "buddy_anim.h"
#include "buddy_badge.h"
#include "buddy_ball.h"
#include "buddy_face.h"
#include "buddy_focus_state.h"
#include "buddy_http.h"
#include "buddy_imu.h"
#include "buddy_event.h"
#include "buddy_link.h"
#include "buddy_sleep.h"
#include "buddy_url.h"
#include "buddy_wake.h"
#include "gfx.h"
#include "imu.h"
#include "net.h"
#include "rgb.h"
#include "rotation.h"
#include "settings.h"
#include "tap_window.h"
#include "text_fold.h"

// --- snapshot shared with the poller task -----------------------------------

#define BUDDY_POLL_MS 1000
// The long-poll: how long the bridge may hold the request, and how many failures
// in a row turn the task back into the 1 s poll. Three is the number in the
// bridge contract: one dropped long-poll is normal, three is a transport that
// cannot be trusted with the event stream.
#define BUDDY_EV_WAIT_SEC 25
#define BUDDY_EV_FAILS_TO_POLL 3
// A bridge without /events is probed this often. Not every poll: the 404 costs a
// full round trip and an old bridge will never grow the endpoint while we ask.
#define BUDDY_EV_PROBE_MS 60000L
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
  // `sleep_hint` is the bridge's silence, not an order: the local sleep clock in
  // buddy_sleep.h takes the MIN of this and the board's own silence.
  float sleepHintSec = 0.0f;
  bool sleepHintValid = false;
  int evSeq = -1;             // cursor for the /events long-poll
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
// The board's own patience, and whether the last frame drew the asleep pose.
// Zero-initialised means "moved at boot": the first idle stretch starts counting
// at power-on instead of waiting for a shake that may never come.
static BuddySleepClock s_sleep;
static bool s_asleepDrawn = false;
// The `/events` cursor: the last `seq` applied. It lives in the poller task and
// nowhere else, so it needs no lock. -1 means "no /state yet", which asks the ring
// for everything it still holds instead of replaying it from `since=0`.
static int s_evCursor = -1;
// The bridge's own cursor, from the last /state. When it goes backwards the ring
// restarted, and our cursor has to follow instead of waiting for a number that
// will never arrive.
static int s_evServerSeq = -1;
// Reaction deadlines set by an event (or by a poll that saw the same change), so
// the frame can paint them before the bridge's own answer arrives. 0 = nothing
// queued.
static uint32_t s_listenUntil = 0;
static uint32_t s_doneWinUntil = 0;
static uint32_t s_doneReliefUntil = 0;
// An event carried a state but not the HUD's phase or the bead's colour, so one
// /state follows to take them from the bridge instead of leaving the last ones on
// the glass. Same task, so no lock is needed.
static bool s_stateRefresh = false;
static bool s_pin = false;
static BuddyState s_pinState = BUDDY_IDLE;
static uint32_t s_pinAge = 0;
static uint32_t s_pinStart = 0;
static uint32_t s_pinHold = 0;
static bool s_phasePinned = false;
static BuddyPhase s_phasePinValue = BUDDY_PHASE_IDLE;
static uint32_t s_phasePinUntil = 0;
static bool s_dirty = true;
// Whether the corner currently shows the no-link mark instead of the HUD phase.
static bool s_badge = false;
// The eased face. The ladders produce a target every frame; this filter is what
// actually moves the eyes, so no field ever jumps between two frames.
static BuddyPoseFilter s_filter;
static uint32_t s_filterAt = 0;
// The idle ball: the owner's toggle (survives a bridge pause), the physics body
// and the clock of its last step. `s_accelRaw` keeps the raw sample: the ball's
// gravity goes through the steering calibration, which the wizard captured on
// the raw axes. The two filtered channels below separate steady tilt (gravity)
// from the shake on top of it.
static BuddyBall s_ball;
static bool s_ballOn = false;
static uint32_t s_ballAt = 0;
static float s_accelRaw[3] = {0, 0, 0};
static float s_ballTiltLat = 0.0f;
static float s_ballTiltFwd = 0.0f;
static bool s_ballPrimed = false;
// The one-tap/boop vs two-tap/ball decision. A touch panel would know the
// difference at once; one button does not, so two presses inside this window
// are the ball. It is measured release to release (the press duration is part of
// the gap), which is why it is more generous than the 400 ms of the pickers.
#define BUDDY_TAP_DOUBLE_MS 600

static TapWindow s_tap = {false, 0};

static void publish(BuddyState state, const char *label, int active,
                    bool online, bool configured, uint32_t stateAgeMs = 0,
                    BuddyPhase phase = BUDDY_PHASE_IDLE,
                    BuddyRgb led = {0, 180, 255}, bool ledValid = false,
                    BuddyLink link = BUDDY_LINK_OK, const char *detail = nullptr,
                    float sleepHintSec = -1.0f, int evSeq = -1) {
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
                 strcmp(s_snap.label, label ? label : "") != 0 ||
                 (sleepHintSec >= 0.0f && sleepHintSec != s_snap.sleepHintSec);
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
  // A negative hint means "the bridge did not send one this time"; it keeps the
  // previous value instead of un-sleeping the face on a malformed body.
  if (sleepHintSec >= 0.0f) {
    s_snap.sleepHintSec = sleepHintSec;
    s_snap.sleepHintValid = true;
  }
  if (evSeq >= 0) s_snap.evSeq = evSeq;
  snprintf(s_snap.linkDetail, sizeof(s_snap.linkDetail), "%s", safeDetail);
  s_snap.fetchedAt = millis();
  if (changed) s_snap.revision++;
  xSemaphoreGive(s_lock);
}

// The state the snapshot already holds, for the one decision that needs the
// previous value: a `done` that arrived from a poll instead of an event carries
// no `prev`, and the snapshot is where the previous state still lives.
static BuddyState snapshotState() {
  BuddyState st = BUDDY_UNCONFIGURED;
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    st = s_snap.state;
    xSemaphoreGive(s_lock);
  }
  return st;
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
  BuddyState next = buddyStateFromName(name);
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
  // `sleep_hint` is Hermes' silence in seconds and `ev_seq` is the cursor for
  // /events. Both are optional: an old bridge simply omits them, and then the
  // sleep clock runs on the board's own silence alone.
  float hint = doc["sleep_hint"] | -1.0f;
  if (!(hint >= 0.0f) || hint > 86400.0f) hint = -1.0f;
  int evSeq = doc["ev_seq"] | -1;
  if (evSeq < 0) evSeq = -1;
  // The cursor for the long-poll. A bridge without the field leaves it alone, and
  // then the event task keeps asking from the last known seq.
  if (evSeq >= 0) {
    s_evServerSeq = evSeq;
    s_evCursor = evSeq;
  }
  // The poll path gets the same graded `done` as the event stream: the previous
  // state is whatever the snapshot already held. Without this, a bridge without
  // /events would show one flat celebration for both endings.
  if (next == BUDDY_DONE) {
    BuddyState prev = snapshotState();
    if (prev == BUDDY_TOOL) {
      s_doneWinUntil = millis() + 1500;
    } else if (prev == BUDDY_THINKING) {
      s_doneReliefUntil = millis() + 1500;
    }
  }
  publish(next, folded, active, true, true, age, phase, led, ledValid,
          BUDDY_LINK_OK, nullptr, hint, evSeq);
}

// Resolves the stored target both transports need. Returns false when the reason
// was already published — unconfigured, a URL that cannot be parsed, a clock that
// cannot validate a certificate — so the caller only has to wait and try again.
static bool resolveTarget(BuddyUrl *url, char *token, size_t tokenCap) {
  char urlText[SETTINGS_BUDDY_URL_MAX + 1];
  settingsGetBuddyUrl(urlText, sizeof(urlText));
  settingsGetBuddyToken(token, tokenCap);
  if (!urlText[0] || !token[0]) {
    publish(BUDDY_UNCONFIGURED, "", 0, false, false, 0, BUDDY_PHASE_IDLE,
            BuddyRgb{0, 180, 255}, false, BUDDY_LINK_UNCONFIGURED);
    return false;
  }

  // A stored URL that cannot be used is its own cause: the request would fail
  // anyway and "sin señal" would hide the typo the owner has to fix.
  BuddyUrlError urlErr = buddyUrlParse(urlText, url);
  if (urlErr != BUDDY_URL_OK) {
    publishFailure(BUDDY_LINK_BAD_URL, buddyUrlErrorName(urlErr), true);
    return false;
  }

  // HTTPS validates the certificate against the clock. Without SNTP the chain
  // reads as not-yet-valid, so say "no clock" instead of blaming the network.
  if (url->tls && !netStatus().timeSynced) {
    publishFailure(BUDDY_LINK_NO_TIME, "sntp", true);
    return false;
  }
  return true;
}

// One `GET /state`. Also the only place that learns the `/events` cursor, which
// is why the two paths share it.
static void pollOnce() {
  BuddyUrl url;
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  if (!resolveTarget(&url, token, sizeof(token))) return;

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

// --- the event stream -------------------------------------------------------
//
// Set by an event so the frame can paint the reaction before the bridge's own
// answer arrives. 0 means nothing is queued.
// Applies an event's state at once and keeps everything else the snapshot had,
// then asks for one fresh /state. The face reacts in milliseconds while the HUD's
// phase and the bead's colour still come from the bridge instead of a guess.
static void applyEventState(BuddyState state, const char *label, uint32_t ageMs) {
  int active = 0;
  BuddyPhase phase = BUDDY_PHASE_IDLE;
  BuddyRgb led = {0, 180, 255};
  bool ledValid = false;
  float hint = -1.0f;
  int evSeq = -1;
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    active = s_snap.active;
    phase = s_snap.phase;
    led = s_snap.led;
    ledValid = s_snap.ledValid;
    if (s_snap.sleepHintValid) hint = s_snap.sleepHintSec;
    evSeq = s_snap.evSeq;
    xSemaphoreGive(s_lock);
  }
  publish(state, label, active, true, true, ageMs, phase, led, ledValid,
          BUDDY_LINK_OK, nullptr, hint, evSeq);
  s_stateRefresh = true;
}

// Decodes the array. Returns the number of events applied, or -1 when the body
// was not the array the bridge promises (a 200 with HTML is a captive portal, and
// it must not touch the face).
static int parseEvents(const String &body, uint32_t nowMs) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return -1;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return -1;

  int applied = 0;
  for (JsonVariant item : arr) {
    JsonObject ev = item.as<JsonObject>();
    if (ev.isNull()) continue;
    BuddyEvent e;
    e.seq = ev["seq"] | -1;
    e.kind = buddyEventKindFromName(ev["event"] | "");
    // A retried long-poll hands back events from before the last one; the cursor
    // is the only thing that keeps the face from replaying them.
    if (!buddyEventApply(s_evCursor, e.seq)) continue;

    if (e.kind == BUDDY_EVENT_ASK_ACCEPTED) {
      // "Te escuche": without it the button feels dead until the first thinking.
      s_listenUntil = nowMs + 800;
      s_dirty = true;
    } else if (e.kind == BUDDY_EVENT_STATE || e.kind == BUDDY_EVENT_LABEL) {
      const char *name = ev["state"] | "";
      const char *rawLabel = ev["label"] | "";
      char clean[BUDDY_LABEL_MAX + 1];
      char folded[BUDDY_LABEL_MAX + 1];
      buddyLabelClean(rawLabel, clean, sizeof(clean));
      textFoldAscii(clean, folded, sizeof(folded));
      BuddyState state = buddyStateFromName(name);
      if (e.kind == BUDDY_EVENT_STATE) {
        const char *prevName = ev["prev"] | "";
        e.prev = *prevName ? buddyStateFromName(prevName) : BUDDY_UNCONFIGURED;
        if (state == BUDDY_DONE) {
          // The direction of the change is the emotion: a finished tool is a win,
          // a finished thought is relief.
          if (buddyDoneReaction(e.prev, 0) == BUDDY_DONE_WIN) {
            s_doneWinUntil = nowMs + 1500;
          } else if (buddyDoneReaction(e.prev, 0) == BUDDY_DONE_RELIEF) {
            s_doneReliefUntil = nowMs + 1500;
          }
        }
      }
      applyEventState(state, folded, 0);
      s_dirty = true;
    }
    s_evCursor = e.seq;
    applied++;
  }
  return applied;
}

// One long-poll. Returns 0 when the transport worked (including the `[]` that
// means "nothing new"), 1 when /events is missing on this bridge, and 2 on a
// transport failure.
static int eventsOnce(uint32_t nowMs) {
  BuddyUrl url;
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  if (!resolveTarget(&url, token, sizeof(token))) return 2;

  // The ring restarted: its cursor is older than ours, so `since` would ask for
  // events we will never see. Adopt the bridge's position and carry on.
  if (s_evServerSeq >= 0 && buddyEventsNeedResync(s_evServerSeq, s_evCursor)) {
    s_evCursor = s_evServerSeq;
  }

  BuddyHttpResult res;
  int since = s_evCursor < 0 ? 0 : s_evCursor;
  buddyHttpGetEvents(url, token, since, BUDDY_EV_WAIT_SEC, &res);

  if (res.status == 404) return 1;
  if (res.status != 200) return 2;
  if (parseEvents(res.body, nowMs) < 0) return 2;
  return 0;
}

// Which transport the task is using. The poll never disappears: it is the
// fallback for a bridge without /events, a network that cannot hold a socket, and
// the resync after the bridge restarts its ring.
enum BuddyNetMode { BUDDY_NET_EVENTS, BUDDY_NET_POLL };

static void pollTask(void *arg) {
  (void)arg;
  BuddyNetMode mode = BUDDY_NET_EVENTS;
  int fails = 0;
  uint32_t lastPoll = 0;
  uint32_t lastProbe = 0;

  for (;;) {
    uint32_t now = millis();

    if (mode == BUDDY_NET_EVENTS) {
      // A cursor the bridge no longer knows (it restarted) answers `[]`, so the
      // only way forward is one /state, which also restores the cursor.
      if (s_evCursor < 0) {
        pollOnce();
        // Still no cursor: this bridge does not speak `ev_seq`, so there is
        // nothing to long-poll from. Stay on the 1 s poll and probe later.
        if (s_evCursor < 0) {
          mode = BUDDY_NET_POLL;
          lastProbe = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(BUDDY_POLL_MS));
        continue;
      }
      int rc = eventsOnce(now);
      if (rc == 1) {
        mode = BUDDY_NET_POLL;
        lastProbe = millis();
        continue;
      }
      if (rc == 2) {
        if (++fails >= BUDDY_EV_FAILS_TO_POLL) {
          mode = BUDDY_NET_POLL;
          lastProbe = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(BUDDY_POLL_MS));
        continue;
      }
      fails = 0;
      if (s_stateRefresh) {
        // The face already reacted to the event; this only brings the HUD's phase
        // and the bead's colour up to date, which the event does not carry.
        s_stateRefresh = false;
        pollOnce();
      }
      // `[]` (a timeout or a restarted ring) answers immediately, so without this
      // the loop would hammer the bridge; and a delivery that lands mid-request
      // must not be invisible until the next wait.
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // BUDDY_NET_POLL: one /state per second, and a probe for /events now and
    // then so a firmware upgrade on the bridge is picked up without a reboot.
    if ((uint32_t)(now - lastPoll) >= BUDDY_POLL_MS) {
      lastPoll = now;
      pollOnce();
      fails = 0;
    }
    if ((uint32_t)(now - lastProbe) >= (uint32_t)BUDDY_EV_PROBE_MS) {
      lastProbe = now;
      mode = BUDDY_NET_EVENTS;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
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
  tapWindowReset(s_tap);
}

void buddyPinHappy(uint32_t holdMs) {
  s_pokeUntil = millis() + holdMs;
  s_dirty = true;
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
  // The first tap boops at once (waiting out the window made the face feel
  // dead), and a second tap inside the window turns the first into the toggle.
  // The boop already played, which is fine: she is happy you are playing. The
  // old wait-for-the-poll design also lost the toggle when the second tap was a
  // normal human double click.
  TapResult tap = tapWindowPress(s_tap, millis(), BUDDY_TAP_DOUBLE_MS);
  if (tap == TAP_DOUBLE) {
    buddySetBall(!s_ballOn);
  } else {
    s_pokeUntil = millis() + 1100;
    rgbPulse(255, 180, 220, 500);
  }
  s_dirty = true;
}

void buddySetBall(bool on) {
  if (on == s_ballOn) return;
  s_ballOn = on;
  if (on) {
    buddyBallSpawn(&s_ball, s_seed ^ millis());
    s_ballAt = 0;
    s_ballPrimed = false;
  } else {
    buddyBallClear(&s_ball);
    s_ballAt = 0;
    s_ballPrimed = false;
  }
  s_dirty = true;
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
  out->sleepHintSec = s_snap.sleepHintSec;
  out->sleepHintValid = s_snap.sleepHintValid;
  out->evSeq = s_snap.evSeq;
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

// The red toy the owner bounces around. A rim and an off-centre highlight make a
// sphere out of a flat RGB565 disc without any alpha.
#define BUDDY_BALL_COLOR 0xE81C10u
#define BUDDY_BALL_RIM 0x7A0A06u
#define BUDDY_BALL_HIGHLIGHT 0xFFCFC8u

static void drawBall(const BuddyBall &b) {
  if (!b.active) return;
  int x = (int)lroundf(b.x);
  int y = (int)lroundf(b.y);
  int r = (int)BUDDY_BALL_RADIUS;
  gfxFillCircle(x, y, r, gfxHex(BUDDY_BALL_COLOR));
  gfxCircle(x, y, r, gfxHex(BUDDY_BALL_RIM));
  gfxFillCircle(x - r / 3, y - r / 3, r / 4, gfxHex(BUDDY_BALL_HIGHLIGHT));
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
  float open = 0;
  float glow = 0;
  float lid = 0;
  float hot = 0;
  int push = 0;
};
static BuddyDrawInfo s_draw;
// What the bead and the face are feeling right now, for buddy_led.cpp.
static BuddyMood s_mood = BUDDY_MOOD_CALM;
// The web game's overlay and the last mood actually drawn, so its expiry is a
// repaint instead of a face stuck in the game's last expression.
static BuddyGameOverride s_game;
static BuddyGameMood s_gameDrawn = BUDDY_GAME_NONE;
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
// nodding off); the HUD never lies. `busy` is motion, `ok` is a check with a green
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

// No bridge: the corner says so, and the eyes stay.
//
// The plate used to lose its whole face to a full-screen WiFi badge. The owner
// asked for the opposite (L19): keep the eyes and put a phone's WiFi mark in the
// HUD corner. A dead link is still the one thing he can act on, so the mark keeps
// the alarm channel the full-screen badge had, and only the size changed.
#define HUD_WIFI_APEX_Y (HUD_CY + 5)

// One band of the fan. The panel has no arc primitive and a 24 px glyph does not
// deserve one: a pixel belongs to the band when its distance from the apex sits
// between the two radii and its direction is inside the ±45° wedge, which is
// exactly the fan a phone draws.
static void hudWifiBand(int rInner, int rOuter, uint16_t color) {
  int inner2 = rInner * rInner;
  int outer2 = rOuter * rOuter;
  for (int dy = 0; dy <= rOuter; dy++) {
    for (int dx = -dy; dx <= dy; dx++) {
      int d2 = dx * dx + dy * dy;
      if (d2 < inner2 || d2 > outer2) continue;
      gfxPixel(HUD_CX + dx, HUD_WIFI_APEX_Y - dy, color);
    }
  }
}

// The muted fan plus a hot slash. The fan is the owner's colour at half light so
// it never outshines the face two finger-widths away, and the slash pulses slowly
// because a steady alarm reads as a hung device while a pulse reads as something
// to fix. The slash is drawn over a black gap: without it the fan looks crossed
// out instead of cut, which is the phone's way of saying "no connection".
static void hudLinkDown(uint32_t nowMs) {
  const int apex = HUD_WIFI_APEX_Y;
  uint16_t fan = gfxHex(buddyEyeScale(settingsGetBuddyEyeColor(), 50));
  hudWifiBand(4, 6, fan);
  hudWifiBand(9, 11, fan);
  hudWifiBand(14, 16, fan);
  gfxFillCircle(HUD_CX, apex, 2, fan);
  const int x0 = HUD_CX - 11, y0 = apex - 3;
  const int x1 = HUD_CX + 11, y1 = apex - 17;
  for (int i = -2; i <= 2; i++) gfxLine(x0, y0 + i, x1, y1 + i, GFX_BLACK);
  float pulse = 0.55f + 0.45f * (0.5f + 0.5f * cosf((float)nowMs * 0.0037f));
  uint16_t hot =
      gfxHex(buddyEyeScale(BUDDY_EYE_HOT_COLOR, (int)(pulse * 100.0f)));
  gfxLine(x0, y0, x1, y1, hot);
  gfxLine(x0, y0 + 1, x1, y1 + 1, hot);
}

// The HUD slot: one corner, one meaning. A dead link owns it, because a phase
// animation describes a bridge the device cannot reach; otherwise it is the
// phase. The step is skipped rather than reset while offline, so the animation
// resumes where it was when the link comes back.
static void drawHud(BuddyPhase phase, BuddyRgb led, bool linkDown,
                    uint32_t nowMs) {
  if (linkDown) {
    hudLinkDown(nowMs);
    return;
  }
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

// Concentración, drawn on the band under the eyes. Kept out of the pose on
// purpose: the number is the clock, not an expression, so no mood the bridge or
// the web can push may change it, and a phase change cannot blank it.
static void drawFocusBand(uint32_t nowMs, uint32_t eyeRgb) {
  const BuddyFocus &f = buddyFocusShared();
  const bool done = buddyFocusDoneVisible(&f, nowMs);
  if (!f.active && !done) return;

  char label[BUDDY_FOCUS_LABEL_MAX];
  uint16_t ink = GFX_WHITE;
  uint16_t fill = gfxHex(eyeRgb);
  if (done) {
    // The achievement: one word, in the glass's gold, on the same band.
    snprintf(label, sizeof(label), "LISTO");
    ink = gfxHex(BUDDY_FOCUS_GOLD);
    fill = ink;
  } else {
    buddyFocusFormat(buddyFocusRemainingMs(&f, nowMs), label, sizeof(label));
  }
  gfxTextCentered(BUDDY_FOCUS_BAND_Y, BUDDY_FOCUS_BAND_SCALE, label, ink);

  // A progress mark under the number: the outline wears the eye colour at low
  // energy so it belongs to the face, and the study fills it as time passes.
  const int barX = (320 - BUDDY_FOCUS_BAR_W) / 2;
  gfxRect(barX, BUDDY_FOCUS_BAR_Y, BUDDY_FOCUS_BAR_W, BUDDY_FOCUS_BAR_H,
          gfxHex(buddyEyeScale(eyeRgb, 45)));
  const int inner = BUDDY_FOCUS_BAR_W - 2;
  const int filled = done ? inner
                          : inner * (int)buddyFocusProgress(&f, nowMs) / 255;
  if (filled > 0) {
    gfxFillRect(barX + 1, BUDDY_FOCUS_BAR_Y + 1, filled,
                BUDDY_FOCUS_BAR_H - 2, fill);
  }
}

static void drawFace(BuddyState state, const BuddyPose &pose, uint32_t nowMs,
                     bool poke, bool ballVisible) {
  gfxClear(GFX_BLACK);

  const int cx[2] = {105, 215};
  const int cy = 88;
  bool happy = pose.happy || poke;
  // The owner's colour is read once per frame (the getter is cached in RAM).
  // Offline dims it; anger and dizziness blend it towards hot red on top, and
  // the owner's choice comes back as soon as the reaction expires.
  uint32_t owner = settingsGetBuddyEyeColor();
  uint32_t eyeRgb = pose.dim ? buddyEyeDim(owner) : buddyEyeHot(owner, pose.hot);
  // The face's energy: it scales the eye colour before the three glow shades are
  // derived from it, so the eye and its halos fade together. Energy 1 is exactly
  // the shipped colour (the helper returns it untouched at 100%), and 0 is a
  // black panel, which is the first frame of the boot wake.
  int energy = buddyGlowPercent(pose.glow);
  if (energy < 100) eyeRgb = buddyEyeScale(eyeRgb, energy);
  uint16_t color = gfxHex(eyeRgb);
  uint16_t glowInner = gfxHex(buddyEyeScale(eyeRgb, BUDDY_GLOW_SHADE_INNER));
  int bob = (int)pose.bob;
  // While a session runs, the eyes drop towards the number once every cycle: a
  // glance at the work. Added at draw time, after every pose source, so it can
  // never fight the bridge, the game overlay or the ball for the gaze channel.
  const float dip = buddyFocusGazeDip(nowMs, buddyFocusShared().active);
  const int gazeY = (int)pose.gazeY + (int)(dip + 0.5f);

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
  // A happy eye is never a blink bar: the joy is the squircle plus its lower
  // lid, and a random idle blink landing inside the poke must not erase it.
  int baseRy = buddyEyeHalfH((float)pose.ry, grow, pose.open, shut && !happy);
  // A turned head, faked with two flat eyes: the eye on the side the head turns
  // away from gets narrower (a rotating sphere foreshortens) and the near eye
  // gets wider, so the pair visibly disagrees in size. `tilt` then rolls both
  // eyes together. Positive turn = head goes to the owner's right.
  float turn = pose.turn;
  if (turn > 1.0f) turn = 1.0f;
  if (turn < -1.0f) turn = -1.0f;
  const float BUDDY_TURN_SCALE = 0.22f;
  BuddyPerspective per = buddyPerspective(pose.gazeX);
  for (int i = 0; i < 2; i++) {
    float widen = i == 0 ? per.widenL : per.widenR;
    float lead = i == 0 ? per.pushL : per.pushR;
    // i == 0 is the left eye on screen. Turning right grows the right eye.
    float turnScale = (i == 0) ? (1.0f - BUDDY_TURN_SCALE * turn)
                              : (1.0f + BUDDY_TURN_SCALE * turn);
    eyeRx[i] = (int)lroundf((float)baseRx * widen * turnScale);
    // The whole pair also slides a couple of pixels the way the head turns.
    float turnX = 5.0f * turn;
    eyeX[i] = cx[i] + (int)(pose.gazeX + lead + turnX +
                            (i == 0 ? pose.eyeGazeL : pose.eyeGazeR));
    eyeDeg[i] = pose.rot + pose.tilt + (i == 0 ? pose.rotL : pose.rotR);
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
  s_draw.open = pose.open;
  s_draw.glow = pose.glow;
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
    int y = cy + bob + gazeY;
    if (happy) {
      // Joy comes from the lower eyelid, never from a brow or an extra line
      // above/below the eye: the squircle keeps its top shape and the bottom
      // lid rises into a squint. Same band machinery as every other expression.
      const float happyBottom = 0.3f;
      float deg = eyeDeg[i];
      drawEyeGlow(x, y, eyeRx[i], baseRy, deg, eyeRgb);
      gfxFillSquircleRotAA(x, y, eyeRx[i], baseRy, deg, color, glowInner);
      int bandHalfX = eyeExtentX[i] + BUDDY_GLOW_PAD + 2;
      BuddyLidBand band =
          buddyLidBandBottom(y, eyeExtentY[i], BUDDY_GLOW_PAD, happyBottom);
      if (band.h > 0) {
        gfxFillRect(x - bandHalfX, band.y0, bandHalfX * 2, band.h, GFX_BLACK);
        float line = buddyLidLineBottom(y, eyeExtentY[i], happyBottom);
        ditherLidRow(x, y, eyeRx[i], baseRy, deg, bandHalfX, band.y0, color,
                     1.0f - (line - floorf(line)));
      }
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
  if (ballVisible) drawBall(s_ball);
  // Last, so the bug, the ball and the sleeping letters cannot cover the number.
  drawFocusBand(nowMs, eyeRgb);
}

// One sensor sample into the reaction machine. The live path and the debug pin
// share it, so a pinned face still knows whether the board is lying down.
static BuddyImuPose readImu(uint32_t nowMs, bool calmReactions) {
  float accel[3], gyro[3];
  imuReadAll(accel, gyro);
  // The face reads the board, not the glass: when the image is the other one of
  // the pair she has to lean the other way to keep leaning downhill to the owner.
  rotMirrorInPlane(accel, gyro, rotationInPlaneSign());
  // The ball keeps the raw axes: its gravity is mapped through the steering
  // calibration, and the wizard captured its baselines on those same raw axes.
  for (int i = 0; i < 3; i++) s_accelRaw[i] = accel[i];
  uint32_t dt = s_imuAt ? (nowMs - s_imuAt) : BUDDY_FRAME_MS;
  s_imuAt = nowMs;
  buddyImuFeed(&s_imu, accel, gyro, nowMs, dt);
  // While the owner is bouncing the ball, shaking the glass is play: no dizzy
  // face, no anger, and no burst left over for the next real shake.
  if (calmReactions) buddyImuCalm(&s_imu);
  return buddyImuPose(&s_imu, nowMs);
}

// Turns the raw sample into the ball's world using the same steering
// calibration the meteor game uses: screen x is the calibrated lateral reading
// (positive = right) and screen y the calibrated fore/aft one (positive = down).
// A slow filter of those channels is the steady tilt that acts as gravity; the
// part above the filter is the shake that kicks the ball. Before the owner ever
// calibrates, the raw axes are the fallback so the toy still works.
static void ballInputs(float *gravityX, float *gravityY, float *shakeX,
                       float *shakeY) {
  SteerCalibration cal = settingsGetSteerCalibration();
  float mirror = (float)rotationInPlaneSign();
  float lat, fwd;
  if (cal.valid && cal.latAxis < 3 && cal.upAxis < 3 &&
      cal.latAxis != cal.upAxis) {
    uint8_t fwdAxis = (uint8_t)(3 - cal.latAxis - cal.upAxis);
    lat = (s_accelRaw[cal.latAxis] - cal.baseline) * (float)cal.sign * mirror;
    fwd = (s_accelRaw[fwdAxis] - cal.baselineY) * (float)(-cal.signY) * mirror;
  } else {
    lat = -s_accelRaw[0];
    fwd = s_accelRaw[1];
  }
  if (!s_ballPrimed) {
    s_ballTiltLat = lat;
    s_ballTiltFwd = fwd;
    s_ballPrimed = true;
  } else {
    // ~0.5 s of lag: a tilt becomes gravity, a flick stays a shake.
    s_ballTiltLat = 0.9f * s_ballTiltLat + 0.1f * lat;
    s_ballTiltFwd = 0.9f * s_ballTiltFwd + 0.1f * fwd;
  }
  *gravityX = s_ballTiltLat;
  *gravityY = s_ballTiltFwd;
  *shakeX = lat - s_ballTiltLat;
  *shakeY = fwd - s_ballTiltFwd;
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
      BuddyImuPose ip = readImu(nowMs, false);
      // The pin is not part of the live pose, but it must not freeze the clock
      // either: with `buddyface` up for twenty seconds the board would keep
      // counting silence through a shake, and the console would report a sleep
      // the live path had already broken.
      if (ip.wake) buddySleepStimulus(&s_sleep, nowMs);
      BuddyPose pose =
          buddyPose(s_pinState, s_pinAge + (nowMs - s_pinStart), nowMs, s_seed);
      if (ip.wake && pose.closed) buddyWakeEyes(&pose);
      s_bug.active = false;
      s_zzz = pose.closed && ip.lying && ip.lyingMs > 6000;
      s_mood = buddyMoodFrom(pose.closed, ip.angry, pose.tremble);
      drawFace(s_pinState, pose, nowMs, false, false);
      s_lastDraw = nowMs;
      s_dirty = false;
      return true;
    }
  }

  BuddyView view;
  buddyGetView(&view);

  // The web game's overlay, read once: it decides whether the badge is allowed
  // to cover the eyes and, further down, which mood the pose carries.
  BuddyGameMood gameMood = buddyGameCurrent(&s_game, nowMs);
  bool gameFace = gameMood != BUDDY_GAME_NONE;

  // No bridge to read: the corner carries the alarm and the eyes stay. The face
  // keeps the dim, calm offline pose, and the mark lives in the HUD slot instead
  // of taking the plate. While the game is playing the mark steps aside: the
  // owner asked for a face during the game, and the link's absence is visible the
  // moment it ends. Arriving or leaving is news by itself, so the frame redraws
  // once more than the animation alone would.
  bool linkDown = buddyShowsLinkBadge(view.link) && !gameFace;
  if (linkDown != s_badge) {
    s_badge = linkDown;
    s_dirty = true;
  }

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

  // The physical layer comes first: the IMU is local, and a shake has to count as
  // a stimulus in the same frame it happens instead of waiting for the next one.
  // That is the fix for "lo muevo y se despierta, lo dejo y se vuelve a dormir al
  // toque": the server no longer has a vote on when the body is tired.
  BuddyImuPose ip = readImu(nowMs, s_ballOn);
  if (ip.wake) buddySleepStimulus(&s_sleep, nowMs);

  // Sleeping is patience, and patience is local: MIN(the bridge's silence, the
  // board's silence) from buddy_sleep.h. Only a state that is not asking for
  // attention may doze: `thinking` or `waiting` is the server working, and the
  // ball is the owner playing, so neither is a nap.
  BuddySleepStage sleepStage = buddySleepClockStage(
      &s_sleep, nowMs, view.sleepHintSec, view.sleepHintValid);
  bool ballGame = s_ballOn && view.phase == BUDDY_PHASE_IDLE &&
                  shown == BUDDY_IDLE;
  bool canSleep =
      view.online && !ballGame && (shown == BUDDY_IDLE || shown == BUDDY_DONE);
  bool asleepNow = canSleep && sleepStage != BUDDY_SLEEP_AWAKE;

  // `shown` stays the bridge's state (the HUD and the bead follow it); the sleep
  // ladder is a pose, not a state the server ever names.
  BuddyState poseState = asleepNow ? BUDDY_ASLEEP : shown;
  uint32_t poseAge =
      asleepNow ? buddySleepClockIdleMs(&s_sleep, nowMs) : shownAge;
  BuddyPose pose = buddyPose(poseState, poseAge, nowMs, s_seed);
  if (shown == BUDDY_TOOL && (int32_t)(s_surpriseUntil - nowMs) > 0) {
    pose.surprise = true;
    pose.rx = 50;
    pose.ry = 60;
    pose.lid = 0.0f;
  }

  // `done` used to be one animation for every arrival. The direction of the
  // change is the emotion: a tool that finished is a win (a bigger, faster
  // bounce), a thought that finished is relief (a calm landing with heavy lids).
  // Both come from the `prev` field the bridge started sending in `state` events.
  if (shown == BUDDY_DONE) {
    if ((int32_t)(s_doneWinUntil - nowMs) > 0) {
      pose.happy = true;
      pose.bob = sinf((float)nowMs * 0.012f) * 3.0f;
    } else if ((int32_t)(s_doneReliefUntil - nowMs) > 0) {
      pose.happy = true;
      pose.lid = 0.3f;
      pose.bob = sinf((float)nowMs * 0.0016f) * 1.0f;
    }
  }

  // The button's "te escuche": one lid drop, centred. Without it a press feels
  // dead for as long as it takes the run to report its first `thinking`, and that
  // second is exactly what reads as a broken button.
  if ((int32_t)(s_listenUntil - nowMs) > 0) {
    pose.lid = 0.35f;
    pose.gazeX = 0.0f;
    pose.gazeY = 0.0f;
    pose.blink = false;
    pose.closed = false;
    pose.happy = false;
  }

  // Physical layer: the IMU is local and the bridge never sees it. A missing
  // sensor reads zero, which the feed ignores, and every reaction expires, so
  // the face can always come back to the state pose.
  // The toy owns the shake channel whenever it is toggled on: bouncing it is
  // play, never a dizzy spell, even while the ball waits out a busy state. The
  // sample was already read at the top of the frame.
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
  // Waking from the local sleep: seed the lid shut once so the opening travels
  // through the ease instead of popping, then open. This fires on the frame the
  // clock says awake, which a shake produces immediately because the stimulus
  // above already reset the silence. The shape of the wake depends on what woke
  // it: a shake with nothing behind it is a yawn, being spoken to is a startle,
  // and an approval is a straight look at the owner.
  if (asleepNow) {
    s_asleepDrawn = true;
    s_waking = false;
  } else if (s_asleepDrawn) {
    s_asleepDrawn = false;
    s_waking = true;
    buddyPoseFilterSeedLid(&s_filter, 1.0f);
    BuddyWakeReaction wake = buddyWakeReaction(BUDDY_IDLE, shown);
    buddyWakeEyes(&pose);
    if (wake == BUDDY_WAKE_YAWN) {
      // Nothing happened: the eyes open heavy and it takes a breath to focus.
      pose.surprise = false;
      pose.rx = 0;
      pose.ry = 0;
      pose.lid = 0.45f;
    } else if (wake == BUDDY_WAKE_ATTEND) {
      // Somebody is waiting on an answer: straight to the owner, no startle.
      pose.surprise = false;
      pose.rx = 0;
      pose.ry = 40;
      pose.gazeX = 0.0f;
      pose.gazeY = 0.0f;
    }
  }
  if (poke) pose.happy = true;

  // Distractions: only while the face is genuinely idle and Sofía is doing
  // nothing. The moment she is spoken to (any other state, phase or a tap) the
  // event is cancelled for good; the eyes follow the bug otherwise.
  // The idle events are a family now (bug, wink, yawn, peek, shiver, look-up,
  // doze), all scheduled off the same long, mostly-empty clock. Anything that is
  // not the bug is painted straight onto the pose through its eyes.
  BuddyIdleEvent idle = buddyIdleEventAt(nowMs, s_seed);
  BuddyBug bug = buddyBugAt(nowMs, s_seed);
  // Console override: `bug <seconds>` puts the distraction on the glass now, so
  // the probe can prove it without waiting for the hashed window.
  if ((int32_t)(s_bugForcedUntil - nowMs) > 0) {
    bug.active = true;
    bug.eventId = 0xFFFFFFFEu;
    bug.x = 40.0f + (float)((nowMs / 20u) % 240u);
    bug.y = 70.0f;
  }
  // "Idle" here means awake and not being talked to: the idle face is exactly
  // when the owner expects her to find something to play with. A face on the
  // sleep ladder, or one whose eyes are already shut, sleeps through it.
  bool awakeIdle = shown == BUDDY_IDLE && !asleepNow && !pose.closed;
  bool ballVisible = ballGame && !pose.closed;
  bool quiet =
      awakeIdle && view.phase == BUDDY_PHASE_IDLE && !poke && !ballVisible;
  if (!quiet && idle.active) s_bugCancelled = idle.eventId;
  if (idle.active && idle.eventId == s_bugCancelled) idle.active = false;
  if (bug.active && bug.eventId == s_bugCancelled) bug.active = false;
  if (idle.active && idle.kind != BUDDY_IDLE_BUG) {
    buddyIdleApply(idle, nowMs, &pose);
  }
  if (bug.active) {
    float gx = 0, gy = 0;
    buddyBugGaze(bug, &gx, &gy);
    pose.gazeX = gx;
    pose.gazeY = gy;
  }
  s_bug = bug;

  // The toy: integrated while it is on the glass, and the eyes follow it. The
  // dynamic lateral acceleration (raw minus the slow tilt baseline) is the
  // shake; a board merely held at an angle does not drag it anywhere.
  if (ballVisible) {
    if (s_ballAt) {
      uint32_t dt = nowMs - s_ballAt;
      if (dt) {
        float gx, gy, sx, sy;
        ballInputs(&gx, &gy, &sx, &sy);
        buddyBallStep(&s_ball, gx, gy, sx, sy, dt);
      }
    }
    s_ballAt = nowMs;
    float bgx = 0, bgy = 0;
    buddyBallGaze(s_ball, &bgx, &bgy);
    pose.gazeX = bgx;
    pose.gazeY = bgy;
  } else {
    s_ballAt = 0;
    s_ballPrimed = false;
  }

  // The web game's overlay wins while it lasts: the browser cannot describe a
  // pose, it names a mood, and buddy_game.h owns what that mood means.
  if (gameMood != s_gameDrawn) {
    s_gameDrawn = gameMood;
    s_dirty = true;
  }
  if (gameMood != BUDDY_GAME_NONE) buddyGameApplyPose(&pose, gameMood);

  // The sleeping letters need both halves: eyes shut (deep sleep) and the board
  // lying on the table for a while.
  s_zzz = pose.closed && ip.lying && ip.lyingMs > 6000;
  s_mood = buddyMoodFrom(pose.closed, ip.angry, pose.tremble);
  // The rear bead follows the game too: red when she wins, a faint ember for a
  // draw. Everything else leaves the IMU's verdict alone.
  if (gameMood == BUDDY_GAME_ANGER) s_mood = BUDDY_MOOD_ANGRY;
  else if (gameMood == BUDDY_GAME_SLEEPY) s_mood = BUDDY_MOOD_SLEEPING;

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

  drawFace(shown, eased, nowMs, poke, ballVisible);
  if (!linkDown) hudTrack(view.phase, nowMs);
  drawHud(view.phase,
          view.ledValid ? view.led : buddyPhaseDefaultLed(view.phase), linkDown,
          nowMs);
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

void buddyGameFaceMood(const char *name, uint32_t holdMs) {
  BuddyGameMood mood = buddyGameMoodFromName(name, BUDDY_GAME_NONE);
  if (mood == BUDDY_GAME_NONE) {
    buddyGameClear(&s_game);
  } else {
    buddyGameSet(&s_game, mood, holdMs, millis());
  }
  s_dirty = true;
}

// --- the boot wake -----------------------------------------------------------
//
// The first frame of a configured board is not a splash but Sofía waking up: the
// ladder in buddy_wake.h owns the glass for its whole length, and `screensTick`
// suspends every screen while it runs so nothing can draw over it. It is an
// overlay of its own and not a state, because the bridge has no vote on how the
// face comes up and the owner asked for something that happens once.
static bool s_wakeOn = false;
static uint32_t s_wakeStart = 0;
static uint32_t s_wakeDraw = 0;

// Whether this board has a bridge to wake up for. An unconfigured board keeps the
// text splash, because that splash names the access point its owner has to join;
// a configured one has nothing to say and can just wake up.
bool buddyConfigured() {
  char url[SETTINGS_BUDDY_URL_MAX + 1];
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  settingsGetBuddyUrl(url, sizeof(url));
  settingsGetBuddyToken(token, sizeof(token));
  return url[0] && token[0];
}

void buddyWakeBegin(uint32_t nowMs) {
  s_wakeOn = true;
  s_wakeStart = nowMs;
  // Backdated by one frame so the very first tick draws instead of waiting 50 ms
  // on a panel that is still black.
  s_wakeDraw = nowMs - BUDDY_FRAME_MS;
  s_dirty = true;
}

bool buddyWakeActive() { return s_wakeOn; }

// The owner asked to skip it (a tap or a hold). Returns whether there was
// anything to skip, so the button handler knows if it already used the gesture.
bool buddyWakeCancel() {
  if (!s_wakeOn) return false;
  s_wakeOn = false;
  s_dirty = true;
  return true;
}

bool buddyWakeTick(uint32_t nowMs) {
  if (!s_wakeOn) return false;
  uint32_t t = nowMs - s_wakeStart;
  if (buddyWakeDone(t)) {
    s_wakeOn = false;
    // A repaint marker: the screen that takes the glass back must draw at once
    // instead of waiting for its own next tick to notice it was covered.
    s_dirty = true;
    return false;
  }
  if ((uint32_t)(nowMs - s_wakeDraw) < BUDDY_FRAME_MS) return false;
  s_wakeDraw = nowMs;
  // The wake is not a bridge state, so it starts from a default pose and only
  // its own seven channels move: no bridge, no IMU, no ball and no HUD.
  BuddyPose pose;
  buddyWakePose(t, &pose);
  // The tremble is noise, not intent, so it goes on after the ladder exactly as
  // the live path does it: same salts and same amplitudes, so the shake reads as
  // the face's own shake and not as a second, softer one.
  if (pose.tremble > 0.01f) {
    pose.gazeX += buddyTremble(nowMs, s_seed, 11u, 1.6f);
    pose.gazeY += buddyTremble(nowMs, s_seed, 23u, 1.0f);
    pose.rot += buddyTremble(nowMs, s_seed, 37u, 2.2f);
  }
  s_mood = buddyMoodFrom(false, false, pose.tremble);
  drawFace(BUDDY_IDLE, pose, nowMs, false, false);
  s_lastDraw = nowMs;
  s_dirty = false;
  return true;
}

void buddyDumpDraw() {
  if (!s_draw.have) {
    Serial.println("buddydraw: todavia no dibujo");
    return;
  }
  static const char *kMoods[] = {"calm", "angry", "sleeping"};
  Serial.printf("draw mood=%s grow=%.2f open=%.2f glow=%.2f lid=%.2f hot=%.2f push=%d\n",
                kMoods[s_mood & 3], s_draw.grow, s_draw.open, s_draw.glow,
                s_draw.lid, s_draw.hot, s_draw.push);
  Serial.printf("ball on=%d active=%d x=%.1f y=%.1f vx=%.1f vy=%.1f\n",
                s_ballOn, s_ball.active, s_ball.x, s_ball.y, s_ball.vx,
                s_ball.vy);
  for (int i = 0; i < 2; i++) {
    Serial.printf("eye%d x=%d rx=%d ry=%d extY=%d deg=%.1f\n", i, s_draw.x[i],
                  s_draw.rx[i], s_draw.ry[i], s_draw.extentY[i],
                  s_draw.deg[i]);
  }
}

void buddyDumpSleep() {
  BuddyView view;
  buddyGetView(&view);
  uint32_t now = millis();
  BuddySleepStage stage = buddySleepClockStage(&s_sleep, now, view.sleepHintSec,
                                              view.sleepHintValid);
  static const char *kStage[] = {"awake", "light", "deep"};
  Serial.printf(
      "sleep stage=%s idle=%lums hint=%s%.1fs cursor=%d asleep=%d\n",
      kStage[stage & 3], (unsigned long)buddySleepClockIdleMs(&s_sleep, now),
      view.sleepHintValid ? "" : "none/", (double)view.sleepHintSec, view.evSeq,
      s_asleepDrawn ? 1 : 0);
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

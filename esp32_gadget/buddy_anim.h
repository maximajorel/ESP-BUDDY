// Face poses for Sofía's buddy, pure and header-only.
//
// The bridge reports a state and `since`; this module turns that into what the
// eyes should look like right now. It is deliberately free of Arduino so
// `tools/buddy_anim_test.cpp` can pin the ladders (bored, long thinking, tool
// surprise) and the anti-flicker hold without a board.
//
// The design follows Sofía's brief: the server names the state, the firmware
// owns duration and emotion. The eyes are solid, so the language is gaze,
// eyelid, shape and blink rhythm.
#pragma once

#include <math.h>
#include <stdint.h>

#include "buddy_state.h"

// A state that lasts less than this is not drawn: the bridge already smooths
// tool/thinking, and this keeps a one-poll flicker from flashing the face.
#define BUDDY_MIN_STATE_MS 300

struct BuddyPose {
  float gazeX = 0;   // whole-eye offset in px
  float gazeY = 0;
  float eyeGazeL = 0;  // extra per-eye offset, used for the cross-eyed error
  float eyeGazeR = 0;
  int rx = 48;
  int ry = 48;
  float lid = 0;       // 0 open .. 1 covered from the top by a black band
  float lidL = 0;      // extra top lid for the left eye (asymmetric squint)
  float lidR = 0;      // extra top lid for the right eye
  float bottom = 0;    // 0 open .. 1 cut from the bottom (tired, EMO-style)
  float rot = 0;       // degrees for both eyes, positive = clockwise
  float rotL = 0;      // extra rotation for the left eye (anger, dizziness)
  float rotR = 0;      // extra rotation for the right eye
  float hot = 0;       // 0 = owner colour .. 1 = hot red (anger, dizziness)
  float bob = 0;       // vertical offset in px: breathing and bouncing
  // Expression channels added by the life pass. All default to the previous
  // look, so every existing expression is unchanged at zero. There are no
  // brows on purpose: the only upper feature of this face is the black eyelid.
  float grow = 1.0f;   // eye scale: anger makes the eye bigger
  float tremble = 0;   // 0..1 shake the renderer adds after the easing
  // The two channels the boot wake needs and no expression did: `open` is how
  // far the eye is open vertically (0 is the 4 px blink bar, 1 the full
  // squircle) and `glow` is the face's energy, which scales the eye colour and
  // its three halos together (0 is a black panel). Both default to 1 so every
  // existing expression draws exactly the frame it drew before them.
  float open = 1.0f;   // vertical aperture: 0 = the 4 px blink bar, 1 = full
  float glow = 1.0f;   // energy: scales the eye colour and its halos, 0 = black
  // There is no mouth and no brow on this face: everything is the two eyes.
  // A head turn is faked the only way two flat eyes can fake it: the eye on the
  // far side shrinks and the near eye grows, exactly like a real head turning
  // away from the camera. `turn` is -1 (looking far left) .. +1 (far right);
  // the renderer spreads it into per-eye scale and a small pair shift.
  float turn = 0;
  // A whole-pair roll in degrees (positive = clockwise): the head tilting.
  float tilt = 0;
  bool blink = false;
  bool happy = false;
  bool closed = false;
  bool surprise = false;
  bool dim = false;
};

static inline uint32_t buddyHash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// Deterministic 0..1 noise: the same seed and salt give the same "random"
// blink or saccade on every frame, so the face is stable between draws.
static inline float buddyUnit(uint32_t seed, uint32_t salt) {
  return (buddyHash32(seed * 0x9e3779b9u + salt) & 0xFFFFFFu) / 16777216.0f;
}

// Which state the glass should show. A brand new state younger than the hold
// window is deferred: if it disappears on the next poll it never flashed.
static inline BuddyState buddyShownState(BuddyState shown, BuddyState raw,
                                         uint32_t rawAgeMs) {
  if (raw == shown) return shown;
  if (rawAgeMs < BUDDY_MIN_STATE_MS) return shown;
  return raw;
}

// --- idle life ---------------------------------------------------------------
//
// The idle face is not a still picture: it holds a fixation, jumps 1-3 px to a
// new one, breathes, blinks on a rhythm that is not a timer, and every so often
// looks to a side. Everything is a deterministic hash of the clock, so the face
// is reproducible frame to frame and a test can pin the rhythm without a board.

// A saccade lands on a 3.6 s slot whose offset is hashed, so consecutive jumps
// are 2.0-5.0 s apart instead of a metronome beat.
#define BUDDY_SACCADE_TICK_MS 200
#define BUDDY_SACCADE_EPOCH_TICKS 18  // 3.6 s
#define BUDDY_SACCADE_SPREAD_TICKS 8  // the jump lands in the first 1.4 s
#define BUDDY_SACCADE_JUMP_MS 60      // how long the eye takes to arrive
#define BUDDY_SACCADE_MAX_PX 3.0f

struct BuddySaccade {
  float fromX = 0, fromY = 0;  // the previous fixation
  float toX = 0, toY = 0;      // where this jump landed
  float x = 0, y = 0;          // what to draw right now
  uint32_t startMs = 0;        // when this jump began
};

static inline uint32_t buddySaccadeTick(uint32_t epoch, uint32_t seed) {
  return epoch * BUDDY_SACCADE_EPOCH_TICKS +
         (buddyHash32(seed ^ (epoch * 0x9E3779B9u)) %
          BUDDY_SACCADE_SPREAD_TICKS);
}

// Landing point: 1-3 px horizontally, up to 2 px vertically, both signs.
static inline void buddySaccadeTarget(uint32_t epoch, uint32_t seed, float *x,
                                      float *y) {
  uint32_t h = buddyHash32(seed ^ (epoch * 0x85EBCA6Bu));
  float mx = 1.0f + (float)(h % 3u);
  float my = (float)((h >> 4) % 3u);
  *x = ((h >> 8) & 1u) ? mx : -mx;
  *y = ((h >> 9) & 1u) ? my : -my;
}

// The fixation in progress. The first 60 ms are the jump itself, eased so a
// 1 px saccade does not alias into a pop; after that the eye just holds.
static inline BuddySaccade buddySaccadeAt(uint32_t nowMs, uint32_t seed) {
  BuddySaccade s;
  uint32_t tick = nowMs / BUDDY_SACCADE_TICK_MS;
  uint32_t epoch = tick / BUDDY_SACCADE_EPOCH_TICKS;
  bool haveCur = false, havePrev = false;
  uint32_t curEpoch = 0, prevEpoch = 0;
  for (uint32_t i = 0; i < 6; i++) {
    if (epoch < i) break;
    uint32_t e = epoch - i;
    uint32_t fired = buddySaccadeTick(e, seed);
    if (fired > tick) continue;
    if (!haveCur) {
      haveCur = true;
      curEpoch = e;
    } else {
      havePrev = true;
      prevEpoch = e;
      break;
    }
  }
  if (!haveCur) return s;
  buddySaccadeTarget(curEpoch, seed, &s.toX, &s.toY);
  if (havePrev) buddySaccadeTarget(prevEpoch, seed, &s.fromX, &s.fromY);
  s.startMs = buddySaccadeTick(curEpoch, seed) * BUDDY_SACCADE_TICK_MS;
  float t = (float)(nowMs - s.startMs) / (float)BUDDY_SACCADE_JUMP_MS;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float e = t * t * (3.0f - 2.0f * t);
  s.x = s.fromX + (s.toX - s.fromX) * e;
  s.y = s.fromY + (s.toY - s.fromY) * e;
  return s;
}

// A side glance: one per 12 s slot, 1.4-1.8 s long, 10-18 px out and back. The
// reach is what lets the renderer turn the pair instead of sliding it.
#define BUDDY_GLANCE_TICK_MS 250
#define BUDDY_GLANCE_EPOCH_TICKS 48  // one look per 12 s
#define BUDDY_GLANCE_SPREAD_TICKS 40
#define BUDDY_GLANCE_MS 1400
#define BUDDY_GLANCE_RISE_MS 140

struct BuddyGlance {
  float x = 0;
  bool active = false;
  uint32_t startMs = 0;
};

static inline uint32_t buddyGlanceTick(uint32_t epoch, uint32_t seed) {
  return epoch * BUDDY_GLANCE_EPOCH_TICKS +
         (buddyHash32(seed ^ (epoch * 0x27D4EB2Fu)) % BUDDY_GLANCE_SPREAD_TICKS);
}

static inline BuddyGlance buddyGlanceAt(uint32_t nowMs, uint32_t seed) {
  BuddyGlance g;
  uint32_t tick = nowMs / BUDDY_GLANCE_TICK_MS;
  uint32_t epoch = tick / BUDDY_GLANCE_EPOCH_TICKS;
  for (uint32_t i = 0; i < 3; i++) {
    if (epoch < i) break;
    uint32_t e = epoch - i;
    uint32_t fired = buddyGlanceTick(e, seed);
    if (fired > tick) continue;
    g.startMs = fired * BUDDY_GLANCE_TICK_MS;
    uint32_t h = buddyHash32(seed ^ (e * 0xC2B2AE35u));
    uint32_t hold = BUDDY_GLANCE_MS + (h % 400u);
    uint32_t in = nowMs - g.startMs;
    if (in >= hold) return g;
    float reach = 10.0f + (float)((h >> 4) % 9u);
    float dir = ((h >> 8) & 1u) ? 1.0f : -1.0f;
    float env = 1.0f;
    if (in < BUDDY_GLANCE_RISE_MS) {
      env = (float)in / (float)BUDDY_GLANCE_RISE_MS;
    } else if (hold - in < BUDDY_GLANCE_RISE_MS) {
      env = (float)(hold - in) / (float)BUDDY_GLANCE_RISE_MS;
    }
    if (env < 0.0f) env = 0.0f;
    g.x = reach * dir * env;
    g.active = true;
    return g;
  }
  return g;
}

// Blinking: a 4 s slot with a hashed offset, so consecutive blinks are 2.1-5.9 s
// apart; three blinks in ten also fire a companion 300 ms later (the burst),
// and one in five stays shut longer. A fixed timer is what gives an animation
// away; this is deliberately uneven.
#define BUDDY_BLINK_TICK_MS 100
#define BUDDY_BLINK_EPOCH_TICKS 40  // 4 s
#define BUDDY_BLINK_SPREAD_TICKS 20
#define BUDDY_BLINK_MS 130
#define BUDDY_BLINK_LONG_MS 260
#define BUDDY_BLINK_BURST_TICKS 3  // 300 ms after the first blink
#define BUDDY_BLINK_BURST_PERCENT 30u
#define BUDDY_BLINK_LONG_PERCENT 20u

static inline uint32_t buddyBlinkTick(uint32_t epoch, uint32_t seed) {
  return epoch * BUDDY_BLINK_EPOCH_TICKS +
         (buddyHash32(seed ^ (epoch * 0x9E3779B9u)) % BUDDY_BLINK_SPREAD_TICKS);
}

static inline bool buddyBlinkAt(uint32_t nowMs, uint32_t seed) {
  uint32_t tick = nowMs / BUDDY_BLINK_TICK_MS;
  uint32_t epoch = tick / BUDDY_BLINK_EPOCH_TICKS;
  uint32_t last = 0;
  bool have = false;
  for (uint32_t i = 0; i < 2; i++) {
    if (epoch < i) break;
    uint32_t e = epoch - i;
    uint32_t h = buddyHash32(seed ^ (e * 0x9E3779B9u));
    uint32_t base = buddyBlinkTick(e, seed);
    bool burst = ((h >> 8) % 100u) < BUDDY_BLINK_BURST_PERCENT;
    if (base <= tick && (!have || base > last)) {
      last = base;
      have = true;
    }
    uint32_t second = base + BUDDY_BLINK_BURST_TICKS;
    if (burst && second <= tick && (!have || second > last)) {
      last = second;
      have = true;
    }
  }
  if (!have) return false;
  uint32_t epochOfLast = last / BUDDY_BLINK_EPOCH_TICKS;
  uint32_t h = buddyHash32(seed ^ (epochOfLast * 0x51ED270Bu));
  uint32_t dur = ((h >> 4) % 100u) < BUDDY_BLINK_LONG_PERCENT
                     ? BUDDY_BLINK_LONG_MS
                     : BUDDY_BLINK_MS;
  return (nowMs - last * BUDDY_BLINK_TICK_MS) < dur;
}

// The breath: one slow sine, -1..1, 4.5 s per cycle. The eye line rides it, so
// the face keeps rising and falling even when nothing else happens.
#define BUDDY_BREATH_MS 4500.0f

static inline float buddyBreath(uint32_t nowMs) {
  return sinf((float)nowMs * (6.2831853f / BUDDY_BREATH_MS));
}

// --- idle distractions --------------------------------------------------------
//
// --- Idle events: the face finds its own business --------------------------
//
// A cat does not fidget on a schedule you can predict, and neither does this
// face. The clock is cut into long epochs, most epochs are empty on purpose (a
// face that twitches every 50 s is a screen saver, not a pet), and when one
// does land it picks a different bit of business each time. Everything is pure
// clock + seed: no state, so it is testable and it never repeats itself.
//
// None of these events ever takes over while Sofía is doing something. The
// renderer cancels the running event the moment the bridge speaks, because the
// owner talking to her always wins over a yawn.
enum BuddyIdleKind {
  BUDDY_IDLE_NONE = 0,
  BUDDY_IDLE_BUG,     // a bug crawls across the glass and the eyes follow it
  BUDDY_IDLE_WINK,    // one eye shuts, then the other
  BUDDY_IDLE_PEEK,    // something off to one side: the head turns and holds
  BUDDY_IDLE_YAWN,    // the lids sink most of the way and come back
  BUDDY_IDLE_SHIVER,  // a quick shake, like a dog shaking off water
  BUDDY_IDLE_LOOKUP,  // hears something above and scans the ceiling
  BUDDY_IDLE_DOZE,    // almost nods off, then snaps awake
  BUDDY_IDLE_SQUINT,  // something tiny on the glass: leans in, eyes narrow
  BUDDY_IDLE_BLISS,   // eyes shut, happy: the face of a cat in the sun
  BUDDY_IDLE_GREET,   // notices you're there: head comes round, eyes lag behind
  BUDDY_IDLE_SHEEPISH,// caught doing nothing: looks away, eyes disagree
  BUDDY_IDLE_PUFF,    // gathers itself: one long breath in, then settles
  BUDDY_IDLE_TRACK,   // follows something crossing in FRONT (depth, not sideways)
  BUDDY_IDLE_COUNT
};

#define BUDDY_IDLE_EPOCH_MS 180000u   // 3 min: at most one event per epoch
#define BUDDY_IDLE_SPREAD_MS 130000u  // it may start anywhere in the epoch
// Con trece tipos de evento la duracion media baja (varios son cortos), asi que
// con 45 la cara quedaba quieta mas del 99% del dia y el test la frena. Con 55
// vuelve a tener aire sin volverse un circo: el techo del test es 8%.
#define BUDDY_IDLE_CHANCE 55u         // and most epochs are completely empty

struct BuddyIdleEvent {
  bool active = false;
  uint8_t kind = BUDDY_IDLE_NONE;
  uint32_t eventId = 0;   // the epoch, so the renderer can cancel this one
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
  uint32_t hash = 0;
  float t = 0.0f;         // 0..1 through the event
};

// Each piece of business has its own honest length: a shiver is over in under a
// second, a bug crossing the glass takes its time.
static inline uint32_t buddyIdleDuration(uint8_t kind, uint32_t h) {
  switch (kind) {
    case BUDDY_IDLE_BUG:    return 7000u + (h >> 9) % 5000u;
    case BUDDY_IDLE_WINK:   return 2200u;
    case BUDDY_IDLE_PEEK:   return 3200u + (h >> 9) % 1500u;
    case BUDDY_IDLE_YAWN:   return 3400u;
    case BUDDY_IDLE_SHIVER: return 900u;
    case BUDDY_IDLE_LOOKUP: return 3600u;
    case BUDDY_IDLE_DOZE:   return 4200u;
    // Los seis nuevos, elegidos por lo que NO existe todavía y no por lo que
    // falta de una lista: entrecerrar, contento cerrado, desfase cabeza/ojos,
    // vergüenza asimétrica, respiración visible y profundidad.
    // Squint dura más que un blink a propósito: si dura lo mismo se lee como
    // pestañeo y no como "está mirando algo".
    case BUDDY_IDLE_SQUINT:   return 2600u;
    case BUDDY_IDLE_BLISS:    return 3000u;
    case BUDDY_IDLE_GREET:    return 3800u;
    case BUDDY_IDLE_SHEEPISH: return 2800u;
    case BUDDY_IDLE_PUFF:     return 3200u;
    case BUDDY_IDLE_TRACK:    return 4000u;
    default:                return 0u;
  }
}

static inline BuddyIdleEvent buddyIdleEventAt(uint32_t nowMs, uint32_t seed) {
  BuddyIdleEvent e;
  uint32_t epoch = nowMs / BUDDY_IDLE_EPOCH_MS;
  uint32_t base = epoch * BUDDY_IDLE_EPOCH_MS;
  uint32_t h = buddyHash32(seed ^ (epoch * 0xB5297A4Du));
  // Most epochs are quiet. This is the whole point of the change: the idle face
  // should be calm by default and surprising only now and then.
  if (h % 100u >= BUDDY_IDLE_CHANCE) return e;
  uint8_t kind = (uint8_t)(1u + ((h >> 3) % (uint32_t)(BUDDY_IDLE_COUNT - 1)));
  uint32_t duration = buddyIdleDuration(kind, h);
  if (duration == 0u) return e;
  uint32_t start = base + ((h >> 8) % BUDDY_IDLE_SPREAD_MS);
  if (start + duration > base + BUDDY_IDLE_EPOCH_MS) {
    // Keep the event inside its own epoch, so two events can never overlap and
    // the hash stays a pure function of the epoch.
    start = base + BUDDY_IDLE_EPOCH_MS - duration;
  }
  if ((int32_t)(nowMs - start) < 0) return e;
  uint32_t in = nowMs - start;
  if (in >= duration) return e;
  e.active = true;
  e.kind = kind;
  e.eventId = epoch;
  e.startMs = start;
  e.durationMs = duration;
  e.hash = h;
  e.t = (float)in / (float)duration;
  return e;
}

// Paint one idle event onto the pose. Only the eyes move: there is no mouth and
// no brow to work with, so every beat is expressed as lids, eye size and a head
// turn faked by the size difference between the two eyes.
static inline void buddyIdleApply(const BuddyIdleEvent &e, uint32_t nowMs,
                                  BuddyPose *p) {
  if (!e.active || p == nullptr) return;
  if (e.kind == BUDDY_IDLE_BUG) return;  // the renderer draws the bug itself
  const float PI_F = 3.14159265f;
  float t = e.t;
  float pulse = sinf(t * PI_F);  // 0 -> 1 -> 0: fades in and out, no pop
  float side = ((e.hash >> 24) & 1u) ? 1.0f : -1.0f;
  switch (e.kind) {
    case BUDDY_IDLE_WINK: {
      // One eye shuts all the way, opens, then the other one does the same.
      float phase = (t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f);
      float w = sinf(phase * PI_F);
      if (t < 0.5f) {
        p->lidR = w;  // right eye winks first
      } else {
        p->lidL = w;
      }
      p->tilt = 3.0f * side * pulse;
      break;
    }
    case BUDDY_IDLE_PEEK: {
      // Something moved off to the side: the head turns towards it, the eyes
      // lead, and the pair holds there long enough to be seen. `turn` is what
      // makes one eye grow and the other shrink, so the glass reads as a head
      // actually rotating rather than two dots sliding right.
      p->turn = 0.45f * side * pulse;
      p->gazeX = 20.0f * side * pulse;
      p->gazeY = -2.0f;
      p->tilt = -4.0f * side * pulse;
      p->bottom = 0.08f * pulse;  // a slightly hooded, curious eye
      break;
    }
    case BUDDY_IDLE_YAWN: {
      // There is no mouth to open, so a yawn is the lids sinking most of the way
      // and dragging back up, with the whole face tipping back.
      p->lid = 0.72f * pulse;
      p->bottom = 0.25f * pulse;
      p->ry = 52.0f - 14.0f * pulse;
      p->gazeY = -7.0f * pulse;
      p->tilt = 5.0f * side * pulse;
      p->bob = sinf((float)nowMs * 0.0022f) * 1.4f;
      break;
    }
    case BUDDY_IDLE_SHIVER: {
      // A fast shudder: the eyes rattle side to side and the lids pinch.
      p->tremble = 0.9f;
      p->turn = 0.12f * sinf(t * 6.2831853f * 5.0f);
      p->lid = 0.10f;
      break;
    }
    case BUDDY_IDLE_LOOKUP: {
      // Something on the ceiling: the eyes climb, the head tips back and turns
      // towards whatever it heard. Looking up is `gazeY` negative and a positive
      // tilt on the side it heard it from.
      p->gazeY = -14.0f * pulse;
      p->gazeX = 9.0f * side * pulse;
      p->turn = 0.25f * side * pulse;
      p->tilt = -7.0f * side * pulse;
      p->lid = 0.06f;
      break;
    }
    case BUDDY_IDLE_DOZE: {
      // Nods off, then snaps back. The fall is slow and the recovery is a
      // startle: the only honest joke a face with no mouth can play.
      float fall = t < 0.72f ? (t / 0.72f) : 1.0f;
      float snap = t >= 0.72f ? (t - 0.72f) / 0.28f : 0.0f;
      float shutness = fall * (1.0f - snap);
      p->lid = 0.90f * shutness;
      p->bottom = 0.35f * shutness;
      p->ry = 50.0f - 26.0f * shutness;
      p->gazeY = 4.0f * shutness;
      if (snap > 0.05f) {
        p->surprise = true;
        p->ry = 60.0f;
        p->lid = 0.0f;
        p->gazeY = -6.0f;
        p->tremble = 0.25f * (1.0f - snap);
      }
      break;
    }
    case BUDDY_IDLE_SQUINT: {
      // Algo chico en el vidrio: se acerca y entrecierra. Sólo párpado inferior
      // arriba, que es como se ve "afinar la vista": el superior bajaría a
      // párpado dormido y ahí la cara se lee aburrida, no concentrada.
      p->bottom = 0.42f * pulse;
      p->lid = 0.10f * pulse;
      p->grow = 1.0f + 0.05f * pulse;   // se inclina hacia adelante
      p->gazeY = 3.0f * pulse;
      p->turn = 0.10f * side * pulse;
      p->tremble = 0.05f * pulse;        // el micro-temblor de sostener el foco
      break;
    }
    case BUDDY_IDLE_BLISS: {
      // Contento sin sonrisa: los ojos se cierran y la cara se afloja hacia
      // arriba. El "gato al sol": cerrado pero contento, no dormido.
      p->lid = 0.88f * pulse;
      p->bottom = 0.20f * pulse;
      // `ry` son píxeles (48 = normal), no un factor: 40 achata sin achicar.
      p->ry = 48.0f - 8.0f * pulse;
      p->gazeY = -4.0f * pulse;
      p->tilt = 4.0f * side * pulse;
      p->bob = sinf((float)nowMs * 0.0016f) * 1.1f;
      break;
    }
    case BUDDY_IDLE_GREET: {
      // Alguien llegó: la cabeza gira al costado donde estaba y después vuelve
      // al centro MIENTRAS los ojos se quedan un toque más. El desfase es todo
      // el chiste: si cabeza y ojos vuelven juntos se lee como un tic, no como
      // "te vio".
      float turnOut = pulse;
      float eyesLag = sinf(t * PI_F * 0.75f);  // vuelve antes, sostenido
      p->turn = 0.22f * side * turnOut;
      p->gazeX = 11.0f * side * eyesLag;
      p->tilt = 5.0f * side * turnOut;
      p->lid = 0.05f;
      p->bottom = 0.10f * pulse;
      break;
    }
    case BUDDY_IDLE_SHEEPISH: {
      // Lo agarraste sin hacer nada: mira para otro lado, vuelve, y se vuelve a
      // ir. La clave es la ASIMETRÍA: los dos ojos no se van juntos (eso sería
      // mirar); uno lidera y el otro sigue.
      float away = pulse;
      p->turn = -0.26f * side * away;    // gira la cara hacia donde no estás
      p->gazeX = -14.0f * side * away;
      p->eyeGazeR = 3.5f * side * away;  // un ojo se escapa un poco más
      p->gazeY = 5.0f * away;            // mirada abajo: es la vergüenza
      p->lid = 0.18f * away;             // párpado bajo, no entrecerrado
      p->tilt = -3.0f * side * away;
      break;
    }
    case BUDDY_IDLE_PUFF: {
      // Un resoplido: no hay boca, así que se cuenta con el pecho. El ojo se
      // agranda al inspirar y se achica al soltar, y `bob` lleva el ritmo. La
      // curva es asimétrica a propósito: infla rápido y suelta lento. Un seno
      // simétrico se lee como latido, no como aire.
      float in = t < 0.30f ? (t / 0.30f) : 1.0f;
      float out = t >= 0.30f ? (t - 0.30f) / 0.70f : 0.0f;
      float air = in * (1.0f - out);
      p->ry = 48.0f + 7.0f * air;
      p->lid = 0.32f * air;
      p->bottom = 0.12f * air;
      p->grow = 1.0f + 0.035f * air;
      p->bob = 2.2f * air;
      p->gazeY = -2.0f * air;
      break;
    }
    case BUDDY_IDLE_TRACK: {
      // Algo cruza por DELANTE del vidrio, de cerca: horizontal de punta a
      // punta pero en curva (se acerca al ojo y se aleja), y con un retraso
      // entre mirada y cabeza. Lo que lo distingue de PEEK es que no se
      // detiene: lo que se mueve sin parar se lee como "algo pasó".
      float x = cosf(t * 2.0f * PI_F);        // +1 -> -1 -> +1
      float depth = sinf(t * PI_F);           // cuánto se acerca al vidrio
      p->gazeX = 24.0f * x * (0.55f + 0.45f * depth);
      p->turn = 0.16f * x * depth;            // la cabeza la sigue, retrasada
      p->grow = 1.0f + 0.03f * depth;         // el bicho pasa cerca: se agranda
      p->gazeY = -3.0f * depth;
      p->bottom = 0.14f * depth;
      break;
    }
    default:
      break;
  }
}

// --- quiet-time gestures -----------------------------------------------------
//
// Two gestures are not one more piece of business in the random idle pool: they
// are about how long the face has been quiet. A stretch after four minutes of
// nothing, and a look up at the owner every 45-105 s. Both are pure functions of
// the idle age and the seed, so neither needs state and both are testable.
//
// They never take the glass while an idle event is already playing: the bug, the
// wink or the yawn are the surprise, and these two are the background hum.
#define BUDDY_STRETCH_PERIOD_MS 240000u  // at most one every four minutes
#define BUDDY_STRETCH_MS 1400u
#define BUDDY_OWNER_EPOCH_MS 75000u      // one look per 75 s slot
#define BUDDY_OWNER_SPREAD_MS 30000u     // it lands in the first 30 s of the slot
#define BUDDY_OWNER_MS 1100u

// The stretch: once every four minutes of quiet, and only after the first four.
static inline bool buddyStretchAt(uint32_t ageMs) {
  if (ageMs < BUDDY_STRETCH_PERIOD_MS) return false;
  return (ageMs % BUDDY_STRETCH_PERIOD_MS) < BUDDY_STRETCH_MS;
}

struct BuddyOwnerLook {
  bool active = false;
  float t = 0.0f;  // 0..1 through the look
};

// The look up at the owner: one per 75 s slot, landing in the slot's first 30 s,
// so two looks are never closer than 45 s and never further than 105 s.
static inline BuddyOwnerLook buddyOwnerLookAt(uint32_t ageMs, uint32_t seed) {
  BuddyOwnerLook look;
  const uint32_t epoch = ageMs / BUDDY_OWNER_EPOCH_MS;
  const uint32_t base = epoch * BUDDY_OWNER_EPOCH_MS;
  const uint32_t h = buddyHash32(seed ^ (epoch * 0x7F4A7C15u));
  const uint32_t start = base + (h % BUDDY_OWNER_SPREAD_MS);
  if ((int32_t)(ageMs - start) < 0) return look;
  const uint32_t in = ageMs - start;
  if (in >= BUDDY_OWNER_MS) return look;
  look.active = true;
  look.t = (float)in / (float)BUDDY_OWNER_MS;
  return look;
}

// Paint both quiet gestures onto an idle pose. `ageMs` is how long the face has
// been idle, which is exactly the "nothing has happened for a while" the design
// asks for; the caller only calls this while idle.
static inline void buddyQuietGestures(uint32_t ageMs, uint32_t seed,
                                      BuddyPose *p) {
  if (!p) return;
  const float PI_F = 3.14159265f;
  BuddyOwnerLook look = buddyOwnerLookAt(ageMs, seed);
  if (look.active) {
    const float pulse = sinf(look.t * PI_F);  // fades in and out, no pop
    p->gazeX -= 16.0f * pulse;
    p->gazeY -= 5.0f * pulse;
    // The eye opens a little: paying attention, not staring.
    const float lid = 0.08f * pulse;
    if (p->lid < lid) p->lid = lid;
    p->rx = 48 + (int)lroundf(2.0f * pulse);
  }
  if (buddyStretchAt(ageMs)) {
    // Rise fast, hold, come back. The stretch is the one gesture that reads as
    // the body moving, so it needs a plateau: a plain sine reads as a bounce.
    const uint32_t in = ageMs % BUDDY_STRETCH_PERIOD_MS;
    const float t = (float)in / (float)BUDDY_STRETCH_MS;
    float k;
    if (t < 0.25f) {
      k = t / 0.25f;
    } else if (t > 0.75f) {
      k = (1.0f - t) / 0.25f;
    } else {
      k = 1.0f;
    }
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    p->gazeY -= 8.0f * k;
    p->lid = 0.38f * k;
    p->rx = 48 - (int)lroundf(3.0f * k);
  }
}

struct BuddyBug {
  bool active = false;
  uint32_t eventId = 0;  // the epoch, so the renderer can cancel this one
  float x = 0;           // screen px
  float y = 0;
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
};

static inline BuddyBug buddyBugAt(uint32_t nowMs, uint32_t seed) {
  BuddyBug b;
  BuddyIdleEvent e = buddyIdleEventAt(nowMs, seed);
  if (!e.active || e.kind != BUDDY_IDLE_BUG) return b;
  uint32_t h = e.hash;
  b.active = true;
  b.eventId = e.eventId;
  b.startMs = e.startMs;
  b.durationMs = e.durationMs;
  float t = e.t;
  // Crawls in from one side and out the other, wandering up and down.
  float dir = ((h >> 16) & 1u) ? 1.0f : -1.0f;
  float from = dir > 0.0f ? -20.0f : 340.0f;
  float to = dir > 0.0f ? 340.0f : -20.0f;
  b.x = from + (to - from) * t;
  uint32_t in = (uint32_t)(t * (float)e.durationMs);
  float wobble = sinf(t * 6.2831853f * 1.5f + (float)((h >> 20) % 100u));
  b.y = 96.0f + wobble * 34.0f - (float)in * 0.0015f;
  return b;
}

// Where the eyes should look to follow the bug: a bounded offset from the eye
// line, so the pair leans towards it without leaving the glass.
static inline void buddyBugGaze(const BuddyBug &b, float *gazeX, float *gazeY) {
  float gx = (b.x - 160.0f) / 9.0f;
  float gy = (b.y - 88.0f) / 12.0f;
  if (gx > 22.0f) gx = 22.0f;
  if (gx < -22.0f) gx = -22.0f;
  if (gy > 14.0f) gy = 14.0f;
  if (gy < -14.0f) gy = -14.0f;
  *gazeX = gx;
  *gazeY = gy;
}

// One Z of the sleeping animation: three letters rise and fade on a 2.4 s loop.
#define BUDDY_ZZZ_MS 2400u
#define BUDDY_ZZZ_COUNT 3

static inline void buddyZzz(uint32_t nowMs, int index, int *x, int *y,
                            int *alphaPercent) {
  if (index < 0) index = 0;
  if (index >= BUDDY_ZZZ_COUNT) index = BUDDY_ZZZ_COUNT - 1;
  float t = (float)(nowMs % BUDDY_ZZZ_MS) / (float)BUDDY_ZZZ_MS;
  float local = t + (float)index / (float)BUDDY_ZZZ_COUNT;
  local -= floorf(local);
  *x = 244 + (int)(local * 30.0f);
  *y = 112 - (int)(local * 54.0f) - index * 6;
  float fade = 1.0f - local;
  *alphaPercent = (int)(fade * 80.0f);
}

// Waking up. A shut eye is a different shape (a bar), not a lid, so the opening
// has to be manufactured: the renderer seeds the pose filter's lid to shut on the
// frame the face wakes (`buddyPoseFilterSeedLid`), the target here is an open eye
// with a startle, and the ease travels the lid from 1 to 0. That is the ~100 ms
// opening the owner asked for, instead of the shape popping open.
static inline void buddyWakeEyes(BuddyPose *p) {
  if (!p) return;
  p->closed = false;
  p->blink = false;
  p->happy = false;
  p->lid = 0.0f;
  p->lidL = 0.0f;
  p->lidR = 0.0f;
  p->surprise = true;
  p->rx = 50;
  p->ry = 60;
}

// One gesture slot: the doc wants a new one every 3-6 s while the state does
// not change. The duration comes from the seed and the slot, so the rhythm is
// irregular instead of a mechanical 4 s beat.
struct BuddyGesture {
  uint32_t slot;
  uint32_t startMs;
};

static inline BuddyGesture buddyGestureAt(uint32_t ageMs, uint32_t seed) {
  BuddyGesture g = {0, 0};
  uint32_t start = 0;
  for (;;) {
    uint32_t duration =
        3000u + (buddyHash32(seed + g.slot * 0x9E3779B9u) % 3000u);
    if (start + duration > ageMs) {
      g.startMs = start;
      return g;
    }
    start += duration;
    g.slot++;
  }
}

// The calendar's reaction, painted on top of the UPCOMING base pose. `msToStart`
// is the time left until the event (negative once it started) and `intensity` is
// how much of the lead window is already consumed (0..100), both from
// `calendar_plan.h`. The escalation is monotonic in intensity on purpose: an
// outside observer reads the same direction as the owner, "the clock is closer".
static inline void buddyUpcomingPose(int32_t msToStart, uint8_t intensity,
                                     uint32_t nowMs, BuddyPose *p) {
  if (!p) return;
  const int level = intensity > 100 ? 100 : (int)intensity;

  // The clock lives in the top-right HUD corner, so she keeps checking it. The
  // whole pair turns slightly towards it instead of staring at the owner.
  p->gazeX = 8.0f + (float)level * 0.07f;
  p->gazeY = -7.0f - (float)level * 0.06f;
  p->turn = -0.10f;
  p->tilt = -1.5f;

  if (msToStart <= 0) {
    // It is happening now: the widest, stillest eyes of the ladder, plus a
    // double blink at the start of every 4 s cycle so it reads as "now" and
    // not as a frozen stare.
    p->rx = 56;
    p->ry = 70;
    p->surprise = true;
    p->lid = 0.0f;
    p->bottom = 0.0f;
    p->tremble = 0.0f;
    p->bob = sinf((float)nowMs * 0.0024f) * 1.4f;
    const uint32_t in = nowMs % 4000u;
    p->blink = in < 140u || (in >= 320u && in < 460u);
    return;
  }

  // 48x48 is the neutral eye; the last stretch opens it past the surprise size.
  p->rx = 48 + level / 8;   // 48..60
  p->ry = 48 + level / 5;   // 48..68
  p->lid = 0.10f * (float)(100 - level) / 100.0f;  // lids lift as it gets closer
  p->bottom = 0.05f;
  p->tremble = level >= 85 ? 0.25f : 0.0f;
  p->bob = sinf((float)nowMs * 0.0022f) * (0.6f + (float)level * 0.01f);
}

static inline BuddyPose buddyPose(BuddyState state, uint32_t ageMs,
                                  uint32_t nowMs, uint32_t seed) {
  BuddyPose p;
  bool genericBlink = true;

  switch (state) {
    case BUDDY_UNCONFIGURED:
      p.dim = true;
      genericBlink = false;
      break;
    case BUDDY_OFFLINE:
      // No bridge: a dim, quiet face. Not an error face.
      p.dim = true;
      genericBlink = false;
      break;
    case BUDDY_DONE:
      // One second of satisfaction, then a relaxed open face for the rest of
      // the bridge's ~8 s done window. The little bounce is the "yes!".
      genericBlink = false;
      if (ageMs < 1200) {
        p.happy = true;
        p.bob = sinf((float)nowMs * 0.010f) * 2.5f;
        // The victory jump: a single lift on the first 400 ms, so "done" after
        // a tool lands harder than the quiet check of a plain finish.
        if (ageMs < 400) {
          p.bob -= 4.0f * sinf((float)ageMs * 0.0078f);
          p.ry = 54;
        }
      } else {
        // Afterglow: pleased, not beaming. The lower lid rises a little and
        // the face breathes, which is the difference between "I won" and "ahh".
        p.bottom = 0.14f;
        p.lid = 0.15f;
        p.ry = 44;
        p.bob = sinf((float)nowMs * 0.0016f) * 1.0f;
      }
      break;
    case BUDDY_ERROR: {
      // Something broke, and it is not the owner's fault: the face is upset
      // with itself. Three bands, escalating with how long it has been stuck:
      //   0-1.2 s  startled: wide, hot-tinted, a hard double blink.
      //   1.2-6 s  frustrated: eyes squeezed by the lids, looking down, a fast
      //            tremble. Shame/annoyance, not confusion.
      //   6 s+     the apology: eyes droop, one slow sad blink, the head turns
      //            away. This is what a stuck job looks like after a while.
      // The crossed look survives as the very first instants, because that is
      // what "no entiendo" reads as, and then it turns into "uf".
      genericBlink = false;
      if (ageMs < 1200) {
        p.eyeGazeL = 7;
        p.eyeGazeR = -7;
        p.lid = 0.05f;
        p.hot = 0.45f;
        p.rx = 50;
        p.ry = 54;
        p.blink = (nowMs % 420) < 70;
      } else if (ageMs < 6000) {
        p.eyeGazeL = 3.0f;
        p.eyeGazeR = -3.0f;
        p.hot = 0.6f;
        p.grow = 1.06f;
        p.ry = 40;
        p.lid = 0.22f;
        p.lidL = 0.08f;
        p.gazeY = 7.0f;              // looks down: rueful
        p.tilt = -5.0f;
        p.tremble = 0.5f;
        p.bob = sinf((float)nowMs * 0.0075f) * 1.2f;
      } else {
        p.hot = 0.3f;
        p.ry = 36;
        p.lid = 0.42f;               // heavy, drooping lids
        p.bottom = 0.12f;
        p.gazeY = 9.0f;
        p.tilt = -3.0f * sinf((float)ageMs * 0.0006f);
        p.turn = -0.22f;             // turns the head away, embarrassed
        p.bob = sinf((float)nowMs * 0.0014f) * 1.4f;
        // One long sorry blink every ~3.2 s, on a fixed but unhurried beat.
        p.blink = (nowMs % 3200) < 300;
      }
      break;
    }
    case BUDDY_WAITING: {
      // The owner has to answer: this is not "the server is busy", it is "I am
      // looking at you". The ladder escalates in three bands:
      //   0-4 s   expectant: eyes up, holding on the owner, head steady.
      //   4-12 s  asking: the head turns slightly aside and tips, the way people
      //           tilt when they are waiting for an answer, with a faster bob.
      //   12 s+   fidgeting: the look wanders off (does the owner even see me?),
      //           the head keeps turning, and the lower lids rise a touch.
      // Nothing here ever narrows the eyes: a squint reads as annoyance, and
      // the owner being slow to answer is not something to be annoyed about.
      p.rx = 50;
      p.ry = 52;
      p.gazeY = -6.0f;
      if (ageMs < 4000) {
        // A slow sway, so the attention is alive but clearly held on the owner.
        p.gazeX = sinf((float)ageMs * 0.0009f) * 5.0f;
        p.bob = sinf((float)nowMs * 0.0045f) * 2.0f;
      } else if (ageMs < 12000) {
        // Asking: the head turns and tips, the eyes stay on the owner.
        float t = (float)(ageMs - 4000) / 8000.0f;
        p.tilt = 4.5f * sinf((float)ageMs * 0.0011f);
        p.turn = 0.16f * sinf((float)ageMs * 0.0007f);
        p.gazeY = -8.0f;
        p.bob = sinf((float)nowMs * 0.0062f) * 3.0f;
        // The lower lid comes up a hair: a soft, patient eye, not a squint.
        p.bottom = 0.10f;
        (void)t;
      } else {
        // Fidgeting: the look wanders and the head turns further, so the near
        // eye visibly grows while the far one shrinks.
        uint32_t t = ageMs - 12000;
        p.tilt = 3.0f * sinf((float)t * 0.0007f);
        p.turn = 0.34f * sinf((float)t * 0.0009f);
        p.gazeX = sinf((float)t * 0.0013f) * 16.0f;
        p.gazeY = -4.0f + sinf((float)t * 0.0021f) * 3.0f;
        p.lid = 0.06f + 0.10f * fabsf(sinf((float)t * 0.0026f));
        p.bob = sinf((float)nowMs * 0.0035f) * 2.5f;
      }
      break;
    }
    case BUDDY_TOOL: {
      // A tool starting is a small surprise: wide for 200 ms, then the cycle.
      if (ageMs < 200) {
        p.surprise = true;
        p.rx = 50;
        p.ry = 60;
        genericBlink = false;
        break;
      }
      BuddyGesture g = buddyGestureAt(ageMs, seed ^ 0x51ed270bu);
      uint32_t count = ageMs > 30000u ? 5u : 4u;
      uint32_t which = g.slot % count;
      uint32_t inSlot = ageMs - g.startMs;
      switch (which) {
        case 0:  // reading: narrowed with a lateral micro-movement
          p.ry = 34;
          p.lid = 0.15f;
          p.gazeX = sinf((float)inSlot * 0.004f) * 4.0f;
          break;
        case 1:  // focus: one eye a little more closed than the other
          p.ry = 36;
          p.lidL = 0.3f;
          p.lidR = 0.1f;
          p.rot = 3.0f;
          break;
        case 2:  // tension up: wider and up
          p.ry = 52;
          p.gazeY = -5.0f;
          break;
        case 3:  // a rapid double blink at the start of the slot
          p.lid = 0.05f;
          p.blink = (inSlot < 120) || (inSlot >= 240 && inSlot < 360);
          break;
        case 4:  // the long tool sigh
          p.lid = 0.2f;
          p.gazeY = 3.0f;
          break;
      }
      break;
    }
    case BUDDY_THINKING: {
      BuddyGesture g = buddyGestureAt(ageMs, seed);
      uint32_t count = ageMs > 20000u ? 6u : 5u;
      uint32_t which = g.slot % count;
      uint32_t inSlot = ageMs - g.startMs;
      switch (which) {
        case 0:  // remembering: up-left, tilted, the right eye squints
          p.gazeX = -12;
          p.gazeY = -12;
          p.rot = 8.0f;
          p.lidR = 0.3f;
          break;
        case 1:  // concentration: narrowed and asymmetric
          p.ry = 32;
          p.lid = 0.12f;
          p.lidL = 0.22f;
          p.rot = 4.0f;
          p.gazeX = -4;
          p.gazeY = -10;
          break;
        case 2:  // looks at the owner with a blink
          p.ry = 38;
          p.blink = inSlot < 220;
          break;
        case 3:  // searching: up-right
          p.gazeX = 12;
          p.gazeY = -12;
          p.rot = -6.0f;
          p.lidL = 0.25f;
          break;
        case 4:  // long blink
          p.ry = 36;
          p.lid = 0.15f;
          p.blink = inSlot < 300;
          break;
        case 5:  // "uf, it is taking long": looks at the owner, bottom lid cut
          p.ry = 36;
          p.bottom = 0.35f;
          p.lid = 0.1f;
          p.gazeY = 2.0f;
          break;
      }
      break;
    }
    case BUDDY_ASLEEP:
      // `ageMs` is the time since the last IMU stimulus, so the bands are the
      // sleep thresholds in `buddy_sleep.h` and not arbitrary ones: light sleep
      // at 60 s, no more lids at 120 s, eyes shut at 180 s.
      if (ageMs < 60000) {
        // Just nodded off: still looks present, normal blink.
      } else if (ageMs < 120000) {
        // Light sleep: gaze drifts to the sides and comes back, with a sigh.
        p.gazeX = sinf((float)ageMs * 0.0009f) * 14.0f;
        p.lid = 0.08f;
      } else if (ageMs < 180000) {
        // Starts to nod: the lids drop and bounce.
        p.lid = 0.35f;
        p.gazeY = sinf((float)ageMs * 0.002f) * 3.0f;
      } else {
        // Deep sleep: the eyes shut and the face stops. No breath, no drift,
        // no blink. The owner asked for exactly that ("que ahi no pase nada");
        // motion is what wakes it, and now the bridge cannot argue.
        p.closed = true;
        p.bob = 0.0f;
        genericBlink = false;
        return p;
      }
      break;
    case BUDDY_UPCOMING:
      // The clock is talking, not Sofía: the escalation lives in
      // `buddyUpcomingPose`, called by the face after this base pose. No idle
      // life (that is what the default would add), and the blink is the
      // reaction's own, so the last stretch does not look like a calm idle face.
      genericBlink = false;
      break;
    case BUDDY_IDLE:
    default:
      break;
  }

  if ((state == BUDDY_THINKING || state == BUDDY_TOOL) && p.bob == 0.0f) {
    // A slow breath under the gesture cycle, so a static pose still moves.
    p.bob = sinf((float)nowMs * 0.0018f) * 0.8f;
  }

  if (state == BUDDY_IDLE) {
    // Idle life: a 1-3 px microsaccade every 2-5 s, the odd side glance, and a
    // breath that lifts the whole eye line about one pixel.
    BuddySaccade sc = buddySaccadeAt(nowMs, seed);
    BuddyGlance gl = buddyGlanceAt(nowMs, seed);
    float breath = buddyBreath(nowMs);
    p.gazeX += sc.x + gl.x;
    p.gazeY += sc.y + breath * 0.35f;
    p.bob = breath * 1.0f;
    // The quiet gestures ride on top of the idle life, unless an idle event is
    // already playing.
    if (!buddyIdleEventAt(nowMs, seed).active) {
      buddyQuietGestures(ageMs, seed, &p);
    }
  }

  if (state == BUDDY_ASLEEP) {
    // The breath never stops, not even asleep: it is the only motion left.
    float breath = buddyBreath(nowMs);
    p.bob = p.closed ? breath * 2.0f : breath * 1.2f;
  }

  if (genericBlink && !p.blink && !p.happy && !p.closed && !p.dim) {
    // Not a timer: the blinks come unevenly, sometimes two in a row, sometimes
    // one that stays shut longer. See buddyBlinkAt.
    p.blink = buddyBlinkAt(nowMs, seed);
  }
  return p;
}

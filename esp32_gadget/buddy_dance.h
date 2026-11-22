// Sofía's dance: the beat of the track that is playing and the notes it throws.
//
// Spotify publishes no tempo any more (`audio-analysis` is gone), so the rhythm
// cannot be read, it has to be derived honestly from what the device does know:
//
// - every track gets its own stable BPM, hashed from `trackId`, so a song dances
//   the same way every time it plays and a new song changes the groove;
// - the phase comes from `progressMs`, the position Spotify reports, plus the
//   time elapsed since that poll. That is what makes the eyes land on the beat
//   instead of on the board's own clock, and it freezes by itself on a pause.
//
// Pure and header-only so `tools/buddy_dance_test.cpp` can pin the BPM range, the
// beat/bar arithmetic, the pose the rhythm produces and the life of a note,
// without a board and without Spotify.
#pragma once

#include <math.h>
#include <stdint.h>

#include "buddy_anim.h"

#define BUDDY_DANCE_BPM_MIN 84
#define BUDDY_DANCE_BPM_MAX 136
#define BUDDY_DANCE_BEATS_PER_BAR 4

// A stable tempo per track: the same id always dances at the same speed, and the
// range is where a body naturally bobs (84, half time of 168, up to 136).
static inline uint32_t buddyDanceBpm(const char *trackId) {
  uint32_t h = 2166136261u;  // FNV-1a offset: a short id still spreads well
  if (trackId) {
    for (const char *p = trackId; *p; ++p) {
      h ^= (uint8_t)*p;
      h *= 16777619u;
    }
  }
  h = buddyHash32(h);
  return BUDDY_DANCE_BPM_MIN +
         h % (BUDDY_DANCE_BPM_MAX - BUDDY_DANCE_BPM_MIN + 1u);
}

// Where the music is right now. Everything is derived from `positionMs` (the
// playback position, extrapolated from the last poll while it plays), so the
// dance follows the song and not the frame clock.
struct BuddyDanceBeat {
  bool active = false;
  uint32_t bpm = 110;
  uint32_t positionMs = 0;
  uint32_t beatIndex = 0;   // beats since the start of the track
  float beatPhase = 0.0f;   // 0 on the beat, approaching 1 before the next one
  uint32_t barIndex = 0;    // bars since the start of the track
  float barPhase = 0.0f;    // 0 on the downbeat, approaching 1 across the bar
};

static inline BuddyDanceBeat buddyDanceBeatAt(uint32_t bpm,
                                              uint32_t positionMs) {
  BuddyDanceBeat b;
  if (bpm < BUDDY_DANCE_BPM_MIN) bpm = BUDDY_DANCE_BPM_MIN;
  if (bpm > BUDDY_DANCE_BPM_MAX) bpm = BUDDY_DANCE_BPM_MAX;
  b.active = true;
  b.bpm = bpm;
  b.positionMs = positionMs;
  double beats = (double)positionMs * (double)bpm / 60000.0;
  b.beatIndex = (uint32_t)beats;
  b.beatPhase = (float)(beats - (double)b.beatIndex);
  b.barIndex = b.beatIndex / (uint32_t)BUDDY_DANCE_BEATS_PER_BAR;
  b.barPhase = ((float)(b.beatIndex % (uint32_t)BUDDY_DANCE_BEATS_PER_BAR) +
                b.beatPhase) /
               (float)BUDDY_DANCE_BEATS_PER_BAR;
  return b;
}

// The pose the rhythm paints: "respiracion con pulso".
//
// No hop at all. The face floats: 3 px of sway under everything, the eye grows a
// little on every beat and comes back, the joy rides on the lower lid, and the
// gaze orbits once per bar. The reason is the owner's own report -- the hop read
// as "poco organico" and not smooth enough -- and the measurement behind it: a
// big periodic jump only looks smooth if the frame rate is high, while a motion
// of <= 3 px per beat is smooth even at 20 fps. The approved artboard is
// `design/baile-sofia/05-respiracion-con-pulso.html`, and this is the same law
// with the same numbers.
//
// It *overrides* the channels it owns instead of adding to them: the idle face
// has its own microsaccades and glances, and a dance that merely added to those
// would read as a twitch, not as a beat.
static inline void buddyDancePose(const BuddyDanceBeat &beat, uint32_t nowMs,
                                  BuddyPose *p) {
  if (!p || !beat.active) return;
  (void)nowMs;
  const float tau = 6.2831853f;
  const uint32_t barMs = 4u * 60000u / (beat.bpm ? beat.bpm : 110u);
  const uint32_t pos = beat.positionMs;
  const float pulse = sinf(3.14159265f * beat.beatPhase);
  // Two different periods: the sway is two bars long and the orbit one, so the
  // face never lands twice on the same picture.
  const float sway = sinf(tau * (float)(pos % (barMs * 2u)) / (float)(barMs * 2u));
  const float orbit = tau * (float)(pos % barMs) / (float)barMs;
  // A song does not hit every beat equally hard.
  static const float kAccent[4] = {1.0f, 0.75f, 0.9f, 0.7f};
  const float accent = kAccent[beat.beatIndex % 4u];

  p->gazeX = 3.0f * cosf(orbit);
  p->gazeY = -1.6f + 1.4f * sinf(orbit);
  p->eyeGazeL = 0.0f;
  p->eyeGazeR = 0.0f;
  p->bob = -1.2f * pulse * accent - 1.75f * (0.5f + 0.5f * sway);
  // The eye grows from the inside: 2 px of height and 1 px of width, never more,
  // which is what keeps every frame a small step.
  p->rx = (int)lroundf(48.0f + 1.3f * pulse * accent);
  p->ry = (int)lroundf(48.0f + 2.0f * pulse * accent);
  // The upper lid never moves: on this face a lid going down is tiredness, and
  // the owner read the first version as sleepy for exactly that.
  p->lid = 0.0f;
  p->lidL = 0.0f;
  p->lidR = 0.0f;
  // Joy lives in the lower eyelid here; there is no mouth to smile with.
  p->bottom = 0.10f + 0.08f * pulse * accent;
  p->rot = 2.2f * sinf(orbit + 1.1f);
  p->rotL = 0.0f;
  p->rotR = 0.0f;
  p->tilt = 2.6f * sinf(orbit);
  p->turn = 0.05f * cosf(orbit);
  p->grow = 1.0f;
  p->open = 1.0f;
  p->glow = 1.0f;
  p->hot = 0.0f;
  p->dim = false;
  p->tremble = 0.0f;
  p->surprise = false;
  p->happy = false;
  p->closed = false;
  // A face that never blinks looks dead. Once every four bars, between beats, so
  // it never lands on the pulse.
  p->blink = (beat.barIndex % 4u) == 3u && beat.beatPhase > 0.5f &&
             beat.beatPhase < 0.62f;
}

// Mixes two poses. The dance writes on top of the eased face for the hop itself,
// because the pose filter would flatten a 545 ms arc into a sway; `t` is the
// blend, 0 leaving the base untouched and 1 being the dance exactly.
static inline void buddyPoseMix(const BuddyPose &base, const BuddyPose &over,
                                float t, BuddyPose *out) {
  if (!out) return;
  if (!(t > 0.0f)) {
    *out = base;
    return;
  }
  if (t >= 1.0f) {
    *out = over;
    return;
  }
  *out = base;
  out->gazeX = base.gazeX + (over.gazeX - base.gazeX) * t;
  out->gazeY = base.gazeY + (over.gazeY - base.gazeY) * t;
  out->eyeGazeL = base.eyeGazeL + (over.eyeGazeL - base.eyeGazeL) * t;
  out->eyeGazeR = base.eyeGazeR + (over.eyeGazeR - base.eyeGazeR) * t;
  out->rx = (int)lroundf((float)base.rx + (float)(over.rx - base.rx) * t);
  out->ry = (int)lroundf((float)base.ry + (float)(over.ry - base.ry) * t);
  out->lid = base.lid + (over.lid - base.lid) * t;
  out->lidL = base.lidL + (over.lidL - base.lidL) * t;
  out->lidR = base.lidR + (over.lidR - base.lidR) * t;
  out->bottom = base.bottom + (over.bottom - base.bottom) * t;
  out->rot = base.rot + (over.rot - base.rot) * t;
  out->rotL = base.rotL + (over.rotL - base.rotL) * t;
  out->rotR = base.rotR + (over.rotR - base.rotR) * t;
  out->hot = base.hot + (over.hot - base.hot) * t;
  out->bob = base.bob + (over.bob - base.bob) * t;
  out->grow = base.grow + (over.grow - base.grow) * t;
  out->tremble = base.tremble + (over.tremble - base.tremble) * t;
  out->tilt = base.tilt + (over.tilt - base.tilt) * t;
  out->turn = base.turn + (over.turn - base.turn) * t;
  out->open = base.open + (over.open - base.open) * t;
  out->glow = base.glow + (over.glow - base.glow) * t;
  // The booleans cannot be blended: halfway through a squint is unspeakable.
  if (t > 0.5f) {
    out->blink = over.blink;
    out->happy = over.happy;
    out->closed = over.closed;
    out->surprise = over.surprise;
    out->dim = over.dim;
  }
}

// --- the music truth ---------------------------------------------------------
//
// The Spotify poll sets `LOADING` at the start of every request, which is every
// three seconds, and a transport error is not a pause either. A gate that read
// the raw feed stopped and restarted the dance on every poll: that was the
// owner's "baila, deja de bailar, y vuelve a bailar, cuando la musica no paro
// nunca". So the dance listens to the last *authoritative* answer (a track, or a
// 204 that says nothing is playing) and ignores everything else for a bounded
// window.
enum BuddyDanceMusic {
  BUDDY_DANCE_MUSIC_UNKNOWN = 0,  // no answer yet, a poll in flight, or an error
  BUDDY_DANCE_MUSIC_PLAYING = 1,
  BUDDY_DANCE_MUSIC_SILENT = 2,   // Spotify answered that nothing is playing
};

// How long an old answer still counts. A failed poll retries in 20 s
// (`SPOTIFY_POLL_RETRY_MS`), so one of those must not stop a song in the middle.
#define BUDDY_DANCE_HOLD_MS 20000u

struct BuddyDanceLatch {
  bool playing = false;
  bool have = false;     // an authoritative answer has been seen
  uint32_t heardMs = 0;  // when it arrived
};

// One step of the latch. `music` is a BuddyDanceMusic. Playing and silent are
// believed at once; anything else only ages what was already known.
static inline void buddyDanceLatchStep(BuddyDanceLatch *l, int music,
                                       uint32_t nowMs) {
  if (!l) return;
  if (music == BUDDY_DANCE_MUSIC_PLAYING || music == BUDDY_DANCE_MUSIC_SILENT) {
    l->playing = music == BUDDY_DANCE_MUSIC_PLAYING;
    l->have = true;
    l->heardMs = nowMs;
    return;
  }
  // Nothing new: the previous answer still counts until the window closes. A
  // pause is never aged out -- it latched `false` and stays there.
  if (!l->have) return;
  if ((uint32_t)(nowMs - l->heardMs) > BUDDY_DANCE_HOLD_MS) l->playing = false;
}

static inline bool buddyDanceLatchActive(const BuddyDanceLatch *l) {
  return l && l->playing;
}

// --- notes -------------------------------------------------------------------
//
// Small eighth notes that leave the bottom of the glass, rise along the two
// outermost columns and fade out. They never cross the eyes or their glow: the
// pair sweeps x 42..278 while it dances, so the only honest free bands are the
// 12 px note glyphs on either edge, which is exactly what these are.

#define BUDDY_NOTE_COUNT 8
#define BUDDY_NOTE_LIFE_MS 2400u
// Measured on the glass with the dance at full sway: the left eye's drawn extent
// plus its glow never starts before x=42 and the right one never ends after 278.
// These bands, one 12 px glyph wide, keep a couple of pixels of black between.
#define BUDDY_NOTE_LEFT_MIN 12.0f
#define BUDDY_NOTE_LEFT_MAX 30.0f
#define BUDDY_NOTE_RIGHT_MIN 291.0f
#define BUDDY_NOTE_RIGHT_MAX 308.0f
#define BUDDY_NOTE_Y0 152.0f     // where it appears
#define BUDDY_NOTE_RISE 46.0f    // px per second: 110 px over its life
#define BUDDY_NOTE_DRIFT 7.0f    // px per second, both signs

struct BuddyNote {
  bool active = false;
  float x = 0.0f;
  float y = 0.0f;
  float size = 1.0f;    // 0.75..1.15 of the base glyph
  float drift = 0.0f;   // px per second sideways
  float alpha = 0.0f;   // 0..1, dithered by the renderer
  uint32_t bornMs = 0;
  // The band the note was born in. The drift is clamped to it, so a note can
  // never wander out of its side field and onto an eye.
  float minX = 0.0f;
  float maxX = 0.0f;
};

// The whole population plus the beat it last reacted to. Keeping the last beat
// inside the field is what makes a spawn happen exactly once per beat even if
// the frame rate changes.
struct BuddyNoteField {
  BuddyNote notes[BUDDY_NOTE_COUNT];
  uint32_t lastBeat = 0;
  bool primed = false;  // the beat that was already playing when the dance began
  uint32_t rng = 0x9E3779B9u;
};

static inline uint32_t buddyNoteRand(BuddyNoteField *f) {
  f->rng = buddyHash32(f->rng + 0x9E3779B9u);
  return f->rng;
}

// 0 outside its life, a quick fade in, a hold, and a long fade out: the notes
// have to be readable when they appear and gone by the time they reach the top.
static inline float buddyDanceNoteAlpha(float life) {
  if (!(life > 0.0f) || life >= 1.0f) return 0.0f;
  if (life < 0.12f) return life / 0.12f;
  if (life < 0.45f) return 1.0f;
  return 1.0f - (life - 0.45f) / 0.55f;
}

static inline void buddyDanceNotesClear(BuddyNoteField *f) {
  if (!f) return;
  *f = BuddyNoteField();
}

// One frame of the field. `dtMs` only integrates what is already alive; a note is
// born on a beat crossing, which is a function of the music and never of the
// frame rate.
static inline void buddyDanceNotesStep(BuddyNoteField *f,
                                       const BuddyDanceBeat &beat,
                                       uint32_t nowMs, uint32_t dtMs) {
  if (!f) return;
  const float dt = (float)dtMs / 1000.0f;
  for (int i = 0; i < BUDDY_NOTE_COUNT; i++) {
    BuddyNote &n = f->notes[i];
    if (!n.active) continue;
    uint32_t age = nowMs - n.bornMs;
    if (age >= BUDDY_NOTE_LIFE_MS) {
      n.active = false;
      continue;
    }
    float life = (float)age / (float)BUDDY_NOTE_LIFE_MS;
    n.y = BUDDY_NOTE_Y0 - BUDDY_NOTE_RISE * life *
                              ((float)BUDDY_NOTE_LIFE_MS / 1000.0f);
    n.x += n.drift * dt;
    if (n.x < n.minX) n.x = n.minX;
    if (n.x > n.maxX) n.x = n.maxX;
    n.alpha = buddyDanceNoteAlpha(life);
  }

  if (!beat.active) {
    f->primed = false;
    return;
  }
  if (!f->primed) {
    // Whatever beat was already playing is not a new one: begin from silence so
    // entering the dance does not dump six notes at once.
    f->primed = true;
    f->lastBeat = beat.beatIndex;
    return;
  }
  if (beat.beatIndex == f->lastBeat) return;
  f->lastBeat = beat.beatIndex;
  // One note every two beats, alternating sides, so the glass is never crowded
  // and the two fields answer each other.
  if ((beat.beatIndex % 2u) != 0u) return;
  int slot = -1;
  for (int i = 0; i < BUDDY_NOTE_COUNT; i++) {
    if (!f->notes[i].active) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;
  // The first note of a phrase leaves on the left, the next one answers on the
  // right: two calls and a response, not a random side.
  const bool right = ((beat.beatIndex / 2u) & 1u) == 0u;
  const uint32_t r = buddyNoteRand(f);
  const float span = BUDDY_NOTE_LEFT_MAX - BUDDY_NOTE_LEFT_MIN;
  BuddyNote &n = f->notes[slot];
  n.active = true;
  n.minX = right ? BUDDY_NOTE_RIGHT_MIN : BUDDY_NOTE_LEFT_MIN;
  n.maxX = right ? BUDDY_NOTE_RIGHT_MAX : BUDDY_NOTE_LEFT_MAX;
  n.x = n.minX + (float)(r % 1000u) / 1000.0f * span;
  n.y = BUDDY_NOTE_Y0;
  n.size = 0.80f + (float)((r >> 10) % 300u) / 1000.0f;
  n.drift = ((r >> 12) & 1u) ? BUDDY_NOTE_DRIFT : -BUDDY_NOTE_DRIFT;
  n.alpha = 0.0f;
  n.bornMs = nowMs;
}

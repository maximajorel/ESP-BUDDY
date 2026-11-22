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

// The pose the rhythm paints. It *overrides* the channels it owns instead of
// adding to them: the idle face has its own microsaccades and glances, and a
// dance that merely added to those would read as a twitch, not as a beat.
//
// The vocabulary is the face's own: no brows, no mouth, no extra shapes. The
// sway turns the head (turn), the pair rolls (tilt/rot), the head lifts on the
// beat (bob), the lids dip with it, and the eyes look where the head goes.
static inline void buddyDancePose(const BuddyDanceBeat &beat, uint32_t nowMs,
                                  BuddyPose *p) {
  if (!p || !beat.active) return;
  (void)nowMs;
  const float tau = 6.2831853f;
  // The bar is the musical phrase: the body sways across it and the dance changes
  // direction every bar, which is what a two-step looks like on a face.
  const float sway = sinf(beat.barPhase * tau);
  const float step = (beat.barIndex & 1u) ? -1.0f : 1.0f;
  // Sharp attack, quick decay: 1 on the beat, 0 by the next one. A linear ramp
  // would make every beat look late.
  const float pop = 1.0f - beat.beatPhase;
  const float kick = pop * pop;

  // The sway is smaller than it wants to be: the pair sliding sideways eats the
  // only two free columns of the glass, and the beat already reads in the lids,
  // the roll and the lift. 7 px of gaze keeps the notes' bands clear of the eye
  // glow (measured on the panel, see the note bands below).
  p->gazeX = 5.0f * sway + 2.0f * step * kick;
  p->gazeY = -2.5f * kick;
  p->eyeGazeL = 0.0f;
  p->eyeGazeR = 0.0f;
  p->turn = 0.14f * sway;
  p->tilt = -5.0f * sway + 1.5f * step;
  p->rot = 2.2f * step * sinf(beat.barPhase * tau + 0.9f);
  p->rotL = 0.0f;
  p->rotR = 0.0f;
  p->bob = -3.5f * kick;
  // The lids dip on the beat: that is the nod, and it is the only way this face
  // can mark time without a mouth.
  p->lid = 0.06f + 0.10f * kick;
  p->lidL = 0.0f;
  p->lidR = 0.0f;
  p->bottom = 0.0f;
  p->grow = 1.0f + 0.03f * kick;
  p->open = 1.0f;
  p->glow = 1.0f;
  p->hot = 0.0f;
  p->dim = false;
  p->tremble = 0.0f;
  p->rx = 48;
  p->ry = 48;
  p->surprise = false;
  p->happy = false;
  p->closed = false;
  // The random idle blink would land off the beat, so the dance blinks on its
  // own count: the last beat of every two bars, in time with the phrase.
  p->blink = (beat.beatIndex % 8u) == 7u && beat.beatPhase < 0.18f;
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

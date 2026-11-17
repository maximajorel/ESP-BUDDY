// Sofía's face screen (S21): a procedural buddy that reacts to Hermes.
//
// The bridge on the LAN exposes {state, label, active}; this screen polls it in
// its own task, keeps a snapshot, and draws a face that is only eyes: no brows,
// no mouth. No assets and no audio: the face language follows OpenCharm (MIT) and
// the states come straight from the bridge.
#pragma once

#include <Arduino.h>

#include "buddy_imu.h"
#include "buddy_link.h"
#include "buddy_phase.h"
#include "buddy_state.h"

void buddyBegin();
void buddyEnter();

// Debug only: pin a state and an age for a few seconds so the serial console can
// dump an expression (for example thinking at 25 s) without waiting for it.
void buddyPinFace(BuddyState state, uint32_t ageMs, uint32_t holdMs);

// Debug only: hold the tap boop (happy face) for a few seconds so the serial
// `dump` can show the squircle with its lower lid without racing a real tap.
void buddyPinHappy(uint32_t holdMs);

// Debug only: pin the HUD/LED phase so `buddyphase ask` can be dumped without
// waiting for the bridge to ask for approval (which is rare by design).
void buddyPinPhase(BuddyPhase phase, uint32_t holdMs);
bool buddyTick(uint32_t nowMs);
void buddyShortPress();
// The idle toy: turn the red ball on or off. The double tap on the live screen
// drives this; the console exposes it so a probe can prove the bounce without
// tapping twice.
void buddySetBall(bool on);

// Snapshot for the API and the screen. `revision` bumps when something visible
// changed, so the tick only repaints on news (plus the idle animation).
struct BuddyView {
  bool configured;
  bool online;
  BuddyState state;
  char label[BUDDY_LABEL_MAX + 1];
  int active;
  BuddyPhase phase;     // the HUD/LED channel, never derived from state
  BuddyRgb led;          // colour already resolved by the bridge
  bool ledValid;
  uint32_t ageMs;       // age of the snapshot
  uint32_t stateAgeMs;  // how long the bridge says this state has lasted
  uint32_t revision;
  // Why the link is in the shape it is: the glass can only dim, the panel names
  // the cause (no clock, rejected certificate, Cloudflare, wrong token...).
  BuddyLink link;
  char linkDetail[96];  // mbedtls error text when the handshake was rejected
};
void buddyGetView(BuddyView *out);

// Starts a run through the bridge (POST /buddy/ask). The panel's prompt box uses
// this; the device never holds the Hermes key, only the bridge token.
bool buddyAsk(const char *prompt, char *err, size_t errCap);

// Console diagnostic: the geometry of the last face drawn (no readable bus).
void buddyDumpDraw();

// What the face is feeling right now, so the rear bead can match it: anger is
// red, deep sleep is a faint ember, everything else follows the bridge phase.
// The vocabulary and the decision live in buddy_imu.h, where a test can pin it.
BuddyMood buddyMood();

// Console debug: put the idle distraction on the glass for `holdMs`.
void buddyForceBug(uint32_t holdMs);

// The `/events` stream: what each event means, and what the cursor drops.
//
// With a one-second poll any event shorter than a second is lost, so the bridge
// keeps a 64-event ring and the device long-polls `GET /events?since=<seq>&wait=N`.
// The transport is `buddy_http.cpp`; the JSON decoding is `screen_buddy.cpp`.
// Everything that is a rule rather than a socket lives here, header-only, so
// `tools/buddy_event_test.cpp` can pin it without an ESP32.
//
// Three rules decide almost everything:
//
//   * a `seq` already applied is dropped — a retried long-poll must not replay
//     the face, and the ring can hand back events from before the last one;
//   * a `seq` ahead of the server means the bridge restarted, so the answer is
//     `[]` and the truth is whatever `GET /state` says next;
//   * the direction of a change is what separates a win from relief, and that is
//     the `prev` field the bridge started including in `state` events.
#pragma once

#include <stdint.h>

#include "buddy_state.h"

enum BuddyEventKind {
  BUDDY_EVENT_UNKNOWN = 0,
  BUDDY_EVENT_STATE,
  BUDDY_EVENT_LABEL,
  BUDDY_EVENT_ASK_ACCEPTED
};

// One decoded event. `prev` is only meaningful for BUDDY_EVENT_STATE, and it is
// BUDDY_UNCONFIGURED when the bridge did not send it (the first event of a run).
struct BuddyEvent {
  int seq = -1;
  BuddyEventKind kind = BUDDY_EVENT_UNKNOWN;
  BuddyState state = BUDDY_IDLE;
  BuddyState prev = BUDDY_UNCONFIGURED;
  char label[BUDDY_LABEL_MAX + 1] = {0};
  char button[10] = {0};
};

static inline BuddyEventKind buddyEventKindFromName(const char *name) {
  if (!name || !*name) return BUDDY_EVENT_UNKNOWN;
  if (strcmp(name, "state") == 0) return BUDDY_EVENT_STATE;
  if (strcmp(name, "label") == 0) return BUDDY_EVENT_LABEL;
  if (strcmp(name, "ask.accepted") == 0) return BUDDY_EVENT_ASK_ACCEPTED;
  return BUDDY_EVENT_UNKNOWN;
}

// 1 when the event is news and the cursor should move, 0 when it is a replay.
static inline int buddyEventApply(int lastSeq, int seq) {
  if (seq < 0) return 0;
  return seq > lastSeq ? 1 : 0;
}

// The server's cursor is older than ours: the bridge restarted (its ring starts
// over). `lastSeq < 0` means the device has never read `/state`, so it owns no
// cursor yet.
static inline int buddyEventsNeedResync(int sinceSeq, int lastSeq) {
  if (lastSeq < 0) return 1;
  return sinceSeq > lastSeq ? 1 : 0;
}

// How a face that was asleep comes back. Waking is not one animation: being
// spoken to deserves a startle, being picked up deserves a look, and a shake with
// nothing behind it deserves a yawn.
enum BuddyWakeReaction {
  BUDDY_WAKE_YAWN = 0,  // dormido -> idle: short, heavy lids
  BUDDY_WAKE_ATTEND,    // dormido -> waiting: straight to the owner
  BUDDY_WAKE_STARTLE    // dormido -> thinking/tool/error: eyes wide, focused
};

static inline BuddyWakeReaction buddyWakeReaction(BuddyState from,
                                                  BuddyState to) {
  if (from != BUDDY_IDLE) return BUDDY_WAKE_YAWN;
  switch (to) {
    case BUDDY_THINKING:
    case BUDDY_TOOL:
    case BUDDY_ERROR:
      return BUDDY_WAKE_STARTLE;
    case BUDDY_WAITING:
      return BUDDY_WAKE_ATTEND;
    default:
      return BUDDY_WAKE_YAWN;
  }
}

// `tool -> done` is the job finishing: a win. `thinking -> done` is the waiting
// ending: relief. Anything else is just the checkmark.
enum BuddyDoneReaction {
  BUDDY_DONE_PLAIN = 0,
  BUDDY_DONE_RELIEF,
  BUDDY_DONE_WIN
};

static inline BuddyDoneReaction buddyDoneReaction(BuddyState prev,
                                                  uint32_t toolElapsedMs) {
  (void)toolElapsedMs;
  if (prev == BUDDY_TOOL) return BUDDY_DONE_WIN;
  if (prev == BUDDY_THINKING) return BUDDY_DONE_RELIEF;
  return BUDDY_DONE_PLAIN;
}

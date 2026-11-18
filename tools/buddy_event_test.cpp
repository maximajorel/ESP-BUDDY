// Host-side test for the /events stream rules.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_event_test.cpp -o /tmp/bet && /tmp/bet
//
// The bridge keeps a 64-event ring and identifies each one with a `seq`. The
// device is the reader with the cursor, so the rules that matter are: a `seq`
// already applied is dropped (a retried long-poll must not replay the face), a
// `seq` ahead of the server means the bridge restarted and the state has to be
// re-read, and the direction of a change is what separates relief from a win.

#include <cstdio>
#include <cstring>

#include "buddy_event.h"

static int g_failures = 0;

static void checkInt(const char *what, long got, long want) {
  if (got != want) {
    printf("FAIL %-56s got=%ld want=%ld\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-56s %ld\n", what, got);
  }
}

static void checkText(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-56s got=\"%s\" want=\"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-56s \"%s\"\n", what, got);
  }
}

int main() {
  printf("-- the three event names\n");
  checkInt("state", buddyEventKindFromName("state"), BUDDY_EVENT_STATE);
  checkInt("label", buddyEventKindFromName("label"), BUDDY_EVENT_LABEL);
  checkInt("ask.accepted", buddyEventKindFromName("ask.accepted"),
           BUDDY_EVENT_ASK_ACCEPTED);
  checkInt("an unknown event is ignored, not an error",
           buddyEventKindFromName("run.finished"), BUDDY_EVENT_UNKNOWN);
  checkInt("null", buddyEventKindFromName(nullptr), BUDDY_EVENT_UNKNOWN);
  checkInt("empty", buddyEventKindFromName(""), BUDDY_EVENT_UNKNOWN);

  printf("-- the cursor drops what was already applied\n");
  checkInt("first event", buddyEventApply(0, 1), 1);
  checkInt("the next one", buddyEventApply(1, 2), 1);
  checkInt("a retried long-poll replays nothing", buddyEventApply(2, 2), 0);
  checkInt("a stale event from the ring is dropped", buddyEventApply(9, 4), 0);
  checkInt("a jump forward is still applied once", buddyEventApply(3, 40), 1);

  printf("-- a cursor ahead of the server means the bridge restarted\n");
  checkInt("same seq is fine", buddyEventsNeedResync(7, 7), 0);
  checkInt("behind is fine", buddyEventsNeedResync(4, 9), 0);
  checkInt("ahead needs a /state", buddyEventsNeedResync(12, 3), 1);
  checkInt("an unknown server cursor (no /state yet) needs one",
           buddyEventsNeedResync(5, -1), 1);

  printf("-- waking goes with the direction of the change\n");
  checkInt("asleep into thinking is a startle",
           buddyWakeReaction(BUDDY_IDLE, BUDDY_THINKING), BUDDY_WAKE_STARTLE);
  checkInt("asleep into tool is a startle",
           buddyWakeReaction(BUDDY_IDLE, BUDDY_TOOL), BUDDY_WAKE_STARTLE);
  checkInt("asleep into error is a startle",
           buddyWakeReaction(BUDDY_IDLE, BUDDY_ERROR), BUDDY_WAKE_STARTLE);
  checkInt("asleep into waiting looks at the owner",
           buddyWakeReaction(BUDDY_IDLE, BUDDY_WAITING), BUDDY_WAKE_ATTEND);
  checkInt("still idle after a shake is the short yawn",
           buddyWakeReaction(BUDDY_IDLE, BUDDY_IDLE), BUDDY_WAKE_YAWN);
  checkInt("woken by `done` is the same soft landing",
           buddyWakeReaction(BUDDY_IDLE, BUDDY_DONE), BUDDY_WAKE_YAWN);

  printf("-- satisfaction depends on where the run came from\n");
  checkInt("tool -> done is a win", buddyDoneReaction(BUDDY_TOOL, 1000),
           BUDDY_DONE_WIN);
  checkInt("thinking -> done is relief", buddyDoneReaction(BUDDY_THINKING, 1000),
           BUDDY_DONE_RELIEF);
  checkInt("tool -> tool is `still going`, not a celebration",
           buddyDoneReaction(BUDDY_TOOL, 60000), BUDDY_DONE_WIN);
  checkInt("done from idle is just a check", buddyDoneReaction(BUDDY_IDLE, 0),
           BUDDY_DONE_PLAIN);
  checkInt("an unknown predecessor is a check too",
           buddyDoneReaction(BUDDY_UNCONFIGURED, 0), BUDDY_DONE_PLAIN);

  printf("-- the label is cleaned like every other label\n");
  char out[BUDDY_LABEL_MAX + 1];
  buddyLabelClean("usando la terminal", out, sizeof(out));
  checkText("a plain label survives", out, "usando la terminal");
  checkText("the wire name of the local sleep state is not a bridge name",
           buddyStateWireName(BUDDY_ASLEEP), "asleep");

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}

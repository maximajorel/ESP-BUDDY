// Host-side test for the buddy state and label rules.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_state_test.cpp -o /tmp/bst && /tmp/bst
//
// The bridge sends state names and a short label; the firmware maps them to a
// face and prints the label. An unknown state must not freeze the face, and a
// label full of control bytes or 500 characters must not run off the panel.

#include <cstdio>
#include <cstring>

#include "buddy_state.h"

static int g_failures = 0;

static void checkInt(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-52s got=%d want=%d\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-52s %d\n", what, got);
  }
}

static void checkText(const char *what, const char *got, const char *want) {
  bool same = strcmp(got, want) == 0;
  if (!same) {
    printf("FAIL %-52s got=\"%s\" want=\"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-52s \"%s\"\n", what, got);
  }
}

int main() {
  printf("-- state names from the bridge\n");
  checkInt("idle", buddyStateFromName("idle"), BUDDY_IDLE);
  // `bored` is gone from the contract: the bridge no longer orders a nap, and an
  // old bridge that still sends it must land on idle, not on some hidden state.
  checkInt("bored is no longer a wire name", buddyStateFromName("bored"), BUDDY_IDLE);
  checkInt("thinking", buddyStateFromName("thinking"), BUDDY_THINKING);
  checkInt("tool", buddyStateFromName("tool"), BUDDY_TOOL);
  checkInt("waiting", buddyStateFromName("waiting"), BUDDY_WAITING);
  checkInt("error", buddyStateFromName("error"), BUDDY_ERROR);
  checkInt("done", buddyStateFromName("done"), BUDDY_DONE);
  checkInt("an unknown state falls back to idle, not offline",
           buddyStateFromName("dancing"), BUDDY_IDLE);
  checkInt("a null state is idle", buddyStateFromName(nullptr), BUDDY_IDLE);
  checkInt("an empty state is idle", buddyStateFromName(""), BUDDY_IDLE);
  checkInt("names are case sensitive on purpose",
           buddyStateFromName("IDLE"), BUDDY_IDLE);

  printf("-- the console can pin the local-only sleep ladder\n");
  checkInt("asleep is not a wire name", buddyStateFromName("asleep"), BUDDY_IDLE);
  checkInt("but buddyface can still pin it", buddyFacePinFromName("asleep"),
           BUDDY_ASLEEP);
  checkInt("everything else stays the wire vocabulary",
           buddyFacePinFromName("tool"), BUDDY_TOOL);
  checkInt("and a null pin is idle", buddyFacePinFromName(nullptr), BUDDY_IDLE);

  printf("-- the wire name round-trips\n");
  checkText("idle", buddyStateWireName(BUDDY_IDLE), "idle");
  checkText("asleep never travels to the bridge", buddyStateWireName(BUDDY_ASLEEP),
            "asleep");
  checkText("tool", buddyStateWireName(BUDDY_TOOL), "tool");
  checkText("done", buddyStateWireName(BUDDY_DONE), "done");
  checkText("offline", buddyStateWireName(BUDDY_OFFLINE), "offline");

  printf("-- labels are trimmed, cleaned and cut\n");
  char out[BUDDY_LABEL_MAX + 8];
  buddyLabelClean("  Buscando precios  ", out, sizeof(out));
  checkText("outer spaces trimmed", out, "Buscando precios");
  buddyLabelClean("", out, sizeof(out));
  checkText("empty stays empty", out, "");
  buddyLabelClean(nullptr, out, sizeof(out));
  checkText("null becomes empty", out, "");
  buddyLabelClean("linea\ncon\ttabs", out, sizeof(out));
  checkText("control bytes do not reach the glass", out, "linea con tabs");
  buddyLabelClean("Este label es demasiado largo para la pantalla y no debe pasar de cuarenta",
                  out, sizeof(out));
  checkInt("the label is cut to the limit", (int)strlen(out), BUDDY_LABEL_MAX);
  buddyLabelClean("0123456789012345678901234567890123456789XYZ", out, sizeof(out));
  checkText("cut at exactly forty", out, "0123456789012345678901234567890123456789");
  buddyLabelClean("   ", out, sizeof(out));
  checkText("spaces only becomes empty", out, "");
  buddyLabelClean("ok", out, 2);
  checkText("a tiny buffer truncates safely", out, "o");

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}

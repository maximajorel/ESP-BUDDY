// Host-side test for the screen-name cleaning rules.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/screen_name_test.cpp -o /tmp/snt && /tmp/snt
//
// The web panel sends whatever the owner typed, and those bytes go straight into
// NVS and then onto a 5x7 panel. A control byte, a name that is only spaces, or
// a cut in the middle of a UTF-8 sequence would all show up as a broken picker,
// so the rules live in one pure function used by the firmware and pinned here.

#include <cstdio>
#include <cstring>

#include "screen_name.h"

static int g_failures = 0;

static void check(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-56s got=%d want=%d\n", what, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-56s %d\n", what, (int)got);
  }
}

static void checkText(const char *what, const char *got, const char *want) {
  bool same = strcmp(got, want) == 0;
  if (!same) {
    printf("FAIL %-56s got=\"%s\" want=\"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-56s \"%s\"\n", what, got);
  }
}

// Runs the cleaner over `in` and returns whether it accepted the name.
static bool clean(const char *in, char *out, size_t cap) {
  memset(out, 'X', cap);  // the output must be NUL-terminated on every path
  out[cap - 1] = '\0';
  return screenNameClean(in, out, cap);
}

int main() {
  char out[SCREEN_NAME_MAX + 8];

  check("a plain name is accepted", clean("RELOJ", out, sizeof(out)), true);
  checkText("a plain name is copied verbatim", out, "RELOJ");

  check("outer spaces are trimmed", clean("  Red  ", out, sizeof(out)), true);
  checkText("outer trim leaves the words", out, "Red");

  check("inner spaces survive", clean("MI RELOJ", out, sizeof(out)), true);
  checkText("inner spaces are kept", out, "MI RELOJ");

  check("a tab around the name is trimmed",
        clean("\tMUSICA\n", out, sizeof(out)), true);
  checkText("tab/newline trim leaves the word", out, "MUSICA");

  check("an empty name is rejected", clean("", out, sizeof(out)), false);
  check("a spaces-only name is rejected", clean("     ", out, sizeof(out)),
        false);
  check("a control byte inside is rejected",
        clean("RE\tLOJ", out, sizeof(out)), false);

  check("a null name is rejected", clean(nullptr, out, sizeof(out)), false);
  check("a one-byte buffer is rejected", clean("RELOJ", out, 1), false);

  check("the maximum length is accepted",
        clean("DOCE CARACTE", out, sizeof(out)), true);
  checkText("the maximum length is copied whole", out, "DOCE CARACTE");

  check("a longer name is truncated, not rejected",
        clean("TRAGAMONEDAS LARGAS", out, sizeof(out)), true);
  checkText("truncation stops at the limit", out, "TRAGAMONEDAS");

  check("a trailing space exposed by the cut is dropped",
        clean("ABCDEFGHIJK ", out, sizeof(out)), true);
  checkText("the cut drops the space", out, "ABCDEFGHIJK");

  // "ABCDEFGHIJK" is 11 ASCII bytes, the tilde is 2 more: byte 12 would be the
  // lead byte of a sequence that does not fit, so the cut must land before it.
  check("a UTF-8 cut never splits a character",
        clean("ABCDEFGHIJK\xC3\x91", out, sizeof(out)), true);
  checkText("the untilde name survives the boundary cut", out, "ABCDEFGHIJK");

  check("accents are stored as sent, for the panel",
        clean("M\xC3\x9ASICA", out, sizeof(out)), true);
  checkText("the accent bytes are untouched", out, "M\xC3\x9ASICA");

  check("an incomplete UTF-8 tail is rejected",
        clean("RELOJ\xC3", out, sizeof(out)), false);

  check("a small buffer still gets a valid name", clean("RELOJ", out, 4), true);
  checkText("the small buffer keeps what fits", out, "REL");

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}

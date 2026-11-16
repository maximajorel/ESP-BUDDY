// Host-side test for the UTF-8 -> ASCII fold used by the LCD text.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/text_fold_test.cpp \
//       esp32_gadget/text_fold.cpp -o /tmp/tft && /tmp/tft
//
// The panel font stops at ASCII 125, so an "EBÜ" used to draw two solid fallback
// blocks. Folding keeps the word readable; these are the rules that decide what
// the owner sees.

#include <cstdio>
#include <cstring>

#include "text_fold.h"

static int g_failures = 0;

static void checkStr(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-46s got=\"%s\" want=\"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-46s \"%s\"\n", what, got);
  }
}

static void fold(const char *what, const char *input, const char *want) {
  char out[128];
  textFoldAscii(input, out, sizeof(out));
  checkStr(what, out, want);
}

int main() {
  fold("dieresis", "EBÜ", "EBU");
  fold("acute on o", "Corazón", "Corazon");
  fold("enye", "Ñandú", "Nandu");
  fold("two accents", "Sigur Rós", "Sigur Ros");
  fold("umlaut", "Motörhead", "Motorhead");
  fold("ascii untouched", "Never Gonna Give You Up 42", "Never Gonna Give You Up 42");
  fold("en dash becomes hyphen", "Björk – Jóga", "Bjork - Joga");
  fold("curly quotes", "\u201CComillas\u201D", "\"Comillas\"");
  fold("inverted marks", "¿Qué?", "?Que?");
  fold("euro sign", "20€", "20E");
  fold("emoji has no stand-in", "a🎵b", "a b");
  fold("sharp s", "Straße", "Strasse"[0] ? "Strasse" : "");
  fold("empty", "", "");

  // A continuation byte with no lead byte is not valid UTF-8: say so with '?'
  // instead of silently dropping the character.
  {
    char out[16];
    const char broken[] = {'x', (char)0x80, 'y', '\0'};
    textFoldAscii(broken, out, sizeof(out));
    checkStr("stray continuation byte", out, "x?y");
  }

  // Truncation must never leave the buffer unterminated.
  {
    char out[4];
    memset(out, 'x', sizeof(out));
    size_t n = textFoldAscii("Corazón", out, sizeof(out));
    checkStr("truncated to the buffer", out, "Cor");
    printf("     wrote %u characters plus terminator\n", (unsigned)n);
  }

  // The fold must also survive a buffer of one, which is all terminator.
  {
    char out[1];
    size_t n = textFoldAscii("abc", out, sizeof(out));
    checkStr("cap of one stays empty", out, "");
    if (n != 0) { printf("FAIL cap of one wrote %u\n", (unsigned)n); g_failures++; }
  }

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
  return g_failures == 0 ? 0 : 1;
}

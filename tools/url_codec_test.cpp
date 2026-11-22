// Host-side test for the RFC 3986 encoder the firmware carries.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/url_codec_test.cpp esp32_gadget/url_codec.cpp -o /tmp/url && /tmp/url
//
// This rule has two consumers with opposite failure modes and, until now, two
// implementations: Spotify's returned void and wrote an empty string when the
// buffer did not fit, while the calendar's returned bool and refused. Both are
// the same RFC. The cases below are the union of what each one had to get right:
// a stray '+' becoming a space on the other end, a `%` that must not be re-escaped
// as a literal, and a buffer one byte too short failing instead of silently
// truncating into a different request.

#include <cstdio>
#include <cstring>

#include "url_codec.h"

static int g_failures = 0;

static void checkText(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-50s\n     got  \"%s\"\n     want \"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-50s \"%s\"\n", what, got);
  }
}

static void check(bool ok, bool want, const char *what) {
  if (ok != want) {
    printf("FAIL %-50s got=%d want=%d\n", what, (int)ok, (int)want);
    g_failures++;
  } else {
    printf("ok   %-50s %d\n", what, (int)ok);
  }
}

// Encodes into a buffer with room to spare and returns the result as text.
static const char *encode(const char *in, char *out, size_t cap) {
  urlEncode(in, out, cap);
  return out;
}

int main() {
  char out[128];

  printf("-- the unreserved set survives (RFC 3986 §2.3)\n");
  checkText("empty input", encode("", out, sizeof(out)), "");
  checkText("every unreserved character", encode("abc-._~019", out, sizeof(out)),
            "abc-._~019");
  checkText("letters, digits and the four marks", encode("AZaz09-._~", out, sizeof(out)),
            "AZaz09-._~");

  printf("-- everything else becomes %%XX, upper case\n");
  checkText("space is %20, never '+'", encode("a b", out, sizeof(out)), "a%20b");
  checkText("a literal '+' is escaped", encode("a+b", out, sizeof(out)), "a%2Bb");
  checkText("a literal '%' is escaped, not doubled", encode("100%", out, sizeof(out)),
            "100%25");
  checkText("reserved delimiters", encode("&=?/:", out, sizeof(out)),
            "%26%3D%3F%2F%3A");
  checkText("an @ does not survive", encode("a@b", out, sizeof(out)), "a%40b");
  checkText("a high byte travels intact", encode("\xC3\xA9", out, sizeof(out)),
            "%C3%A9");

  printf("-- the two callers that motivated the shared rule\n");
  checkText("a Google calendar id",
            encode("abc@group.calendar.google.com", out, sizeof(out)),
            "abc%40group.calendar.google.com");
  checkText("a Spotify redirect uri",
            encode("http://127.0.0.1:8888/callback", out, sizeof(out)),
            "http%3A%2F%2F127.0.0.1%3A8888%2Fcallback");
  checkText("a token with a slash and a plus",
            encode("dev/1+code", out, sizeof(out)), "dev%2F1%2Bcode");

  printf("-- every byte, at every capacity around its own boundary\n");
  {
    // The merge of the two encoders claims the escape table and the capacity
    // arithmetic are the same. This walks the whole table once, so a single
    // wrong class (a byte that stopped being escaped, or one that started) cannot
    // hide in a hand-picked list.
    int bad = 0;
    for (int c = 1; c < 256; c++) {
      char in[2] = {(char)c, 0};
      const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                         c == '_' || c == '~';
      const size_t need = plain ? 1 : 3;
      for (size_t cap = 1; cap <= 5; cap++) {
        char buf[8];
        memset(buf, 0x7f, sizeof(buf));
        const bool ok = urlEncode(in, buf, cap);
        const bool want = cap >= need + 1;
        if (ok != want) {
          printf("FAIL byte 0x%02X cap=%zu got=%d want=%d\n", c, cap, (int)ok,
                 (int)want);
          bad++;
          continue;
        }
        if (ok) {
          if (strlen(buf) != need) {
            printf("FAIL byte 0x%02X wrote %zu chars, want %zu\n", c, strlen(buf),
                   need);
            bad++;
          }
        } else if (strcmp(buf, "") != 0) {
          printf("FAIL byte 0x%02X left \"%s\" after refusing\n", c, buf);
          bad++;
        }
      }
    }
    check(bad == 0, true, "all 255 bytes escaped consistently at every capacity");
  }

  printf("-- the capacity contract\n");
  check(urlEncode("a b", out, 6), true, "exactly the five characters plus NUL");
  checkText("  and it fits", out, "a%20b");
  check(urlEncode("a b", out, 5), false, "one byte short is refused");
  checkText("  and nothing is left behind", out, "");
  check(urlEncode("primary", out, 8), true, "exactly 'primary' plus NUL");
  check(urlEncode("primary", out, 7), false, "'primary' without NUL is refused");
  check(urlEncode("abc@group.calendar.google.com", out, 8), false,
        "a short buffer never truncates");
  check(urlEncode("", out, 1), true, "an empty string fits a one-byte buffer");

  printf("-- null arguments are refused, not crashed on\n");
  check(urlEncode("abc", nullptr, 0), false, "a null output with no capacity");
  check(urlEncode(nullptr, out, sizeof(out)), false, "a null input");
  checkText("  and the buffer was cleared", out, "");

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}

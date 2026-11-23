// Host-side test for the query-string reader the console shares with HTTP.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/net_query_test.cpp \
//       -o /tmp/net_query && /tmp/net_query
//
// The month endpoint is the first parameterized route on this board, and the only
// way to ask for it without a panel session is the serial console, which pastes
// the query onto the path. So the reader has to be exactly right:
//  1. a key matches whole: "yy" is not "y" and "year" is not "y";
//  2. a parameter in the middle of the string is found;
//  3. a value that is not a whole number, or a value out of the caller's range, is
//     a miss — never a silent 0, which would be the wrong month;
//  4. a trailing separator or an empty query is a miss, not a crash.

#include <cstdio>
#include <cstring>

#include "net_query.h"

static int g_failures = 0;

static void checkMiss(const char *query, const char *key, long min, long max) {
  long value = -1;
  const bool found = queryInt(query, key, min, max, &value);
  if (found) {
    printf("FAIL %-46s found=%ld in \"%s\"\n", key, value, query ? query : "(null)");
    g_failures++;
  } else {
    printf("ok   %-46s miss in \"%s\"\n", key, query ? query : "(null)");
  }
}

static void checkHit(const char *query, const char *key, long min, long max,
                     long want) {
  long value = -1;
  if (!queryInt(query, key, min, max, &value) || value != want) {
    printf("FAIL %-46s got=%ld want=%ld in \"%s\"\n", key, value, want,
           query ? query : "(null)");
    g_failures++;
  } else {
    printf("ok   %-46s %ld in \"%s\"\n", key, value, query ? query : "(null)");
  }
}

int main() {
  printf("-- a key matches whole\n");
  checkHit("y=2026", "y", 2020, 2199, 2026);
  checkHit("y=2026&m=11", "y", 2020, 2199, 2026);
  checkHit("y=2026&m=11", "m", 1, 12, 11);
  checkMiss("yy=2026", "y", 2020, 2199);
  checkMiss("year=2026", "y", 2020, 2199);
  checkHit("yy=5&y=2026", "y", 2020, 2199, 2026);
  checkHit("a=1&y=2026&b=2", "y", 2020, 2199, 2026);

  printf("-- a value that is not a whole number in range is a miss\n");
  checkMiss("y=", "y", 2020, 2199);
  checkMiss("y=abc", "y", 2020, 2199);
  checkMiss("y=2026x", "y", 2020, 2199);
  checkMiss("y=2019", "y", 2020, 2199);
  checkMiss("m=13", "m", 1, 12);
  checkMiss("m=0", "m", 1, 12);
  checkHit("m=12", "m", 1, 12, 12);
  checkHit("m=-1", "m", -2, 12, -1);
  checkMiss("y=999999999999", "y", 2020, 2199);

  printf("-- nothing to read is a miss, not a crash\n");
  checkMiss(nullptr, "y", 2020, 2199);
  checkMiss("", "y", 2020, 2199);
  checkMiss("&", "y", 2020, 2199);
  checkMiss("y=2026&", "m", 1, 12);
  checkMiss("y=2026", nullptr, 2020, 2199);
  checkMiss("y=2026", "", 2020, 2199);

  printf("\n%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

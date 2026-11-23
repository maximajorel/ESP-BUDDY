// Query-string parameters without a web server.
//
// The same endpoint is reached from two places with different furniture: the HTTP
// server hands its parameters through `WebServer::arg`, and the serial console —
// the only way to ask the board something without opening a panel session —
// pastes the query straight onto the path. Both have to mean the same thing, so
// the reading lives here, pure and pinned by `tools/net_query_test.cpp`.
//
// Deliberately strict: a value that is not a whole number in range is a miss, not
// a zero. A month of "0" would land on a January nobody asked for.
#pragma once

#include <stdint.h>
#include <string.h>

// True when `query` has `key=<integer>` and the value fits `min..max`.
// `key` matches whole: "yy" is not "y", and "y" is not "year".
static inline bool queryInt(const char *query, const char *key, long min,
                            long max, long *out) {
  if (!query || !key || !*key || !out) return false;
  const size_t keyLen = strlen(key);
  const char *p = query;
  while (*p) {
    // Each parameter starts at the beginning or right after a separator.
    if (p != query && p[-1] != '&') {
      p++;
      continue;
    }
    if (strncmp(p, key, keyLen) != 0 || p[keyLen] != '=') {
      // Skip this whole parameter, not just one character: "yy=5&y=7" must still
      // find the "y".
      const char *next = strchr(p, '&');
      if (!next) return false;
      p = next + 1;
      continue;
    }
    const char *value = p + keyLen + 1;
    bool negative = false;
    if (*value == '-') {
      negative = true;
      value++;
    }
    if (*value < '0' || *value > '9') return false;
    long parsed = 0;
    while (*value >= '0' && *value <= '9') {
      parsed = parsed * 10 + (*value - '0');
      if (parsed > 100000000L) return false;  // a year, not a phone number
      value++;
    }
    // Anything after the digits that is not the next separator makes the value a
    // typo: "y=2026x" is not the year 2026.
    if (*value && *value != '&') return false;
    if (negative) parsed = -parsed;
    if (parsed < min || parsed > max) return false;
    *out = parsed;
    return true;
  }
  return false;
}

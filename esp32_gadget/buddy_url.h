// The bridge address, parsed instead of blindly concatenated.
//
// The owner pastes this value into the panel by hand, and it now arrives in two
// shapes: the plain-HTTP LAN address the firmware always had and the public
// HTTPS URL a Cloudflare tunnel exposes. Both live in the same setting, so the
// parser has to say which transport a string asks for, normalise it once (the
// panel and the settings API store what this returns), and refuse anything that
// could only ever fail later with a face that says "no signal" and no reason.
//
// Pure and header-only so `tools/buddy_url_test.cpp` pins it on the host.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Same budget as SETTINGS_BUDDY_URL_MAX: the value that reaches NVS never
// exceeds what the setting can hold.
#define BUDDY_URL_MAX 96
#define BUDDY_URL_HOST_MAX 64

enum BuddyUrlError {
  BUDDY_URL_OK = 0,
  BUDDY_URL_EMPTY,     // nothing to store
  BUDDY_URL_NO_SCHEME,  // "192.168.100.64:8787": the scheme is not optional
  BUDDY_URL_BAD_SCHEME,  // ftp:// and friends
  BUDDY_URL_NO_HOST,
  BUDDY_URL_BAD_HOST,
  BUDDY_URL_BAD_PORT,
  BUDDY_URL_BAD_PATH,  // a base path with a space, a query or a fragment
  BUDDY_URL_TOO_LONG
};

struct BuddyUrl {
  bool tls = false;
  bool explicitPort = false;  // the owner typed the port: send it back verbatim
  uint16_t port = 0;          // effective port, defaults applied
  char host[BUDDY_URL_HOST_MAX] = {0};  // no brackets, even for a literal
  // scheme://host[:port] and any base path, without a trailing slash. This is
  // what gets stored, so a re-read of NVS parses to the same struct.
  char base[BUDDY_URL_MAX + 1] = {0};
};

static inline const char *buddyUrlErrorName(BuddyUrlError err) {
  switch (err) {
    case BUDDY_URL_OK: return "ok";
    case BUDDY_URL_NO_SCHEME: return "no-scheme";
    case BUDDY_URL_BAD_SCHEME: return "bad-scheme";
    case BUDDY_URL_NO_HOST: return "no-host";
    case BUDDY_URL_BAD_HOST: return "bad-host";
    case BUDDY_URL_BAD_PORT: return "bad-port";
    case BUDDY_URL_BAD_PATH: return "bad-path";
    case BUDDY_URL_TOO_LONG: return "too-long";
    case BUDDY_URL_EMPTY: break;
  }
  return "empty";
}

static inline bool buddyUrlIsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// The device's libc and the host's disagree about where strncasecmp lives, and
// this header has to compile in both places.
static inline bool buddyUrlEqualsIgnoreCase(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return *a == '\0' && *b == '\0';
}

// A host may not contain anything that would break the request line: no
// spaces, no control bytes, no separators. Brackets are stripped before this
// runs, and a bare ':' is already the port split.
static inline bool buddyUrlHostUsable(const char *host) {
  if (!host || !*host) return false;
  for (const char *p = host; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c <= 0x20 || c == 0x7F) return false;
    if (c == '/' || c == '\\' || c == '?' || c == '#' || c == '@') return false;
    if (c == '[' || c == ']') return false;
  }
  return true;
}

// Parses and normalises. `out` is only written when the result is BUDDY_URL_OK.
static inline BuddyUrlError buddyUrlParse(const char *in, BuddyUrl *out) {
  if (!out) return BUDDY_URL_EMPTY;
  if (!in) return BUDDY_URL_EMPTY;

  const char *begin = in;
  while (*begin && buddyUrlIsSpace(*begin)) begin++;
  const char *end = begin + strlen(begin);
  while (end > begin && buddyUrlIsSpace(end[-1])) end--;
  size_t len = (size_t)(end - begin);
  if (len == 0) return BUDDY_URL_EMPTY;
  if (len > BUDDY_URL_MAX) return BUDDY_URL_TOO_LONG;

  // Scheme. The separator is required: without it the value cannot be used and
  // guessing would hide the typo.
  const char *sep = nullptr;
  for (size_t i = 0; i + 2 < len; i++) {
    if (begin[i] == ':' && begin[i + 1] == '/' && begin[i + 2] == '/') {
      sep = begin + i;
      break;
    }
  }
  if (!sep) return BUDDY_URL_NO_SCHEME;
  size_t schemeLen = (size_t)(sep - begin);
  if (schemeLen == 0) return BUDDY_URL_BAD_SCHEME;

  // The scheme is compared through a bounded copy: `begin` is a slice of the
  // input, not a NUL-terminated string of its own.
  char scheme[6];
  if (schemeLen >= sizeof(scheme)) return BUDDY_URL_BAD_SCHEME;
  memcpy(scheme, begin, schemeLen);
  scheme[schemeLen] = '\0';
  bool tls;
  if (buddyUrlEqualsIgnoreCase(scheme, "http")) {
    tls = false;
  } else if (buddyUrlEqualsIgnoreCase(scheme, "https")) {
    tls = true;
  } else {
    return BUDDY_URL_BAD_SCHEME;
  }

  const char *afterScheme = sep + 3;
  size_t rest = (size_t)(end - afterScheme);
  if (rest == 0) return BUDDY_URL_NO_HOST;
  const char *slash = (const char *)memchr(afterScheme, '/', rest);
  size_t authLen = slash ? (size_t)(slash - afterScheme) : rest;
  if (authLen == 0) return BUDDY_URL_NO_HOST;

  // A base path is legitimate (a tunnel can mount the bridge under one) but the
  // trailing slash never is: every endpoint adds its own.
  const char *path = slash ? slash : afterScheme + authLen;
  size_t pathLen = (size_t)(end - path);
  while (pathLen > 0 && path[pathLen - 1] == '/') pathLen--;
  // Every endpoint appends its own path, so a base that already carries a query,
  // a fragment or a space can only produce a malformed request line. Reject it
  // here instead of sending "/a b/c/state" and calling the bridge silent.
  for (size_t i = 0; i < pathLen; i++) {
    unsigned char c = (unsigned char)path[i];
    if (c <= 0x20 || c == 0x7F || c == '?' || c == '#') return BUDDY_URL_BAD_PATH;
  }

  char host[BUDDY_URL_HOST_MAX];
  bool bracketed = false;
  const char *portText = nullptr;
  size_t portLen = 0;
  if (afterScheme[0] == '[') {
    bracketed = true;
    const char *close = (const char *)memchr(afterScheme, ']', authLen);
    if (!close) return BUDDY_URL_BAD_HOST;
    size_t hostLen = (size_t)(close - afterScheme - 1);
    if (hostLen == 0 || hostLen >= sizeof(host)) return BUDDY_URL_BAD_HOST;
    memcpy(host, afterScheme + 1, hostLen);
    host[hostLen] = '\0';
    const char *tail = close + 1;
    size_t tailLen = (size_t)((afterScheme + authLen) - tail);
    if (tailLen > 0) {
      if (*tail != ':') return BUDDY_URL_BAD_HOST;
      portText = tail + 1;
      portLen = tailLen - 1;
    }
  } else {
    const char *colon = (const char *)memchr(afterScheme, ':', authLen);
    size_t hostLen = colon ? (size_t)(colon - afterScheme) : authLen;
    if (hostLen == 0) return BUDDY_URL_NO_HOST;
    if (hostLen >= sizeof(host)) return BUDDY_URL_TOO_LONG;
    memcpy(host, afterScheme, hostLen);
    host[hostLen] = '\0';
    if (colon) {
      portText = colon + 1;
      portLen = authLen - hostLen - 1;
    }
  }
  if (!buddyUrlHostUsable(host)) return BUDDY_URL_BAD_HOST;

  bool explicitPort = portText != nullptr;
  long port = tls ? 443 : 80;
  if (explicitPort) {
    if (portLen == 0) return BUDDY_URL_BAD_PORT;
    long value = 0;
    for (size_t i = 0; i < portLen; i++) {
      char c = portText[i];
      if (c < '0' || c > '9') return BUDDY_URL_BAD_PORT;
      value = value * 10 + (c - '0');
      if (value > 65535) return BUDDY_URL_BAD_PORT;
    }
    if (value <= 0) return BUDDY_URL_BAD_PORT;
    port = value;
  }

  BuddyUrl parsed;
  parsed.tls = tls;
  parsed.explicitPort = explicitPort;
  parsed.port = (uint16_t)port;
  snprintf(parsed.host, sizeof(parsed.host), "%s", host);

  int n = snprintf(parsed.base, sizeof(parsed.base), "%s://%s%s%s", tls ? "https" : "http",
                   bracketed ? "[" : "", host, bracketed ? "]" : "");
  if (n < 0 || (size_t)n >= sizeof(parsed.base)) return BUDDY_URL_TOO_LONG;
  size_t used = (size_t)n;
  if (explicitPort) {
    n = snprintf(parsed.base + used, sizeof(parsed.base) - used, ":%u",
                 (unsigned)parsed.port);
    if (n < 0 || (size_t)n >= sizeof(parsed.base) - used) return BUDDY_URL_TOO_LONG;
    used += (size_t)n;
  }
  if (pathLen > 0) {
    if (pathLen >= sizeof(parsed.base) - used) return BUDDY_URL_TOO_LONG;
    memcpy(parsed.base + used, path, pathLen);
    parsed.base[used + pathLen] = '\0';
  }

  *out = parsed;
  return BUDDY_URL_OK;
}

// base + path, for the two endpoints the firmware calls. Returns false when the
// buffer cannot hold the whole string: a truncated URL must never be sent.
static inline bool buddyUrlJoin(const BuddyUrl &url, const char *path, char *out,
                                size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  if (!path) path = "";
  int n = snprintf(out, cap, "%s%s", url.base, path);
  return n >= 0 && (size_t)n < cap;
}

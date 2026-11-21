// The requests Google expects, built as strings.
//
// Pure and header-only, so the exact bytes that leave the board are pinned by
// `tools/calendar_core_test.cpp` instead of being discovered in a packet trace
// after a failed authorization. The device flow is used on purpose: it needs no
// public redirect URI, which this board cannot offer, and it gives the owner a
// short code to type on any phone or computer.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "calendar_model.h"
#include "calendar_time.h"

#define CALENDAR_OAUTH_HOST "oauth2.googleapis.com"
#define CALENDAR_DEVICE_CODE_PATH "/device/code"
#define CALENDAR_TOKEN_PATH "/token"
#define CALENDAR_API_HOST "www.googleapis.com"
#define CALENDAR_LIST_PATH                                                   \
  "/calendar/v3/users/me/calendarList?maxResults=50"                         \
  "&fields=items(id,summary,primary,defaultReminders)"
#define CALENDAR_SCOPE "https://www.googleapis.com/auth/calendar.readonly"

// RFC 3986 unreserved characters stay, everything else becomes %XX in upper
// case. Calendar ids contain `@` and refresh tokens contain `/` and `+`, so this
// is not cosmetic: an unescaped `+` becomes a space on Google's side.
static inline bool calendarUrlEncode(const char *in, char *out, size_t cap) {
  if (!in || !out || cap == 0) return false;
  static const char kHex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (const char *p = in; *p; p++) {
    const unsigned char c = (unsigned char)*p;
    const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                      c == '.' || c == '~';
    if (safe) {
      if (n + 1 >= cap) return false;
      out[n++] = (char)c;
    } else {
      if (n + 3 >= cap) return false;
      out[n++] = '%';
      out[n++] = kHex[c >> 4];
      out[n++] = kHex[c & 0x0F];
    }
  }
  out[n] = '\0';
  return true;
}

static inline bool calendarDeviceCodeForm(const char *clientId, char *out,
                                          size_t cap) {
  if (!clientId || !out) return false;
  char id[CALENDAR_ID_MAX * 3 + 4];
  char scope[sizeof(CALENDAR_SCOPE) * 3];
  if (!calendarUrlEncode(clientId, id, sizeof(id))) return false;
  if (!calendarUrlEncode(CALENDAR_SCOPE, scope, sizeof(scope))) return false;
  const int n = snprintf(out, cap, "client_id=%s&scope=%s", id, scope);
  return n > 0 && (size_t)n < cap;
}

// `grant_type=urn:ietf:params:oauth:grant-type:device_code` is what tells Google
// this is a device-flow poll and not an authorization-code exchange.
static inline bool calendarTokenFormDevice(const char *clientId,
                                           const char *clientSecret,
                                           const char *deviceCode, char *out,
                                           size_t cap) {
  if (!clientId || !clientSecret || !deviceCode || !out) return false;
  char id[CALENDAR_ID_MAX * 3 + 4];
  char secret[CALENDAR_ID_MAX * 3 + 4];
  char code[CALENDAR_ID_MAX * 3 + 4];
  if (!calendarUrlEncode(clientId, id, sizeof(id))) return false;
  if (!calendarUrlEncode(clientSecret, secret, sizeof(secret))) return false;
  if (!calendarUrlEncode(deviceCode, code, sizeof(code))) return false;
  const int n = snprintf(
      out, cap,
      "client_id=%s&client_secret=%s&device_code=%s&grant_type="
      "urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code",
      id, secret, code);
  return n > 0 && (size_t)n < cap;
}

static inline bool calendarTokenFormRefresh(const char *clientId,
                                            const char *clientSecret,
                                            const char *refreshToken,
                                            char *out, size_t cap) {
  if (!clientId || !clientSecret || !refreshToken || !out) return false;
  char id[CALENDAR_ID_MAX * 3 + 4];
  char secret[CALENDAR_ID_MAX * 3 + 4];
  char token[512 * 3 + 4];
  if (!calendarUrlEncode(clientId, id, sizeof(id))) return false;
  if (!calendarUrlEncode(clientSecret, secret, sizeof(secret))) return false;
  if (!calendarUrlEncode(refreshToken, token, sizeof(token))) return false;
  const int n = snprintf(out, cap,
                         "client_id=%s&client_secret=%s&refresh_token=%s"
                         "&grant_type=refresh_token",
                         id, secret, token);
  return n > 0 && (size_t)n < cap;
}

// The events page: one day forward and a few hours back, so an event that is
// running right now is still in the answer. `fields` keeps the body small enough
// for the board's heap; `singleEvents` expands recurrences, which is what makes
// per-instance reminders available.
static inline bool calendarEventsPath(const char *calendarId,
                                      int64_t timeMin, int64_t timeMax,
                                      char *out, size_t cap) {
  if (!calendarId || !out) return false;
  char id[CALENDAR_ID_MAX * 3 + 4];
  if (!calendarUrlEncode(calendarId, id, sizeof(id))) return false;
  char from[24];
  char to[24];
  if (!calendarRfc3339FromEpoch(timeMin, from, sizeof(from))) return false;
  if (!calendarRfc3339FromEpoch(timeMax, to, sizeof(to))) return false;
  const int n = snprintf(
      out, cap,
      "/calendar/v3/calendars/%s/events?timeMin=%s&timeMax=%s"
      "&singleEvents=true&orderBy=startTime&maxResults=%d"
      "&fields=items(id,status,summary,location,start,end,reminders)",
      id, from, to, CALENDAR_MAX_EVENTS);
  return n > 0 && (size_t)n < cap;
}

// HTTPS to Google, with its own client.
//
// Deliberately not shared with the bridge's transport or Spotify's: the buddy
// already corrupted the heap once when two tasks tore down one TLS client, and
// the calendar task talks to a different host with different headers. One
// client, one mutex, three requests per refresh (token, list, events), and the
// session is reused between them.
//
// Certificates are validated against the bundle compiled into the core, the same
// way Spotify and the bridge do it: Google's chain is there, so there is no PEM
// to upload and nothing to renew.
#pragma once

#include <Arduino.h>

// `transportError` when the answer was larger than the caller allowed. The body
// was already downloaded by then, so it is dropped instead of parsed in halves.
#define CALENDAR_HTTP_BODY_TOO_LARGE (-1000)

struct CalendarHttpResult {
  int status = 0;         // 0 when no status line arrived
  int transportError = 0;  // HTTPClient's negative code, for the panel
  bool tlsFailed = false;
  String body;
};

void calendarHttpBegin();

// GET a JSON document. Returns true when a status line arrived, whatever it was;
// `out->status` says which. `maxBody` refuses an answer that would not fit the
// board instead of parsing half of it.
bool calendarHttpGet(const char *host, const char *path, const char *bearer,
                     size_t maxBody, CalendarHttpResult *out);

// POST an `application/x-www-form-urlencoded` body. Used by the OAuth endpoints,
// which accept nothing else.
bool calendarHttpPostForm(const char *host, const char *path, const String &form,
                          size_t maxBody, CalendarHttpResult *out);

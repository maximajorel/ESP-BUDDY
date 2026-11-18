// The bridge transport: plain HTTP on the LAN, HTTPS through the public URL.
//
// The scheme decides the client, not the caller. `HTTPClient::begin(String)` on
// this core silently calls `setInsecure()` for an https URL (it builds a
// `TLSTraits(NULL)`), which would leave the buddy token open to anyone on the
// path; `spotify_http.h` already refuses that, and the bridge is now exposed to
// the internet, so it has to be refused here too.
//
// TLS is validated against the bundle compiled into the core, which carries GTS
// Root R1/R4 and ISRG Root X1 — the roots Cloudflare's edge serves. That is what
// lets the owner configure nothing but the URL and the token: no PEM to upload,
// no expiry to watch.
#pragma once

#include <Arduino.h>

#include "buddy_url.h"

// One poll answer. `status` is 0 when no status line ever arrived, which is why
// `transportError` and `tlsFailed` exist: they separate "nobody answered" from
// "somebody answered and refused the certificate".
struct BuddyHttpResult {
  int status = 0;
  int transportError = 0;
  bool tlsFailed = false;
  bool redirected = false;
  char detail[96] = {0};  // mbedtls error text, when there is one
  String body;
};

// Prepares the transport (the shared clients' lock). Called once at boot.
void buddyHttpBegin();

// GET <base>/state. The body is kept only for a 200.
void buddyHttpGetState(const BuddyUrl &url, const char *token, BuddyHttpResult *out);

// Longest `wait` the bridge accepts, and the margin the socket gets on top of it.
// The client must always outlast the server, or every long-poll dies as a
// transport error right before the answer would have arrived.
#define BUDDY_EVENTS_MAX_WAIT_SEC 25
#define BUDDY_EVENTS_MARGIN_SEC 10

// GET <base>/events?since=<seq>&wait=<sec>. `[]` for a 200 means "nothing new in
// `wait` seconds" (or a cursor the bridge no longer knows) and is not an error.
// A 404 means a bridge that predates the endpoint.
void buddyHttpGetEvents(const BuddyUrl &url, const char *token, int since,
                        int waitSec, BuddyHttpResult *out);

// POST <base>/buddy/ask with `json` as the body. The bridge answers 202 and runs
// asynchronously, so no body is kept.
void buddyHttpAsk(const BuddyUrl &url, const char *token, const String &json,
                  BuddyHttpResult *out);

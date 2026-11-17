// Why the bridge poll ended the way it did.
//
// The face has one "no signal" look and that is all the glass can say, but the
// panel can do better: "the token is wrong", "the clock has not synced yet" and
// "Cloudflare challenged the request" are three different things the owner fixes
// in three different places. The classifier is pure and header-only so
// `tools/buddy_link_test.cpp` pins the precedence, which is the part that is easy
// to get wrong: a missing clock hides a transport error, and a Cloudflare block
// looks like any other error status from the outside.
#pragma once

#include <stdint.h>

enum BuddyLink {
  BUDDY_LINK_OK = 0,
  BUDDY_LINK_UNCONFIGURED,  // no URL or no token yet
  BUDDY_LINK_BAD_URL,       // stored value is not a usable URL
  BUDDY_LINK_NO_TIME,       // https without a synced clock: validation cannot pass
  BUDDY_LINK_TLS,           // the handshake was rejected (certificate or hostname)
  BUDDY_LINK_BLOCKED,       // 403 / challenge / an Access login redirect
  BUDDY_LINK_UNAUTHORIZED,  // 401: the buddy token is wrong or missing
  BUDDY_LINK_HTTP,          // the bridge answered an error status
  BUDDY_LINK_OFFLINE,       // it never answered
  BUDDY_LINK_COUNT
};

// What the transport observed for one attempt. Kept as plain facts so the
// classifier never has to know about HTTPClient or mbedTLS types.
struct BuddyLinkFacts {
  bool configured;   // a URL and a token are stored
  bool urlValid;     // the stored URL parsed
  bool tls;          // the URL asks for https
  bool timeValid;    // the clock passed the SNTP sync
  int status;        // HTTP status, 0 when the request never completed
  bool tlsFailed;    // the handshake itself failed
  bool redirected;   // a 3xx: Cloudflare Access sends the device to a login page
};

static inline const char *buddyLinkWireName(BuddyLink link) {
  switch (link) {
    case BUDDY_LINK_OK: return "ok";
    case BUDDY_LINK_UNCONFIGURED: return "unconfigured";
    case BUDDY_LINK_BAD_URL: return "bad-url";
    case BUDDY_LINK_NO_TIME: return "no-time";
    case BUDDY_LINK_TLS: return "tls";
    case BUDDY_LINK_BLOCKED: return "blocked";
    case BUDDY_LINK_UNAUTHORIZED: return "unauthorized";
    case BUDDY_LINK_HTTP: return "http";
    case BUDDY_LINK_COUNT: break;
  }
  return "offline";
}

static inline BuddyLink buddyLinkFromFacts(const BuddyLinkFacts &facts) {
  if (!facts.configured) return BUDDY_LINK_UNCONFIGURED;
  if (!facts.urlValid) return BUDDY_LINK_BAD_URL;
  // Checked before the request is even attempted: without a clock the
  // certificate looks not-yet-valid and the failure would be blamed on the
  // network instead of on time.
  if (facts.tls && !facts.timeValid) return BUDDY_LINK_NO_TIME;
  if (facts.tlsFailed) return BUDDY_LINK_TLS;
  if (facts.redirected || facts.status == 302 || facts.status == 303 ||
      facts.status == 307 || facts.status == 308) {
    return BUDDY_LINK_BLOCKED;
  }
  if (facts.status == 403) return BUDDY_LINK_BLOCKED;
  if (facts.status == 401) return BUDDY_LINK_UNAUTHORIZED;
  if (facts.status >= 200 && facts.status < 300) return BUDDY_LINK_OK;
  if (facts.status > 0) return BUDDY_LINK_HTTP;
  return BUDDY_LINK_OFFLINE;
}

// Minimal HTTPS transport for the Spotify hosts.
//
// One shared, reused TLS client: every Spotify call is a handshake otherwise,
// and a handshake on this board costs ~40 KB of heap for a second and a half.
// The certificate chain is validated against the bundle compiled into the
// Arduino core (`useBuiltinCACertBundle`), so the firmware ships no PEM that can
// silently expire, and `setInsecure` is never used.
#pragma once

#include <Arduino.h>

struct SpotifyHttpResult {
  int status;              // HTTP status, or 0 when the request never completed
  int transportError;      // HTTPClient error code when status is 0
  long retryAfterSeconds;  // from the 429's Retry-After, 0 when absent
  String body;
};

// GET a path on `host`. `bearer` may be null; `maxBody` caps what is kept.
void spotifyHttpGet(const char *host, const char *path, const char *bearer,
                    size_t maxBody, SpotifyHttpResult *out);

// POST an application/x-www-form-urlencoded body (the token endpoints).
void spotifyHttpPostForm(const char *host, const char *path, const String &form,
                         SpotifyHttpResult *out);

// Fetches a whole URL into `out` as bytes, for artwork. Returns false when the
// response was not a 200 or exceeded `maxBytes`.
bool spotifyHttpGetBytes(const char *url, uint8_t *out, size_t maxBytes,
                         size_t *outLen, int *statusOut);

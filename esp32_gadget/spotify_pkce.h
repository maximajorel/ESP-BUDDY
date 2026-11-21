// PKCE (RFC 7636) pieces for the Spotify authorization code flow.
//
// The digest those pieces need is not here: SHA-256 has two consumers in this
// firmware and `sha256.h` owns it. This file keeps only what the PKCE flow itself
// defines — the challenge encoding, the verifier rule and the code extraction.
//
// No Arduino and no mbedtls includes: the firmware and
// `tools/spotify_core_test.cpp` compile this same file, so the challenge the
// panel hands to Spotify is the one pinned by the RFC's published test vector.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum SpotifyPkceResult {
  SPOTIFY_PKCE_OK = 0,
  SPOTIFY_PKCE_INVALID_VERIFIER = 1,
  SPOTIFY_PKCE_BUFFER_TOO_SMALL = 2,
};

// Base64url without padding. Writes "" when `outCap` is too small.
void spotifyBase64UrlEncode(const uint8_t *data, size_t len, char *out,
                            size_t outCap);

// Percent-encodes for a URL query and an application/x-www-form-urlencoded body
// (space becomes %20, not '+'). Writes "" when `outCap` is too small.
void spotifyUrlEncode(const char *in, char *out, size_t outCap);

enum SpotifyCodeResult {
  SPOTIFY_CODE_OK = 0,
  SPOTIFY_CODE_EMPTY = 1,               // nothing usable in what was pasted
  SPOTIFY_CODE_DENIED = 2,              // the URL carried error=access_denied
  SPOTIFY_CODE_BUFFER_TOO_SMALL = 3,    // a longer code than the caller allows
};

// Accepts what a person actually copies out of the browser after approving: the
// whole redirect URL, just the `code=...` fragment, or the bare code. Percent
// escapes are decoded. Lives in the firmware rather than only in the panel so the
// serial console behaves the same way.
SpotifyCodeResult spotifyExtractCode(const char *input, char *out,
                                    size_t outCap);

// SHA-256 (FIPS 180-4) lives in `sha256.h`; the S256 challenge below is
// BASE64URL of that digest.

// RFC 7636: 43..128 characters of the unreserved set.
bool spotifyPkceVerifierValid(const char *verifier);

// code_challenge = BASE64URL(SHA256(verifier)). The 64px buffer holds the
// 43-character challenge plus its terminator.
SpotifyPkceResult spotifyPkceChallengeFromDigest(const uint8_t digest[32],
                                                 char *out, size_t outCap);
SpotifyPkceResult spotifyPkceChallenge(const char *verifier, char *out,
                                       size_t outCap);

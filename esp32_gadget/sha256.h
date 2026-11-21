// SHA-256 (FIPS 180-4), with no Arduino and no mbedtls.
//
// Two different subsystems need this digest and neither owns it: the Spotify PKCE
// flow builds its S256 challenge from it, and the web panel's password is an
// iterated digest of the owner's secret. It lives here so `admin_auth` does not
// have to include a file named after Spotify to hash a password, and so
// `tools/sha256_test.cpp` can pin it against the published vectors on the host.
//
// mbedtls ships with the core, but reaching it would tie the host tests to the
// ESP32 toolchain, which is the one thing the pure layer exists to avoid.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Writes the 32-byte digest of `len` bytes at `data`. A null `data` with a zero
// `len` is the empty message, which hashes to a well-known value rather than
// being a special case the callers have to guard.
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

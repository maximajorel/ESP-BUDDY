// Percent-encoding for URLs and form bodies (RFC 3986), with no Arduino.
//
// Two features needed exactly this rule and each carried its own copy: the
// Spotify PKCE flow escapes the authorize query and the token body, and Google
// Calendar escapes client ids and refresh tokens — which contain `/` and `+` and
// become a different request if they are not escaped. The rule is one rule, so it
// lives here and `tools/url_codec_test.cpp` pins it.
//
// Space becomes %20 and never '+': the same string is used in a query and in an
// `application/x-www-form-urlencoded` body, and only %20 means the same thing in
// both. Everything outside the unreserved set is escaped in upper-case hex.
#pragma once

#include <stddef.h>

// Writes the encoded form of `in` into `out`. Returns false — and leaves `out`
// empty — when the result would not fit `cap` bytes including the terminator, or
// when either pointer is null. Truncating instead would silently produce a
// different request than the one the caller meant to send.
bool urlEncode(const char *in, char *out, size_t cap);

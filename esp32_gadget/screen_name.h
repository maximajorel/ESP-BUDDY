// Cleaning rules for owner-chosen screen names.
//
// Pure and header-only so the firmware and `tools/screen_name_test.cpp` run the
// exact same code. The input arrives from the web panel, is persisted in NVS and
// is then drawn by the 5x7 panel font, so three things have to be decided in one
// place: the length limit, what counts as whitespace, and how to cut a UTF-8
// name without splitting a character.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Longest stored name in bytes, NUL excluded. Twelve characters still fit the
// picker at scale 3 ("TRAGAMONEDAS" is exactly that long).
#define SCREEN_NAME_MAX 12

// Returns the byte length of the UTF-8 sequence starting at `lead`, or 0 when the
// byte cannot start a sequence.
static inline size_t screenNameSeqLen(uint8_t lead) {
  if (lead < 0x80) return 1;
  if (lead >= 0xC0 && lead <= 0xDF) return 2;
  if (lead >= 0xE0 && lead <= 0xEF) return 3;
  if (lead >= 0xF0 && lead <= 0xF7) return 4;
  return 0;  // a continuation byte or an invalid lead
}

static inline bool screenNameSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Trims the name, rejects what cannot be drawn, and truncates to at most
// SCREEN_NAME_MAX bytes (or `cap - 1` when the destination is smaller) without
// ever cutting a UTF-8 sequence. `out` is NUL-terminated on every path.
//
// Returns false for a null input, a buffer with no room, an empty name, a name
// made only of spaces, an ASCII control byte, or an incomplete UTF-8 tail.
static inline bool screenNameClean(const char *in, char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  if (!in) return false;

  size_t start = 0;
  size_t len = 0;
  while (in[len]) len++;
  while (start < len && screenNameSpace(in[start])) start++;
  while (len > start && screenNameSpace(in[len - 1])) len--;
  if (start >= len) return false;

  size_t budget = cap - 1;
  if (budget > SCREEN_NAME_MAX) budget = SCREEN_NAME_MAX;

  size_t written = 0;
  size_t i = start;
  while (i < len) {
    uint8_t lead = (uint8_t)in[i];
    // Control bytes are never part of a name: they would draw as a blank glyph
    // and make the picker look broken.
    if (lead < 0x20 || lead == 0x7F) return false;
    size_t seq = screenNameSeqLen(lead);
    if (seq == 0 || i + seq > len) return false;  // malformed or split UTF-8
    if (written + seq > budget) break;            // cut on a character boundary
    for (size_t k = 0; k < seq; k++) out[written + k] = in[i + k];
    written += seq;
    i += seq;
  }

  if (written == 0) {
    out[0] = '\0';
    return false;
  }
  // A cut can expose a trailing space ("ABCDEFGHIJK " -> 12 bytes -> "...K ").
  while (written > 0 && screenNameSpace(out[written - 1])) written--;
  out[written] = '\0';
  return written > 0;
}

// Fold UTF-8 text down to what the 5x7 panel font can draw.
//
// The font covers ASCII 32..125. Everything above it (a dieresis on EBÜ, a tilde
// on ñ) arrives as multi-byte UTF-8 and used to render as the fallback block, so
// "EBÜ" showed as "EB" plus two solid squares. Folding keeps the word readable:
// the LCD shows EBU, while the API and the web panel keep the real text.
//
// Pure and host-buildable: `tools/text_fold_test.cpp` pins the rules.
#pragma once

#include <stddef.h>

// Writes an ASCII-only version of `src` into `dst`, always NUL-terminated.
// Returns the number of characters written.
size_t textFoldAscii(const char *src, char *dst, size_t cap);

// Music screen extras: the album artwork buffer and the one place the decoded
// pixels are handed over.
//
// The screen itself is declared in screens.h like every other screen; this
// header exists because the artwork path belongs to the decoder, not to the
// carousel.
#pragma once

#include <Arduino.h>

// The owner wants the cover centred, with the title, the smaller artist and the
// progress stacked under it.
#define SCREEN_MUSIC_SHOW_ART 1

#define SCREEN_MUSIC_ART_W 172  // decoded buffer, shared with the decoder
#define SCREEN_MUSIC_COVER_SIZE 94
#define SCREEN_MUSIC_COVER_X ((320 - SCREEN_MUSIC_COVER_SIZE) / 2)
#define SCREEN_MUSIC_COVER_Y 6

// Copies a decoded full-cover RGB565 square (SCREEN_MUSIC_ART_W). Ignores anything of another size, so a
// half-decoded image can never be drawn.
void musicSetArtwork(const uint16_t *pixels, int width);
void musicClearArtwork();
bool musicHasArtwork();
// Increments on every artwork change, so a repaint can be triggered for it.
uint16_t musicArtworkGeneration();
// Which track id the current artwork belongs to, so a track change drops it.
const char *musicArtworkFor();
void musicSetArtworkFor(const char *trackId);

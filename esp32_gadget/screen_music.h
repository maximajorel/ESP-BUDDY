// Music screen extras: the album artwork buffer and the one place the decoded
// pixels are handed over.
//
// The screen itself is declared in screens.h like every other screen; this
// header exists because the artwork path belongs to the decoder, not to the
// carousel.
#pragma once

#include <Arduino.h>

// 0 = the screen shows text only. The artwork path works and is kept behind this
// switch because the 1.47" panel made dark covers read as noise and the owner
// chose a clean, centred layout instead. Flip to 1 to bring the cover back.
#define SCREEN_MUSIC_SHOW_ART 0

#define SCREEN_MUSIC_ART_W 64
#define SCREEN_MUSIC_ART_X 10
#define SCREEN_MUSIC_ART_Y 20
#define SCREEN_MUSIC_TEXT_X 84

// Copies a decoded 64x64 RGB565 square. Ignores anything of another size, so a
// half-decoded image can never be drawn.
void musicSetArtwork(const uint16_t *pixels, int width);
void musicClearArtwork();
bool musicHasArtwork();
// Increments on every artwork change, so a repaint can be triggered for it.
uint16_t musicArtworkGeneration();
// Which track id the current artwork belongs to, so a track change drops it.
const char *musicArtworkFor();
void musicSetArtworkFor(const char *trackId);

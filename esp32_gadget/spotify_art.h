// Album artwork for the music screen: download, decode and cache.
//
// The music screen already knows how to draw a 64x64 RGB565 square; this module
// owns everything between "the poll parsed a track" and `musicSetArtwork()`.
// It is deliberately separate from `spotify.cpp`: the network state machine must
// stay readable, and the decoder is the part that can fail on a hostile JPEG.
#pragma once

#include "spotify_parse.h"

// Allocates nothing. PSRAM is taken per decode and released before returning.
void spotifyArtBegin();

// Diagnostics for the console. They only set flags: the work still happens in
// the poll task, because the TLS client is shared and must never be touched
// from two tasks at once.
void spotifyArtSetDebug(bool on);
// Forgets the "already decoded this track" memo and drops the pixels, so the
// next poll downloads and decodes again.
void spotifyArtInvalidate();

// Called from the Spotify poll with the track that was just parsed. Cheap when
// the track has not changed: the artwork is keyed by track id.
void spotifyArtPoll(const SpotifyTrack &track);

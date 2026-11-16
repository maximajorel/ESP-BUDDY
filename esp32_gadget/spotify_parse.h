// Currently-playing JSON, reduced to the fields the music screen prints.
//
// Pure and host-buildable on purpose: this is where a wrong answer is invisible
// on the glass (a title cut mid-word, the 640px artwork decoded instead of the
// 64px one, a podcast that blanks the screen). `tools/spotify_core_test.cpp`
// pins the rules against real Spotify response shapes.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Buffer sizes are "max characters", so each array is one byte larger.
#define SPOTIFY_TITLE_MAX 95
#define SPOTIFY_ARTIST_MAX 95
#define SPOTIFY_ALBUM_MAX 63
#define SPOTIFY_TRACK_ID_MAX 39
#define SPOTIFY_ART_URL_MAX 191

// Smallest artwork that still has detail to reduce from. Below this the cover is
// a pre-shrunk thumbnail whose own JPEG grain survives into the panel.
#define SPOTIFY_ART_PREFERRED_MIN 128

enum SpotifyParseResult {
  SPOTIFY_PARSE_OK = 0,     // a track or episode is loaded in `out`
  SPOTIFY_PARSE_EMPTY = 1,  // 204, or nothing is playing right now
  SPOTIFY_PARSE_ERROR = 2,  // the body was not usable JSON
};

struct SpotifyTrack {
  bool isPlaying;
  uint32_t progressMs;
  uint32_t durationMs;
  char title[SPOTIFY_TITLE_MAX + 1];
  // Every artist joined with ", "; for an episode, the show name.
  char artist[SPOTIFY_ARTIST_MAX + 1];
  char album[SPOTIFY_ALBUM_MAX + 1];
  char trackId[SPOTIFY_TRACK_ID_MAX + 1];
  // The smallest artwork URL Spotify offers, which is the 64px one when it
  // exists. Empty when the response carried no images.
  char artUrl[SPOTIFY_ART_URL_MAX + 1];
};

// Parses a `/v1/me/player/currently-playing` body. `out` is always either a
// complete track or a zeroed struct; it never keeps the previous track.
SpotifyParseResult spotifyParseCurrentlyPlaying(const char *json, size_t len,
                                                SpotifyTrack *out);

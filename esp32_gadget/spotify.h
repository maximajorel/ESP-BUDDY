// Spotify "now playing" feed: credentials, OAuth, polling and the snapshot the
// music screen renders.
//
// Split so the risky parts stay testable: `spotify_parse` and `spotify_pkce` are
// pure and compiled for the host, `spotify_store` owns NVS, and this module owns
// the network state machine and the one lock that lets a background poll run
// without stalling the button or the panel.
#pragma once

#include <Arduino.h>

#include "spotify_parse.h"

// From the panel's point of view: what is missing before a track can appear.
#define SPOTIFY_AUTH_UNCONFIGURED 0  // no client id stored
#define SPOTIFY_AUTH_NO_WIFI 1       // STA link down, so no internet
#define SPOTIFY_AUTH_NEEDS_CODE 2    // client id present, no refresh token yet
#define SPOTIFY_AUTH_READY 3         // tokens in hand

// What the last poll produced, one state per honest thing the screen can say.
#define SPOTIFY_FEED_IDLE 0     // nothing polled yet
#define SPOTIFY_FEED_LOADING 1  // a poll is in flight
#define SPOTIFY_FEED_TRACK 2    // a track or episode is playing
#define SPOTIFY_FEED_NOTHING 3  // 204: nothing is playing right now
#define SPOTIFY_FEED_ERROR 4    // network, auth or rate-limit failure

struct SpotifySnapshot {
  uint8_t auth;
  uint8_t feed;
  SpotifyTrack track;
  // millis() of the last successful poll; 0 when there has never been one.
  uint32_t fetchedAtMs;
  uint16_t httpStatus;
  char error[64];
};

void spotifyBegin();

// Services the poll schedule. Cheap when idle: it only sets flags.
void spotifyLoop();

// A copy taken under the lock: the poll task writes the same struct.
void spotifySnapshot(SpotifySnapshot *out);

// Forces a poll as soon as the poll task wakes up.
void spotifyRequestRefresh();

// True while the music screen is on the glass. Polling stops otherwise: a TLS
// handshake every ten seconds for a screen nobody is looking at is not free.
void spotifySetActive(bool active);

// Waits for the poll task to go idle, for the console and the panel.
bool spotifyWaitIdle(uint32_t timeoutMs);

// Console surface, used to verify the module without the panel.
void spotifyConsole(int argc, char **argv);

// OAuth 2.0 authorization-code flow with PKCE, no client secret.
//
// The ESP32 cannot receive the redirect: Spotify accepts only HTTPS or an
// explicit loopback address, and neither is the device. So the owner opens the
// authorize URL on a computer, copies the `code` out of the failed redirect's
// address bar and hands it to the panel once; the verifier that proves the
// exchange stays in RAM on the device the whole time.
//
// The verifier is deliberately not persisted: if the board reboots between
// showing the URL and receiving the code, the flow restarts. That is a few
// seconds of work and far cheaper than another secret on the flash.
#pragma once

#include <Arduino.h>

// Must match the redirect URI registered in the Spotify developer dashboard,
// character for character. Nothing listens on that port; the code is copied out
// of the browser's address bar.
#define SPOTIFY_REDIRECT_URI "http://127.0.0.1:8888/callback"
#define SPOTIFY_SCOPE "user-read-currently-playing"

// Builds the authorize URL the panel shows. Generates a fresh verifier, so
// calling it again invalidates any code obtained for the previous URL.
bool spotifyAuthBuildUrl(char *out, size_t cap);

// True when a verifier is waiting for its code.
bool spotifyAuthFlowPending();

// Trades the pasted code for tokens. On success the refresh token is persisted
// (and rotated, when Spotify returns a new one).
bool spotifyAuthExchangeCode(const char *code, char *err, size_t errCap);

// Returns a usable access token in RAM, refreshing when `force` or near expiry.
// An `invalid_grant` means the refresh token is dead: it is dropped and `err`
// says the owner has to authorize again.
bool spotifyAuthEnsureAccessToken(bool force, char *err, size_t errCap);

const char *spotifyAuthAccessToken();

// Drops the in-RAM token and any pending verifier.
void spotifyAuthReset();

// How long the current access token still has, for the console.
long spotifyAuthSecondsLeft();

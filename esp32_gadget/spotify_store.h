// Spotify credentials in NVS, kept out of the settings blob on purpose.
//
// The settings struct is written as one blob, so adding fields to it would
// invalidate every existing owner's WiFi credentials on upgrade. The Spotify
// secrets therefore live in their own namespace under their own keys, the same
// way orientation and steer calibration do.
#pragma once

#include <Arduino.h>

#define SPOTIFY_CLIENT_ID_MAX 63
#define SPOTIFY_TOKEN_MAX 511

void spotifyStoreBegin();

// A client id is not a secret (it travels in the authorization URL), but a typo
// in it produces an OAuth error that reads like a Spotify outage, so the shape
// is validated before it is stored.
bool spotifyStoreSetClientId(const char *clientId);
const char *spotifyStoreClientId();
bool spotifyStoreHasClientId();
// "abcd…wxyz" for logs and the panel. Never prints the whole token.
void spotifyStoreMaskClientId(char *out, size_t cap);

// The refresh token is the only long-lived secret on the device. The short-lived
// access token stays in RAM: persisting it would cost a flash write per hour to
// save a single request per boot.
bool spotifyStoreSetRefreshToken(const char *token);
const char *spotifyStoreRefreshToken();
bool spotifyStoreHasRefreshToken();
uint16_t spotifyStoreRefreshTokenLength();

// Drops the tokens and keeps the client id, for "reautorizar".
void spotifyStoreForgetTokens();
// Drops everything, for the panel's factory reset.
void spotifyStoreForgetAll();

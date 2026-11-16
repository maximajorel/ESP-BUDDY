#include "spotify_store.h"

#include <Preferences.h>
#include <string.h>

static Preferences s_prefs;
static bool s_ready = false;
static char s_clientId[SPOTIFY_CLIENT_ID_MAX + 1];
static char s_refresh[SPOTIFY_TOKEN_MAX + 1];

static bool hex32(const char *s) {
  size_t len = strlen(s);
  if (len != 32) return false;
  for (size_t i = 0; i < len; i++) {
    char c = s[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F');
    if (!ok) return false;
  }
  return true;
}

void spotifyStoreBegin() {
  s_clientId[0] = '\0';
  s_refresh[0] = '\0';
  s_ready = s_prefs.begin("spotify", false);
  if (!s_ready) return;

  String id = s_prefs.getString("client_id", "");
  if (id.length() <= SPOTIFY_CLIENT_ID_MAX) {
    strncpy(s_clientId, id.c_str(), SPOTIFY_CLIENT_ID_MAX);
    s_clientId[SPOTIFY_CLIENT_ID_MAX] = '\0';
  }
  String token = s_prefs.getString("refresh", "");
  if (token.length() <= SPOTIFY_TOKEN_MAX) {
    strncpy(s_refresh, token.c_str(), SPOTIFY_TOKEN_MAX);
    s_refresh[SPOTIFY_TOKEN_MAX] = '\0';
  }
}

bool spotifyStoreSetClientId(const char *clientId) {
  if (!clientId) return false;
  size_t len = strlen(clientId);
  // Eight characters minimum keeps an empty or truncated paste out; the 32-hex
  // shape every real Spotify id has is only reported, never enforced, because
  // guessing wrong here would lock the owner out of their own device.
  if (len < 8 || len > SPOTIFY_CLIENT_ID_MAX) return false;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)clientId[i];
    if (c <= 0x20 || c >= 0x7F) return false;
  }
  strncpy(s_clientId, clientId, SPOTIFY_CLIENT_ID_MAX);
  s_clientId[SPOTIFY_CLIENT_ID_MAX] = '\0';
  if (s_ready) {
    s_prefs.putString("client_id", s_clientId);
  }
  return true;
}

const char *spotifyStoreClientId() { return s_clientId; }

bool spotifyStoreHasClientId() { return s_clientId[0] != '\0'; }

void spotifyStoreMaskClientId(char *out, size_t cap) {
  if (!out || cap == 0) return;
  size_t len = strlen(s_clientId);
  if (len <= 8) {
    snprintf(out, cap, "%s", s_clientId);
    return;
  }
  // ASCII dots deliberately: this string reaches the serial console, which is
  // not UTF-8 aware here and turned a real ellipsis into mojibake.
  snprintf(out, cap, "%.4s...%.4s", s_clientId, s_clientId + len - 4);
}

bool spotifyStoreSetRefreshToken(const char *token) {
  if (!token) return false;
  size_t len = strlen(token);
  if (len == 0 || len > SPOTIFY_TOKEN_MAX) return false;
  strncpy(s_refresh, token, SPOTIFY_TOKEN_MAX);
  s_refresh[SPOTIFY_TOKEN_MAX] = '\0';
  if (s_ready) {
    s_prefs.putString("refresh", s_refresh);
  }
  return true;
}

const char *spotifyStoreRefreshToken() { return s_refresh; }

bool spotifyStoreHasRefreshToken() { return s_refresh[0] != '\0'; }

uint16_t spotifyStoreRefreshTokenLength() {
  return (uint16_t)strlen(s_refresh);
}

void spotifyStoreForgetTokens() {
  s_refresh[0] = '\0';
  if (s_ready) s_prefs.remove("refresh");
}

void spotifyStoreForgetAll() {
  s_refresh[0] = '\0';
  s_clientId[0] = '\0';
  if (s_ready) {
    s_prefs.remove("refresh");
    s_prefs.remove("client_id");
  }
}

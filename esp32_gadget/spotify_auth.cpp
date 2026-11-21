#include "spotify_auth.h"

#include <ArduinoJson.h>
#include <esp_random.h>
#include <time.h>

#include "spotify_http.h"
#include "spotify_pkce.h"
#include "spotify_store.h"
#include "url_codec.h"

// Unreserved set from RFC 7636; 66 characters, so a modulo has no bias worth
// worrying about for a value that lives for a couple of minutes.
static const char VERIFIER_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

static char s_verifier[65];
static char s_accessToken[SPOTIFY_TOKEN_MAX + 1];
static time_t s_expiresAt = 0;

// A stale clock (NTP not yet synced) must not turn every poll into a refresh,
// so an unsynced clock gets a deliberately short window instead.
static bool clockSynced() { return time(nullptr) > 1600000000; }

static void copyErr(char *err, size_t cap, const char *text) {
  if (!err || cap == 0) return;
  snprintf(err, cap, "%s", text ? text : "");
}

// Spotify answers errors as {"error":"invalid_grant","error_description":"..."}.
// The description is more useful than the code, but the code is what tells us
// whether the owner has to act.
static void spotifyErrorText(const String &body, char *err, size_t cap) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    copyErr(err, cap, "respuesta invalida de Spotify");
    return;
  }
  const char *code = doc["error"] | "";
  const char *desc = doc["error_description"] | "";
  if (*desc) {
    snprintf(err, cap, "%s (%s)", desc, code);
  } else if (*code) {
    copyErr(err, cap, code);
  } else {
    copyErr(err, cap, "error desconocido de Spotify");
  }
}

bool spotifyAuthBuildUrl(char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  if (!spotifyStoreHasClientId()) return false;

  for (size_t i = 0; i < 64; i++) {
    s_verifier[i] = VERIFIER_CHARS[esp_random() % (sizeof(VERIFIER_CHARS) - 1)];
  }
  s_verifier[64] = '\0';

  char challenge[64];
  if (spotifyPkceChallenge(s_verifier, challenge, sizeof(challenge)) !=
      SPOTIFY_PKCE_OK) {
    return false;
  }

  char client[128];
  char scope[128];
  char redirect[128];
  urlEncode(spotifyStoreClientId(), client, sizeof(client));
  urlEncode(SPOTIFY_SCOPE, scope, sizeof(scope));
  urlEncode(SPOTIFY_REDIRECT_URI, redirect, sizeof(redirect));

  int n = snprintf(
      out, cap,
      "https://accounts.spotify.com/authorize?response_type=code"
      "&client_id=%s&scope=%s&redirect_uri=%s"
      "&code_challenge_method=S256&code_challenge=%s",
      client, scope, redirect, challenge);
  return n > 0 && (size_t)n < cap;
}

bool spotifyAuthFlowPending() { return s_verifier[0] != '\0'; }

bool spotifyAuthExchangeCode(const char *code, char *err, size_t errCap) {
  if (!code || !*code) {
    copyErr(err, errCap, "falta el code");
    return false;
  }
  if (!spotifyAuthFlowPending()) {
    copyErr(err, errCap, "pedi la URL de autorizacion primero");
    return false;
  }

  // Accept the whole redirect URL, the `code=...` fragment or the bare code: the
  // owner copies whatever their address bar shows, and asking them to trim it by
  // hand is how a valid authorization ends up looking like a device failure.
  char extracted[512];
  switch (spotifyExtractCode(code, extracted, sizeof(extracted))) {
    case SPOTIFY_CODE_OK:
      break;
    case SPOTIFY_CODE_DENIED:
      copyErr(err, errCap, "no autorizaste la app, proba de nuevo");
      return false;
    case SPOTIFY_CODE_BUFFER_TOO_SMALL:
      copyErr(err, errCap, "el code es demasiado largo");
      return false;
    default:
      copyErr(err, errCap, "no encontre el code en lo que pegaste");
      return false;
  }

  char codeEnc[512];
  char redirect[128];
  char client[128];
  urlEncode(extracted, codeEnc, sizeof(codeEnc));
  urlEncode(SPOTIFY_REDIRECT_URI, redirect, sizeof(redirect));
  urlEncode(spotifyStoreClientId(), client, sizeof(client));

  String form = "grant_type=authorization_code";
  form += "&code=" + String(codeEnc);
  form += "&redirect_uri=" + String(redirect);
  form += "&client_id=" + String(client);
  form += "&code_verifier=" + String(s_verifier);

  SpotifyHttpResult res;
  spotifyHttpPostForm("accounts.spotify.com", "/api/token", form, &res);
  if (res.status != 200) {
    if (res.status == 0) {
      copyErr(err, errCap, res.body.length() ? res.body.c_str()
                                            : "sin conexion con Spotify");
    } else {
      spotifyErrorText(res.body, err, errCap);
    }
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, res.body) != DeserializationError::Ok) {
    copyErr(err, errCap, "respuesta de token ilegible");
    return false;
  }
  const char *access = doc["access_token"] | "";
  const char *refresh = doc["refresh_token"] | "";
  long expiresIn = doc["expires_in"] | 3600L;
  if (!*access) {
    copyErr(err, errCap, "sin access_token");
    return false;
  }
  if (!*refresh) {
    // Without a refresh token the device would need a new code every hour.
    copyErr(err, errCap, "Spotify no devolvio refresh_token");
    return false;
  }

  strncpy(s_accessToken, access, SPOTIFY_TOKEN_MAX);
  s_accessToken[SPOTIFY_TOKEN_MAX] = '\0';
  s_expiresAt = time(nullptr) + expiresIn - 60;
  spotifyStoreSetRefreshToken(refresh);
  s_verifier[0] = '\0';  // single use
  return true;
}

bool spotifyAuthEnsureAccessToken(bool force, char *err, size_t errCap) {
  if (!force && s_accessToken[0] &&
      (clockSynced() ? time(nullptr) < s_expiresAt : true)) {
    // An unsynced clock cannot judge expiry, so it keeps the token until a
    // request fails; a wrong 401 costs one extra refresh, not a broken screen.
    if (clockSynced()) return true;
    if (s_expiresAt > 0 && time(nullptr) < s_expiresAt) return true;
  }
  if (!spotifyStoreHasRefreshToken()) {
    copyErr(err, errCap, "falta autorizar");
    return false;
  }

  char tokenEnc[SPOTIFY_TOKEN_MAX * 3 + 8];
  char client[128];
  urlEncode(spotifyStoreRefreshToken(), tokenEnc, sizeof(tokenEnc));
  urlEncode(spotifyStoreClientId(), client, sizeof(client));

  String form = "grant_type=refresh_token";
  form += "&refresh_token=" + String(tokenEnc);
  form += "&client_id=" + String(client);

  SpotifyHttpResult res;
  spotifyHttpPostForm("accounts.spotify.com", "/api/token", form, &res);
  if (res.status != 200) {
    if (res.status == 0) {
      copyErr(err, errCap, res.body.length() ? res.body.c_str()
                                            : "sin conexion con Spotify");
      return false;
    }
    spotifyErrorText(res.body, err, errCap);
    // invalid_grant is terminal for this token: the owner has to reauthorize.
    if (res.body.indexOf("invalid_grant") >= 0) {
      spotifyStoreForgetTokens();
      s_accessToken[0] = '\0';
      s_expiresAt = 0;
      copyErr(err, errCap, "token vencido, autoriza de nuevo");
    }
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, res.body) != DeserializationError::Ok) {
    copyErr(err, errCap, "respuesta de token ilegible");
    return false;
  }
  const char *access = doc["access_token"] | "";
  long expiresIn = doc["expires_in"] | 3600L;
  if (!*access) {
    copyErr(err, errCap, "sin access_token");
    return false;
  }
  strncpy(s_accessToken, access, SPOTIFY_TOKEN_MAX);
  s_accessToken[SPOTIFY_TOKEN_MAX] = '\0';
  s_expiresAt = time(nullptr) + expiresIn - 60;

  // Spotify may rotate the refresh token on every use; dropping the new one
  // silently locks the device out an hour later.
  const char *rotation = doc["refresh_token"] | "";
  if (*rotation) spotifyStoreSetRefreshToken(rotation);
  return true;
}

const char *spotifyAuthAccessToken() { return s_accessToken; }

void spotifyAuthReset() {
  s_verifier[0] = '\0';
  s_accessToken[0] = '\0';
  s_expiresAt = 0;
}

long spotifyAuthSecondsLeft() {
  if (!s_accessToken[0]) return 0;
  if (!clockSynced()) return -1;  // unknown without a clock
  long left = (long)(s_expiresAt - time(nullptr));
  return left > 0 ? left : 0;
}

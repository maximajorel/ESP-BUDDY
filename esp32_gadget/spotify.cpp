#include "spotify.h"

#include <string.h>

#include "console.h"
#include "net.h"
#include "spotify_art.h"
#include "spotify_auth.h"
#include "spotify_http.h"
#include "screen_music.h"
#include "spotify_store.h"

// The poll runs in its own task, so the screen and the panel both read a copy of
// a struct that another task may be writing. A short critical section around the
// copy is enough: the struct is ~500 bytes and is never touched for long.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static SpotifySnapshot s_state;

// Owned by the loop/console side; read by the poll task.
static volatile bool s_wantPoll = false;
static volatile bool s_active = false;
static volatile bool s_polling = false;
static volatile uint32_t s_nextPollAt = 0;
static volatile uint32_t s_backoffUntil = 0;

// Ten seconds is well inside Spotify's rolling 30 s window for one client, and
// fast enough that a track change shows up while the owner is still looking.
#define SPOTIFY_POLL_MS 10000
#define SPOTIFY_POLL_RETRY_MS 20000
#define SPOTIFY_TASK_STACK 12288

static const char *authName(uint8_t auth) {
  switch (auth) {
    case SPOTIFY_AUTH_UNCONFIGURED: return "sin configurar";
    case SPOTIFY_AUTH_NO_WIFI: return "sin wifi";
    case SPOTIFY_AUTH_NEEDS_CODE: return "falta autorizar";
    case SPOTIFY_AUTH_READY: return "listo";
  }
  return "?";
}

static const char *feedName(uint8_t feed) {
  switch (feed) {
    case SPOTIFY_FEED_IDLE: return "idle";
    case SPOTIFY_FEED_LOADING: return "cargando";
    case SPOTIFY_FEED_TRACK: return "pista";
    case SPOTIFY_FEED_NOTHING: return "nada sonando";
    case SPOTIFY_FEED_ERROR: return "error";
  }
  return "?";
}

// Credentials come before connectivity in this order: a missing client id is
// what the owner has to act on, and "sin wifi" would hide it behind a link that
// may well be fine by the time they read the screen.
static uint8_t deriveAuth() {
  if (!spotifyStoreHasClientId()) return SPOTIFY_AUTH_UNCONFIGURED;
  if (!spotifyStoreHasRefreshToken()) return SPOTIFY_AUTH_NEEDS_CODE;
  if (!netStatus().staConnected) return SPOTIFY_AUTH_NO_WIFI;
  return SPOTIFY_AUTH_READY;
}

static void setError(uint16_t status, const char *text) {
  portENTER_CRITICAL(&s_mux);
  s_state.feed = SPOTIFY_FEED_ERROR;
  s_state.httpStatus = status;
  snprintf(s_state.error, sizeof(s_state.error), "%s", text ? text : "");
  portEXIT_CRITICAL(&s_mux);
}

static void setFeed(uint8_t feed) {
  portENTER_CRITICAL(&s_mux);
  s_state.feed = feed;
  if (feed != SPOTIFY_FEED_ERROR) s_state.error[0] = '\0';
  portEXIT_CRITICAL(&s_mux);
}

// One poll: token, then the endpoint. Runs only in the poll task.
static void pollOnce() {
  char err[64] = "";
  setFeed(SPOTIFY_FEED_LOADING);

  if (!spotifyAuthEnsureAccessToken(false, err, sizeof(err))) {
    setError(0, err);
    return;
  }

  SpotifyHttpResult res;
  spotifyHttpGet("api.spotify.com", "/v1/me/player/currently-playing",
                 spotifyAuthAccessToken(), 8192, &res);

  // A 401 here means our idea of the token lifetime was wrong (a clock jump, a
  // device that booted before NTP). Refresh once and retry before giving up.
  if (res.status == 401) {
    if (!spotifyAuthEnsureAccessToken(true, err, sizeof(err))) {
      setError(401, err);
      return;
    }
    spotifyHttpGet("api.spotify.com", "/v1/me/player/currently-playing",
                   spotifyAuthAccessToken(), 8192, &res);
  }

  if (res.status == 429) {
    long wait = res.retryAfterSeconds > 0 ? res.retryAfterSeconds : 30;
    if (wait < 5) wait = 5;
    if (wait > 300) wait = 300;
    s_backoffUntil = millis() + (uint32_t)wait * 1000;
    setError(429, "limite de Spotify, esperando");
    return;
  }
  if (res.status == 0) {
    setError(0, res.body.length() ? res.body.c_str() : "sin conexion");
    return;
  }
  if (res.status == 204) {
    portENTER_CRITICAL(&s_mux);
    memset(&s_state.track, 0, sizeof(s_state.track));
    s_state.feed = SPOTIFY_FEED_NOTHING;
    s_state.httpStatus = 204;
    s_state.error[0] = '\0';
    s_state.fetchedAtMs = millis();
    portEXIT_CRITICAL(&s_mux);
    return;
  }
  if (res.status != 200) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Spotify respondio %d", res.status);
    setError((uint16_t)res.status, msg);
    return;
  }

  SpotifyTrack track;
  SpotifyParseResult parsed =
      spotifyParseCurrentlyPlaying(res.body.c_str(), res.body.length(), &track);
  portENTER_CRITICAL(&s_mux);
  s_state.httpStatus = 200;
  s_state.error[0] = '\0';
  if (parsed == SPOTIFY_PARSE_OK) {
    s_state.track = track;
    s_state.feed = SPOTIFY_FEED_TRACK;
    s_state.fetchedAtMs = millis();
  } else if (parsed == SPOTIFY_PARSE_EMPTY) {
    memset(&s_state.track, 0, sizeof(s_state.track));
    s_state.feed = SPOTIFY_FEED_NOTHING;
    s_state.fetchedAtMs = millis();
  } else {
    s_state.feed = SPOTIFY_FEED_ERROR;
    snprintf(s_state.error, sizeof(s_state.error), "respuesta ilegible");
  }
  portEXIT_CRITICAL(&s_mux);

  // Artwork is fetched outside the lock: the download is the one slow part of a
  // poll and the screen must keep reading the previous snapshot meanwhile.
#if SCREEN_MUSIC_SHOW_ART
  // Only fetch a cover when the screen actually draws one: it is a second HTTP
  // request and ~20 KB per track change for nothing otherwise.
  if (parsed == SPOTIFY_PARSE_OK) spotifyArtPoll(track);
#endif
}

// Own task, pinned away from the panel loop. A TLS handshake takes a second and
// a half and tens of kilobytes; doing that inside loop() would freeze the button
// and the long-press bar for its duration.
static void pollTask(void *) {
  for (;;) {
    uint32_t now = millis();
    bool due = s_wantPoll ||
               ((int32_t)(now - s_nextPollAt) >= 0 && s_active);
    if (!due) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if ((int32_t)(now - s_backoffUntil) < 0) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    s_wantPoll = false;
    s_polling = true;
    // The console and the panel wait on this state, so it is published before
    // the first byte goes out.
    uint8_t auth = deriveAuth();
    portENTER_CRITICAL(&s_mux);
    s_state.auth = auth;
    portEXIT_CRITICAL(&s_mux);
    if (auth == SPOTIFY_AUTH_READY) {
      pollOnce();
      s_nextPollAt = millis() + SPOTIFY_POLL_MS;
    } else {
      s_nextPollAt = millis() + SPOTIFY_POLL_RETRY_MS;
    }
    s_polling = false;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void spotifyBegin() {
  spotifyArtBegin();
  spotifyStoreBegin();
  memset(&s_state, 0, sizeof(s_state));
  s_state.feed = SPOTIFY_FEED_IDLE;
  s_state.auth = deriveAuth();
  if (spotifyStoreHasClientId()) {
    char masked[24];
    spotifyStoreMaskClientId(masked, sizeof(masked));
    Serial.printf("spotify: client id %s, token %s\n", masked,
                  spotifyStoreHasRefreshToken() ? "presente" : "ausente");
  } else {
    Serial.println("spotify: sin client id (panel o 'spotify id <id>')");
  }
  xTaskCreatePinnedToCore(pollTask, "spotify", SPOTIFY_TASK_STACK, nullptr, 1,
                          nullptr, 0);
}

void spotifyLoop() {
  uint8_t auth = deriveAuth();
  portENTER_CRITICAL(&s_mux);
  s_state.auth = auth;
  portEXIT_CRITICAL(&s_mux);
}

void spotifySnapshot(SpotifySnapshot *out) {
  if (!out) return;
  portENTER_CRITICAL(&s_mux);
  *out = s_state;
  portEXIT_CRITICAL(&s_mux);
  out->auth = deriveAuth();
}

void spotifyRequestRefresh() { s_wantPoll = true; }

void spotifySetActive(bool active) {
  if (active && !s_active) s_wantPoll = true;
  s_active = active;
}

bool spotifyWaitIdle(uint32_t timeoutMs) {
  uint32_t deadline = millis() + timeoutMs;
  // A poll that has not started yet still has to be waited for, so the wait
  // covers both "queued" and "in flight".
  while (s_polling || s_wantPoll) {
    if ((int32_t)(millis() - deadline) >= 0) return false;
    delay(50);
  }
  return true;
}

// --- console ----------------------------------------------------------------

// The client id is public (it travels in the authorize URL), so it is printed in
// full; the refresh token never is, only its presence and length.
void spotifyConsole(int argc, char **argv) {
  const char *sub = argc > 1 ? argv[1] : "status";
  if (!strcmp(sub, "id")) {
    if (argc < 3) {
      Serial.println("uso: spotify id <client_id>");
      return;
    }
    if (!spotifyStoreSetClientId(argv[2])) {
      Serial.println("spotify: client id invalido (8..63 chars sin espacios)");
      return;
    }
    size_t len = strlen(spotifyStoreClientId());
    Serial.printf("spotify: client id guardado (%u chars)\n", (unsigned)len);
    bool hex = len == 32;
    for (size_t i = 0; hex && i < len; i++) {
      char c = spotifyStoreClientId()[i];
      hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    if (!hex) {
      Serial.println("spotify: aviso, un client id de Spotify tiene 32 hex");
    }
    return;
  }
  if (!strcmp(sub, "forget")) {
    if (argc > 2 && !strcmp(argv[2], "all")) {
      spotifyStoreForgetAll();
      spotifyAuthReset();
      Serial.println("spotify: client id y token borrados");
    } else {
      spotifyStoreForgetTokens();
      spotifyAuthReset();
      Serial.println("spotify: token borrado, el client id queda");
    }
    setFeed(SPOTIFY_FEED_IDLE);
    return;
  }
  if (!strcmp(sub, "url")) {
    char url[640];
    if (!spotifyAuthBuildUrl(url, sizeof(url))) {
      Serial.println("spotify: guarda el client id primero");
      return;
    }
    txReliable(true);
    Serial.printf("spotify url %s\n", url);
    txReliable(false);
    Serial.println("spotify: abrila en la compu, autoriza y pega la URL entera");
    Serial.println("spotify code <url o code>");
    return;
  }
  if (!strcmp(sub, "code")) {
    if (argc < 3) {
      Serial.println("uso: spotify code <code o URL completa>");
      return;
    }
    char err[72] = "";
    if (!spotifyAuthExchangeCode(argv[2], err, sizeof(err))) {
      Serial.printf("spotify: fallo el canje: %s\n", err);
      return;
    }
    Serial.printf("spotify: autorizado, token de refresco guardado (%u chars)\n",
                  (unsigned)spotifyStoreRefreshTokenLength());
    s_wantPoll = true;
    return;
  }
  if (!strcmp(sub, "poll")) {
    s_wantPoll = true;
    if (!spotifyWaitIdle(15000)) Serial.println("spotify: poll lento, mirando...");
    return;
  }

  SpotifySnapshot snap;
  spotifySnapshot(&snap);
  char masked[24];
  spotifyStoreMaskClientId(masked, sizeof(masked));
  Serial.printf("spotify auth    %s\n", authName(snap.auth));
  Serial.printf("spotify client  %s\n", masked[0] ? masked : "(vacio)");
  Serial.printf("spotify token   %s (%u chars)  access=%lds\n",
                spotifyStoreHasRefreshToken() ? "presente" : "ausente",
                (unsigned)spotifyStoreRefreshTokenLength(),
                spotifyAuthSecondsLeft());
  Serial.printf("spotify feed    %s  http=%u  error=%s\n", feedName(snap.feed),
                (unsigned)snap.httpStatus, snap.error[0] ? snap.error : "-");
  Serial.printf("spotify sta     %s  ip=%s  heap=%lu\n",
                netStatus().staConnected ? "conectado" : "caido",
                netStatus().staIp, (unsigned long)ESP.getFreeHeap());
  if (snap.feed == SPOTIFY_FEED_TRACK) {
    Serial.printf("spotify track   %s - %s [%s] %lu/%lu ms\n", snap.track.title,
                  snap.track.artist, snap.track.isPlaying ? "play" : "pause",
                  (unsigned long)snap.track.progressMs,
                  (unsigned long)snap.track.durationMs);
    if (snap.track.artUrl[0]) {
      Serial.printf("spotify art     %s\n", snap.track.artUrl);
    }
  }
}

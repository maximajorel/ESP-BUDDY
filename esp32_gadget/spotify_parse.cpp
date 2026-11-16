#include "spotify_parse.h"

#include <ArduinoJson.h>
#include <string.h>

// Longest run of `maxChars` characters that fits, always NUL-terminated.
static void copyTrunc(char *dst, size_t maxChars, const char *src) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len > maxChars) len = maxChars;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// Spotify lists artwork at several sizes and in no guaranteed order. The 64px
// thumbnail is tempting (3 KB) but it is already a destroyed JPEG: at 64x64 the
// grain dominates and the cover reads as coloured noise on the panel. So the
// smallest image that still carries detail wins, and the firmware does the final
// reduction itself with an area average. Picking images[0] would fetch a 640px
// JPEG, which is the difference between a 3 KB download and a 60 KB one.
static void pickArtwork(JsonArray images, char *dst, size_t cap) {
  dst[0] = '\0';
  const char *best = nullptr;
  long bestScore = 0;
  for (JsonObject img : images) {
    const char *url = img["url"] | (const char *)nullptr;
    if (!url || !*url) continue;
    int width = img["width"] | 0;
    long score;
    if (width >= SPOTIFY_ART_PREFERRED_MIN) {
      // Enough detail: among these, the smallest keeps the download cheap.
      score = width;
    } else if (width > 0) {
      // Below the threshold: only better than an unknown size, prefer the larger.
      score = 100000 + (SPOTIFY_ART_PREFERRED_MIN - width);
    } else {
      score = 200000;  // unknown size: last resort
    }
    if (!best || score < bestScore) {
      best = url;
      bestScore = score;
    }
  }
  if (best) copyTrunc(dst, cap, best);
}

// "A, B, C", truncated to the artist buffer as a whole rather than per name.
static void joinArtists(JsonArray artists, char *dst, size_t maxChars) {
  char *p = dst;
  size_t room = maxChars;
  for (JsonObject a : artists) {
    const char *name = a["name"] | "";
    if (!*name) continue;
    if (p != dst) {
      if (room < 2) break;
      *p++ = ',';
      *p++ = ' ';
      room -= 2;
    }
    size_t len = strlen(name);
    if (len > room) len = room;
    memcpy(p, name, len);
    p += len;
    room -= len;
    if (room == 0) break;
  }
  *p = '\0';
}

// Only the fields the screen prints, so a poll allocates hundreds of bytes
// instead of the whole response (which carries external URLs, available
// markets and every artist's href).
static JsonDocument &currentlyPlayingFilter() {
  static JsonDocument filter;
  static bool ready = false;
  if (!ready) {
    filter["is_playing"] = true;
    filter["progress_ms"] = true;
    filter["item"]["type"] = true;
    filter["item"]["id"] = true;
    filter["item"]["name"] = true;
    filter["item"]["duration_ms"] = true;
    filter["item"]["artists"][0]["name"] = true;
    filter["item"]["album"]["name"] = true;
    filter["item"]["album"]["images"][0]["url"] = true;
    filter["item"]["album"]["images"][0]["width"] = true;
    filter["item"]["show"]["name"] = true;
    filter["item"]["show"]["images"][0]["url"] = true;
    filter["item"]["show"]["images"][0]["width"] = true;
    ready = true;
  }
  return filter;
}

SpotifyParseResult spotifyParseCurrentlyPlaying(const char *json, size_t len,
                                                SpotifyTrack *out) {
  if (!out) return SPOTIFY_PARSE_ERROR;
  memset(out, 0, sizeof(*out));

  // HTTP 204, or the caller had nothing to give us.
  if (!json || len == 0) return SPOTIFY_PARSE_EMPTY;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, json, len,
                      DeserializationOption::Filter(currentlyPlayingFilter()));
  if (err) return SPOTIFY_PARSE_ERROR;

  // An advertisement break reports 200 with a null item; a podcast that ended
  // reports the same. Either way there is no track to show.
  JsonVariant item = doc["item"];
  if (item.isNull()) return SPOTIFY_PARSE_EMPTY;

  out->isPlaying = doc["is_playing"] | false;
  out->progressMs = doc["progress_ms"] | (uint32_t)0;
  out->durationMs = item["duration_ms"] | (uint32_t)0;
  copyTrunc(out->title, SPOTIFY_TITLE_MAX, item["name"] | "");
  copyTrunc(out->trackId, SPOTIFY_TRACK_ID_MAX, item["id"] | "");

  // An episode has no album: the show is the second line and the artwork
  // source, which is better than a blank screen while audio is playing.
  const char *type = item["type"] | "track";
  if (strcmp(type, "episode") == 0) {
    copyTrunc(out->artist, SPOTIFY_ARTIST_MAX, item["show"]["name"] | "");
    pickArtwork(item["show"]["images"].as<JsonArray>(), out->artUrl,
                SPOTIFY_ART_URL_MAX);
  } else {
    joinArtists(item["artists"].as<JsonArray>(), out->artist, SPOTIFY_ARTIST_MAX);
    copyTrunc(out->album, SPOTIFY_ALBUM_MAX, item["album"]["name"] | "");
    pickArtwork(item["album"]["images"].as<JsonArray>(), out->artUrl,
                SPOTIFY_ART_URL_MAX);
  }
  return SPOTIFY_PARSE_OK;
}

#include "spotify_art.h"
#include <Arduino.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <string.h>

#include <new>

#include "screen_music.h"
#include "spotify_http.h"

// Spotify's smallest artwork is ~3 KB, but a playlist can carry a 640px image
// when the response is odd. Cap the download so a bad URL can never eat the
// station's bandwidth; the decoder downscales whatever fits.
#define SPOTIFY_ART_MAX_BYTES (64 * 1024)
// A failed fetch or a broken JPEG is retried only after this long, so a track
// whose artwork is gone does not cost a TLS handshake every poll.
#define SPOTIFY_ART_RETRY_MS 60000
// Guard against a decompression-bomb header: Spotify never exceeds 640px, and a
// scaled buffer beyond this would not fit the PSRAM budget anyway.
#define SPOTIFY_ART_MAX_SIDE 1024

// What the last attempt was for. The screen's `musicArtworkFor()` already keys
// successes by track id; this covers failures and tracks without an id.
static char s_attemptKey[SPOTIFY_ART_URL_MAX + 1];
static bool s_attempted = false;
static bool s_attemptOk = false;
static uint32_t s_attemptAtMs = 0;

// The decoder is 20-odd KB of scratch. It lives in PSRAM, not in `.bss`, so the
// internal heap keeps the room the TLS handshake needs.
static void *psramAlloc(size_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// Diagnostics, off unless the console asks for a decode.
static bool sDebug = false;
static int artBlocks = 0;
static int artMaxBlockPixels = 0;

// One decode in flight. JPEGDEC has no user pointer argument on `decode()`, so
// the draw callback reads this through `pDraw->pUser`.
struct ArtDraw {
  uint16_t *pixels;
  int width;
  int height;
};

static int artDrawBlock(JPEGDRAW *draw) {
  ArtDraw *ctx = (ArtDraw *)draw->pUser;
  if (!ctx || !ctx->pixels) return 0;
  artBlocks++;
  if (draw->iWidth * draw->iHeight > artMaxBlockPixels) {
    artMaxBlockPixels = draw->iWidth * draw->iHeight;
  }
  for (int row = 0; row < draw->iHeight; row++) {
    int y = draw->y + row;
    if (y < 0 || y >= ctx->height) continue;
    const uint16_t *src = draw->pPixels + (size_t)row * draw->iWidth;
    uint16_t *dst = ctx->pixels + (size_t)y * ctx->width;
    for (int col = 0; col < draw->iWidthUsed; col++) {
      int x = draw->x + col;
      if (x < 0 || x >= ctx->width) continue;
      dst[x] = src[col];
    }
  }
  return 1;
}

// Scaled dimension JPEGDEC will emit for a fast-scale option.
static int scaledSide(int side, int shift) {
  return (side + (1 << shift) - 1) >> shift;
}

// Picks the coarsest power-of-two downscale that still leaves ~128px on the
// shorter side: enough source pixels for the area average below to actually
// smooth the JPEG grain instead of shuffling it around. A 300px image decodes at
// 1/2 (150px); a 64px thumbnail only has itself to give.
static int pickShift(int width, int height) {
  int shift = 3;
  while (shift > 0) {
    int side = width < height ? width : height;
    if (scaledSide(side, shift) >= SCREEN_MUSIC_ART_W * 2) break;
    shift--;
  }
  return shift;
}

static int scaleOption(int shift) {
  switch (shift) {
    case 3: return JPEG_SCALE_EIGHTH;
    case 2: return JPEG_SCALE_QUARTER;
    case 1: return JPEG_SCALE_HALF;
  }
  return 0;
}

// Decodes `data` into a SCREEN_MUSIC_ART_W RGB565 square. Returns false for anything that
// does not parse, leaving nothing allocated.
static bool decodeArtwork(const uint8_t *data, size_t len, uint16_t **out) {
  *out = nullptr;
  void *decoderMem = psramAlloc(sizeof(JPEGDEC));
  if (!decoderMem) {
    Serial.println("spotify art: sin PSRAM para el decodificador");
    return false;
  }
  JPEGDEC *jpeg = new (decoderMem) JPEGDEC();

  bool ok = false;
  uint16_t *scaled = nullptr;
  uint16_t *square = nullptr;
  ArtDraw ctx = {nullptr, 0, 0};

  if (!jpeg->openRAM((uint8_t *)data, (int)len, artDrawBlock)) {
    Serial.printf("spotify art: JPEG invalido (%d)\n", jpeg->getLastError());
    goto done;
  }

  {
    int width = jpeg->getWidth();
    int height = jpeg->getHeight();
    if (width <= 0 || height <= 0) goto done;

    int shift = pickShift(width, height);
    int scaledW = scaledSide(width, shift);
    int scaledH = scaledSide(height, shift);
    if (scaledW > SPOTIFY_ART_MAX_SIDE || scaledH > SPOTIFY_ART_MAX_SIDE) {
      Serial.printf("spotify art: imagen %dx%d fuera de rango\n", width, height);
      goto done;
    }

    scaled = (uint16_t *)psramAlloc((size_t)scaledW * scaledH * 2);
    square = (uint16_t *)psramAlloc(SCREEN_MUSIC_ART_W * SCREEN_MUSIC_ART_W * 2);
    if (!scaled || !square) {
      Serial.println("spotify art: sin PSRAM para los pixeles");
      goto done;
    }

    ctx.pixels = scaled;
    ctx.width = scaledW;
    ctx.height = scaledH;
    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
    jpeg->setUserPointer(&ctx);
    if (sDebug) {
      Serial.printf("art: %dx%d shift=%d scaled=%dx%d opt=%d blockMax=%d\n",
                    width, height, shift, scaledW, scaledH, scaleOption(shift),
                    artMaxBlockPixels);
    }
    artBlocks = 0;
    if (!jpeg->decode(0, 0, scaleOption(shift)) ||
        jpeg->getLastError() != JPEG_SUCCESS) {
      Serial.printf("spotify art: fallo el decode (%d)\n", jpeg->getLastError());
      goto done;
    }

    // Center-crop to a square before sampling, so a non-square image is not
    // stretched. The common case is already square and this is a plain resize.
    int crop = scaledW < scaledH ? scaledW : scaledH;
    int cropX = (scaledW - crop) / 2;
    int cropY = (scaledH - crop) / 2;
    // Area average, not point sampling: every target pixel takes the mean of the
    // source rectangle it covers. Point sampling a 150px cover into 64x64 keeps
    // the grain and makes the art look like coloured noise, which is exactly how
    // this was reported.
    for (int ty = 0; ty < SCREEN_MUSIC_ART_W; ty++) {
      int y0 = cropY + (int)((long)ty * crop / SCREEN_MUSIC_ART_W);
      int y1 = cropY + (int)((long)(ty + 1) * crop / SCREEN_MUSIC_ART_W);
      if (y1 <= y0) y1 = y0 + 1;
      if (y1 > scaledH) y1 = scaledH;
      for (int tx = 0; tx < SCREEN_MUSIC_ART_W; tx++) {
        int x0 = cropX + (int)((long)tx * crop / SCREEN_MUSIC_ART_W);
        int x1 = cropX + (int)((long)(tx + 1) * crop / SCREEN_MUSIC_ART_W);
        if (x1 <= x0) x1 = x0 + 1;
        if (x1 > scaledW) x1 = scaledW;
        uint32_t sr = 0, sg = 0, sb = 0;
        int n = 0;
        for (int y = y0; y < y1; y++) {
          const uint16_t *row = scaled + (size_t)y * scaledW;
          for (int x = x0; x < x1; x++) {
            uint16_t p = row[x];
            sr += (p >> 11) & 0x1F;
            sg += (p >> 5) & 0x3F;
            sb += p & 0x1F;
            n++;
          }
        }
        uint16_t r = n ? (uint16_t)(sr / n) : 0;
        uint16_t g = n ? (uint16_t)(sg / n) : 0;
        uint16_t b = n ? (uint16_t)(sb / n) : 0;
        square[ty * SCREEN_MUSIC_ART_W + tx] =
            (uint16_t)((r << 11) | (g << 5) | b);
      }
    }
    if (sDebug) {
      long sumR = 0, sumG = 0, sumB = 0;
      int minL = 255, maxL = 0;
      const int n = SCREEN_MUSIC_ART_W * SCREEN_MUSIC_ART_W;
      for (int i = 0; i < n; i++) {
        uint16_t p = square[i];
        int rr = (p >> 11) & 0x1F, gg = (p >> 5) & 0x3F, bb = p & 0x1F;
        sumR += rr; sumG += gg; sumB += bb;
        int lum = rr * 2 + gg + bb * 2;
        if (lum < minL) minL = lum;
        if (lum > maxL) maxL = lum;
      }
      Serial.printf("art: avg r=%ld g=%ld b=%ld lum %d..%d\n", sumR / n, sumG / n,
                    sumB / n, minL, maxL);
      for (int r = 0; r < 3; r++) {
        Serial.print("art px ");
        for (int col = 0; col < 8; col++) {
          Serial.printf("%04x ", square[r * SCREEN_MUSIC_ART_W + col]);
        }
        Serial.println();
      }
    }
    ok = true;
  }

done:
  jpeg->close();
  free(scaled);
  if (ok) {
    *out = square;
  } else {
    free(square);
  }
  free(decoderMem);
  return ok;
}

// Downloads and decodes one URL. Everything transient is PSRAM and released
// before this returns.
static bool fetchArtwork(const char *url, uint16_t **out) {
  *out = nullptr;
  uint8_t *jpegBytes = (uint8_t *)psramAlloc(SPOTIFY_ART_MAX_BYTES);
  if (!jpegBytes) {
    Serial.println("spotify art: sin PSRAM para la descarga");
    return false;
  }

  size_t got = 0;
  int status = 0;
  bool fetched = spotifyHttpGetBytes(url, jpegBytes, SPOTIFY_ART_MAX_BYTES, &got,
                                     &status);
  bool ok = false;
  if (!fetched || got < 4) {
    Serial.printf("spotify art: descarga fallo (http %d)\n", status);
  } else {
    ok = decodeArtwork(jpegBytes, got, out);
  }
  free(jpegBytes);
  return ok;
}

void spotifyArtBegin() {
  s_attemptKey[0] = '\0';
  s_attempted = false;
  s_attemptOk = false;
  s_attemptAtMs = 0;
}

void spotifyArtPoll(const SpotifyTrack &track) {
  if (!track.artUrl[0]) {
    musicClearArtwork();
    s_attempted = false;
    s_attemptKey[0] = '\0';
    return;
  }

  // Tracks always carry an id; fall back to the URL for anything that does not,
  // so an id-less response still caches instead of decoding every poll.
  const char *key = track.trackId[0] ? track.trackId : track.artUrl;

  // Already on the glass for this track, whoever put it there.
  if (musicHasArtwork() && track.trackId[0] &&
      strcmp(musicArtworkFor(), track.trackId) == 0) {
    return;
  }
  if (s_attempted && strcmp(s_attemptKey, key) == 0) {
    if (s_attemptOk && musicHasArtwork()) return;
    if (!s_attemptOk && millis() - s_attemptAtMs < SPOTIFY_ART_RETRY_MS) return;
  }

  snprintf(s_attemptKey, sizeof(s_attemptKey), "%s", key);
  s_attempted = true;
  s_attemptOk = false;
  s_attemptAtMs = millis();

  uint16_t *square = nullptr;
  if (!fetchArtwork(track.artUrl, &square)) {
    // A broken image must not leave the previous track's cover under this one.
    musicClearArtwork();
    return;
  }
  musicSetArtwork(square, SCREEN_MUSIC_ART_W);
  musicSetArtworkFor(track.trackId);
  free(square);
  s_attemptOk = true;
}


void spotifyArtSetDebug(bool on) { sDebug = on; }

void spotifyArtInvalidate() {
  s_attempted = false;
  s_attemptKey[0] = '\0';
  musicClearArtwork();
}

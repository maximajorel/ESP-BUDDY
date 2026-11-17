#include "screens.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "gfx.h"
#include "screen_music.h"

#include "text_fold.h"
#include "spotify.h"

// Cache of what is currently on the glass. The panel is only pushed when this
// changes, so an idle screen costs nothing and a second ticking on the progress
// bar costs one frame.
static struct {
  uint8_t feed;
  bool isPlaying;
  char trackId[SPOTIFY_TRACK_ID_MAX + 1];
  uint32_t bucket;  // seconds of progress already drawn
  char error[64];
  uint16_t artGen;
} s_drawn;

// Two 172x172 RGB565 squares (59 KB each). They live in PSRAM: the internal
// heap has to keep room for the TLS handshake, and the art is the biggest
// allocation on the screen.
static uint16_t *s_art[2] = {nullptr, nullptr};
// Which of the two buffers the glass is showing. The poll task decodes into the
// other one and then publishes this index with a single word write, so a frame is
// never drawn from a half-copied image. Before this, the photo of the panel
// showed colour noise that was really two covers mixed together.
static volatile int s_artIndex = 0;

static bool ensureArtBuffers() {
  if (s_art[0] && s_art[1]) return true;
  for (int i = 0; i < 2; i++) {
    if (!s_art[i]) {
      s_art[i] = (uint16_t *)heap_caps_malloc(
          (size_t)SCREEN_MUSIC_ART_W * SCREEN_MUSIC_ART_W * 2,
          MALLOC_CAP_SPIRAM);
    }
  }
  return s_art[0] && s_art[1];
}
// Bumped whenever the artwork changes, so the screen repaints even when the
// track itself did not change (the cover arrives a moment after the metadata).
static volatile uint16_t s_artGen = 0;
static bool s_artValid = false;
static char s_artFor[SPOTIFY_TRACK_ID_MAX + 1];

void musicEnter() {
  memset(&s_drawn, 0xFF, sizeof(s_drawn));
  s_drawn.bucket = 0xFFFFFFFFul;
  s_drawn.trackId[0] = '\0';
  s_drawn.error[0] = '\0';
  spotifySetActive(true);
  spotifyRequestRefresh();
}

void musicLeave() {
  // The Spotify poll stays active: the next visit shows the current track (and
  // its cover) without a "consultando" screen.
}

void musicSetArtwork(const uint16_t *pixels, int width) {
  if (!pixels || width != SCREEN_MUSIC_ART_W) {
    musicClearArtwork();
    return;
  }
  if (!ensureArtBuffers()) {
    musicClearArtwork();
    return;
  }
  int next = 1 - s_artIndex;
  memcpy(s_art[next], pixels,
         (size_t)SCREEN_MUSIC_ART_W * SCREEN_MUSIC_ART_W * 2);
  // Publish only after the copy is complete, and last, so a reader either sees
  // the previous complete image or this complete one.
  s_artValid = true;
  s_artIndex = next;
  s_artGen++;
}

void musicClearArtwork() {
  s_artValid = false;
  s_artFor[0] = '\0';
  s_artGen++;
}

bool musicHasArtwork() { return s_artValid && s_art[s_artIndex]; }

uint16_t musicArtworkGeneration() { return s_artGen; }

const char *musicArtworkFor() { return s_artFor; }

void musicSetArtworkFor(const char *trackId) {
  snprintf(s_artFor, sizeof(s_artFor), "%s", trackId ? trackId : "");
}

// --- drawing ----------------------------------------------------------------

#if SCREEN_MUSIC_SHOW_ART
static void drawNote(int x, int y, int size, uint16_t color) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 10;
  gfxFillCircle(cx - size / 8, cy + size / 8, r, color);
  gfxFillRect(cx - size / 32, cy - size / 5, size / 24, size / 3, color);
  gfxFillRect(cx - size / 32, cy - size / 5, size / 5, size / 32, color);
  gfxFillRect(cx + size / 6, cy - size / 5, size / 24, size / 8, color);
}

static void drawArtworkPlaceholder(int x, int y, int size) {
  gfxRect(x, y, size, size, CHROME_LINE);
  drawNote(x, y, size, CHROME_DIM);
}

// The decoded buffer is 172x172; the glass may ask for a smaller square.
static void drawArtwork(int x, int y, int size) {
  if (!musicHasArtwork()) {
    drawArtworkPlaceholder(x, y, size);
    return;
  }
  const uint16_t *art = s_art[s_artIndex];
  for (int row = 0; row < size; row++) {
    int sy = (int)((long)row * SCREEN_MUSIC_ART_W / size);
    const uint16_t *src = art + (size_t)sy * SCREEN_MUSIC_ART_W;
    for (int col = 0; col < size; col++) {
      int sx = (int)((long)col * SCREEN_MUSIC_ART_W / size);
      gfxPixel(x + col, y + row, src[sx]);
    }
  }
}
#endif  // SCREEN_MUSIC_SHOW_ART

// --- blurred cover backdrop -------------------------------------------------
//
// There is no GPU: the "blur" is a tiny colour field (24x13 area averages of the
// cover) upscaled with bilinear interpolation and dimmed. It is built once per
// artwork change, not per frame, and lives in PSRAM; the per-second redraw only
// copies it.
#define BG_W 320
#define BG_H 172
#define BG_GX 24
#define BG_GY 13

static uint16_t *s_bg = nullptr;  // BG_W * BG_H, PSRAM
static uint16_t s_bgGrid[BG_GY][BG_GX];
static uint32_t s_bgGen = 0xFFFFFFFFu;
static bool s_bgValid = false;

static void buildBackground() {
  s_bgValid = false;
  if (!musicHasArtwork()) return;
  if (!s_bg) {
    s_bg = (uint16_t *)heap_caps_malloc((size_t)BG_W * BG_H * 2,
                                        MALLOC_CAP_SPIRAM);
    if (!s_bg) return;
  }
  const uint16_t *art = s_art[s_artIndex];
  // Centre-crop the square cover to the 320:172 screen aspect before averaging,
  // so the colours are not squashed.
  const int cropH = SCREEN_MUSIC_ART_W * BG_H / BG_W;
  const int cropY = (SCREEN_MUSIC_ART_W - cropH) / 2;
  for (int gy = 0; gy < BG_GY; gy++) {
    int y0 = cropY + gy * cropH / BG_GY;
    int y1 = cropY + (gy + 1) * cropH / BG_GY;
    if (y1 <= y0) y1 = y0 + 1;
    for (int gx = 0; gx < BG_GX; gx++) {
      int x0 = gx * SCREEN_MUSIC_ART_W / BG_GX;
      int x1 = (gx + 1) * SCREEN_MUSIC_ART_W / BG_GX;
      if (x1 <= x0) x1 = x0 + 1;
      uint32_t sr = 0, sg = 0, sb = 0;
      int n = 0;
      for (int y = y0; y < y1; y++) {
        const uint16_t *row = art + (size_t)y * SCREEN_MUSIC_ART_W;
        for (int x = x0; x < x1; x++) {
          uint16_t pix = row[x];
          sr += (pix >> 11) & 0x1F;
          sg += (pix >> 5) & 0x3F;
          sb += pix & 0x1F;
          n++;
        }
      }
      uint16_t r = n ? (uint16_t)(sr / n) : 0;
      uint16_t g = n ? (uint16_t)(sg / n) : 0;
      uint16_t b = n ? (uint16_t)(sb / n) : 0;
      s_bgGrid[gy][gx] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }

  // Bilinear upscale, then dim so the white text and the sharp cover read on top.
  for (int y = 0; y < BG_H; y++) {
    float fy = ((float)y + 0.5f) * (float)BG_GY / (float)BG_H - 0.5f;
    if (fy < 0.0f) fy = 0.0f;
    int y0 = (int)fy;
    if (y0 > BG_GY - 1) y0 = BG_GY - 1;
    int y1 = y0 + 1 < BG_GY ? y0 + 1 : y0;
    float ty = fy - (float)y0;
    uint16_t *dst = s_bg + (size_t)y * BG_W;
    for (int x = 0; x < BG_W; x++) {
      float fx = ((float)x + 0.5f) * (float)BG_GX / (float)BG_W - 0.5f;
      if (fx < 0.0f) fx = 0.0f;
      int x0 = (int)fx;
      if (x0 > BG_GX - 1) x0 = BG_GX - 1;
      int x1 = x0 + 1 < BG_GX ? x0 + 1 : x0;
      float tx = fx - (float)x0;
      uint16_t c00 = s_bgGrid[y0][x0], c10 = s_bgGrid[y0][x1];
      uint16_t c01 = s_bgGrid[y1][x0], c11 = s_bgGrid[y1][x1];
      float r = (((c00 >> 11) & 31) * (1 - tx) + ((c10 >> 11) & 31) * tx) *
                    (1 - ty) +
                (((c01 >> 11) & 31) * (1 - tx) + ((c11 >> 11) & 31) * tx) * ty;
      float g = (((c00 >> 5) & 63) * (1 - tx) + ((c10 >> 5) & 63) * tx) *
                    (1 - ty) +
                (((c01 >> 5) & 63) * (1 - tx) + ((c11 >> 5) & 63) * tx) * ty;
      float b = ((c00 & 31) * (1 - tx) + (c10 & 31) * tx) * (1 - ty) +
                ((c01 & 31) * (1 - tx) + (c11 & 31) * tx) * ty;
      // 30% keeps it as atmosphere, not competition.
      dst[x] = (uint16_t)(((uint16_t)(r * 0.30f) << 11) |
                          ((uint16_t)(g * 0.30f) << 5) |
                          (uint16_t)(b * 0.30f));
    }
  }
  s_bgValid = true;
}

static void formatTime(uint32_t ms, char *out, size_t cap) {
  uint32_t total = ms / 1000;
  snprintf(out, cap, "%lu:%02lu", (unsigned long)(total / 60),
           (unsigned long)(total % 60));
}

// Greedy wrap on spaces into at most `maxLines` chunks of at most `maxChars`.
// Returns how many chunks were filled.
static int wrapLines(const char *text, int maxChars, int maxLines, char lines[][40]) {
  int n = 0;
  const char *p = text;
  while (*p && n < maxLines) {
    while (*p == ' ') p++;
    if (!*p) break;
    size_t len = strlen(p);
    size_t take = len <= (size_t)maxChars ? len : (size_t)maxChars;
    if (take < len) {
      size_t cut = take;
      while (cut > 0 && p[cut] != ' ') cut--;
      if (cut > 0) take = cut;
    }
    if (take > 39) take = 39;
    memcpy(lines[n], p, take);
    lines[n][take] = '\0';
    size_t k = strlen(lines[n]);
    while (k > 0 && lines[n][k - 1] == ' ') lines[n][--k] = '\0';
    if (k == 0) break;
    n++;
    p += take;
  }
  return n;
}

// Shorten `text` to fit `maxChars`, with a trailing ".." so it is obvious the
// line was cut rather than misread.
static void ellipsize(char *dst, size_t cap, const char *text, int maxChars) {
  snprintf(dst, cap, "%s", text ? text : "");
  size_t len = strlen(dst);
  if ((int)len <= maxChars) return;
  size_t keep = (size_t)maxChars;
  if (keep > 2) keep -= 2;
  dst[keep] = '\0';
  if (cap > keep + 3) strcat(dst, "..");
}

// Centred message plus a hint, for every state where there is no track to show.
static void drawMessage(const char *title, const char *hint) {
  gfxTextCentered(60, 2, title, GFX_WHITE);
  if (hint && *hint) gfxTextCentered(86, 1, hint, CHROME_DIM);
}

static void drawTrack(const SpotifySnapshot &snap, uint32_t nowMs) {
  // Centred cover, then the title (big), the artist (small) and the progress bar
  // at the bottom. The album line is gone: the title gets the room.
  drawArtwork(SCREEN_MUSIC_COVER_X, SCREEN_MUSIC_COVER_Y, SCREEN_MUSIC_COVER_SIZE);

  char folded[96];
  char lines[2][40];
  textFoldAscii(snap.track.title, folded, sizeof(folded));
  // 12 px of padding on each side leaves 296 px for the text.
  int titleLines = wrapLines(folded, (DISPLAY_W - 24) / (6 * 2) - 1, 2, lines);
  if (titleLines == 0) {
    snprintf(lines[0], sizeof(lines[0]), "SIN TITULO");
    titleLines = 1;
  }
  // Reserve two lines so a one-line title sits centred in the same block.
  int titleY = 104 + (2 - titleLines) * 8;
  for (int i = 0; i < titleLines; i++) {
    gfxTextCentered(titleY, 2, lines[i], GFX_WHITE);
    titleY += 16;
  }

  char line[64];
  if (snap.track.artist[0] != '\0') {
    textFoldAscii(snap.track.artist, folded, sizeof(folded));
    ellipsize(line, sizeof(line), folded, (DISPLAY_W - 24) / 6 - 1);
    gfxTextCentered(138, 1, line, gfxHex(0xC8C8C8));
  }

  // Progress: interpolated from the last poll so the bar moves between polls
  // instead of jumping every ten seconds. Clamped, because Spotify reports a
  // progress larger than the duration during a crossfade.
  uint32_t duration = snap.track.durationMs;
  if (duration > 0) {
    uint32_t progress = snap.track.progressMs;
    if (snap.track.isPlaying && snap.fetchedAtMs) {
      progress += millis() - snap.fetchedAtMs;
    }
    if (progress > duration) progress = duration;
    const int barX = 12;
    const int barW = DISPLAY_W - 24;
    const int barY = 150;
    gfxRect(barX, barY, barW, 6, CHROME_LINE);
    int filled = (int)((long long)(barW - 2) * progress / duration);
    if (filled > 0) {
      gfxFillRect(barX + 1, barY + 1, filled, 4,
                  snap.track.isPlaying ? gfxHex(0x1DB954) : CHROME_DIM);
    }
    char left[12];
    char right[12];
    formatTime(progress, left, sizeof(left));
    formatTime(duration, right, sizeof(right));
    gfxText(barX, barY + 8, 1, left, CHROME_DIM);
    gfxTextRight(barX + barW, barY + 8, 1, right, CHROME_DIM);
  }
}

bool musicTick(uint32_t nowMs) {
  SpotifySnapshot snap;
  spotifySnapshot(&snap);

  // Redraw only on a visible change: feed, playback flag, track, a new second of
  // progress, or a new error message.
  uint32_t bucket = snap.track.durationMs
                        ? (snap.track.progressMs + (snap.track.isPlaying
                                                        ? nowMs - snap.fetchedAtMs
                                                        : 0)) /
                              1000
                        : 0;
  bool changed = snap.feed != s_drawn.feed ||
                 snap.track.isPlaying != s_drawn.isPlaying ||
                 strcmp(snap.track.trackId, s_drawn.trackId) != 0 ||
                 bucket != s_drawn.bucket ||
                 strcmp(snap.error, s_drawn.error) != 0 ||
                 musicArtworkGeneration() != s_drawn.artGen;
  if (!changed) return false;

  s_drawn.feed = snap.feed;
  s_drawn.isPlaying = snap.track.isPlaying;
  snprintf(s_drawn.trackId, sizeof(s_drawn.trackId), "%s", snap.track.trackId);
  s_drawn.bucket = bucket;
  s_drawn.artGen = musicArtworkGeneration();
  snprintf(s_drawn.error, sizeof(s_drawn.error), "%s", snap.error);

  if (musicArtworkGeneration() != s_bgGen) {
    s_bgGen = musicArtworkGeneration();
    buildBackground();
  }
  gfxClear(GFX_BLACK);
  if (s_bgValid) memcpy(gfxBuffer(), s_bg, (size_t)BG_W * BG_H * 2);

  switch (snap.auth) {
    case SPOTIFY_AUTH_UNCONFIGURED:
      drawMessage("SIN CONFIGURAR", "Panel web > Spotify > client id");
      break;
    case SPOTIFY_AUTH_NEEDS_CODE:
      drawMessage("FALTA AUTORIZAR", "Panel web > Spotify > autorizar");
      break;
    case SPOTIFY_AUTH_NO_WIFI:
      drawMessage("SIN WIFI", "Red > conectate a tu red");
      break;
    default:
      // The track stays on the glass while the poll runs in the background:
      // no "consultando" screen. Only a real no-playback or error gets a message.
      if (snap.track.trackId[0] != '\0' || snap.track.durationMs) {
        drawTrack(snap, nowMs);
      } else if (snap.feed == SPOTIFY_FEED_NOTHING) {
        drawMessage("NADA SONANDO", "Dale play en Spotify");
      } else if (snap.feed == SPOTIFY_FEED_ERROR) {
        drawMessage("ERROR", snap.error[0] ? snap.error : "sin detalle");
      } else {
        drawArtworkPlaceholder(SCREEN_MUSIC_COVER_X, SCREEN_MUSIC_COVER_Y, SCREEN_MUSIC_COVER_SIZE);
      }
      break;
  }
  return true;
}

void musicShortPress() {
  // Read-only by design: the short press does not touch playback, which is what
  // keeps this screen usable without Spotify Premium.
}

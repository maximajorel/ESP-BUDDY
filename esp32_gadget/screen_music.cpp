#include "screens.h"

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

static uint16_t s_art[2][SCREEN_MUSIC_ART_W * SCREEN_MUSIC_ART_W];
// Which of the two buffers the glass is showing. The poll task decodes into the
// other one and then publishes this index with a single word write, so a frame is
// never drawn from a half-copied image. Before this, the photo of the panel
// showed colour noise that was really two covers mixed together.
static volatile int s_artIndex = 0;
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

void musicLeave() { spotifySetActive(false); }

void musicSetArtwork(const uint16_t *pixels, int width) {
  if (!pixels || width != SCREEN_MUSIC_ART_W) {
    musicClearArtwork();
    return;
  }
  int next = 1 - s_artIndex;
  memcpy(s_art[next], pixels, sizeof(s_art[0]));
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

bool musicHasArtwork() { return s_artValid; }

uint16_t musicArtworkGeneration() { return s_artGen; }

const char *musicArtworkFor() { return s_artFor; }

void musicSetArtworkFor(const char *trackId) {
  snprintf(s_artFor, sizeof(s_artFor), "%s", trackId ? trackId : "");
}

// --- drawing ----------------------------------------------------------------

#if SCREEN_MUSIC_SHOW_ART
// A note drawn from primitives: no font glyph for it, and an empty grey square
// looks like a bug rather than a placeholder.
static void drawNote(int x, int y, uint16_t color) {
  gfxFillCircle(x + 22, y + 44, 7, color);
  gfxFillRect(x + 28, y + 18, 2, 28, color);
  gfxHLine(x + 28, y + 18, 12, color);
  gfxVLine(x + 39, y + 18, 12, color);
}

static void drawArtworkPlaceholder(int x, int y) {
  gfxRect(x, y, SCREEN_MUSIC_ART_W, SCREEN_MUSIC_ART_W, CHROME_LINE);
  drawNote(x, y, CHROME_DIM);
}

static void drawArtwork(int x, int y) {
  if (!s_artValid) {
    drawArtworkPlaceholder(x, y);
    return;
  }
  // One read of the published index: the decoder never writes this buffer again
  // until it has published the other one.
  const uint16_t *art = s_art[s_artIndex];
  for (int row = 0; row < SCREEN_MUSIC_ART_W; row++) {
    for (int col = 0; col < SCREEN_MUSIC_ART_W; col++) {
      gfxPixel(x + col, y + row, art[row * SCREEN_MUSIC_ART_W + col]);
    }
  }
}

#endif  // SCREEN_MUSIC_SHOW_ART

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
  // Text only, centred: the panel is small and the title is what the owner reads
  // from across the room. Title at scale 3 on up to two lines, then artist and
  // album underneath.
  const int titleScale = 3;
  const int titleLineH = 8 * titleScale;      // 24
  const int titleMaxChars = (DISPLAY_W - 12) / (6 * titleScale);

  // The font stops at ASCII 125, so a dieresis or a tilde would draw as a solid
  // block. Fold to the base letters for the glass only; the panel and the API
  // keep the real text.
  char folded[96];

  char lines[2][40];
  textFoldAscii(snap.track.title, folded, sizeof(folded));
  int titleLines = wrapLines(folded, titleMaxChars, 2, lines);
  if (titleLines == 0) {
    snprintf(lines[0], sizeof(lines[0]), "SIN TITULO");
    titleLines = 1;
  }

  bool hasArtist = snap.track.artist[0] != '\0';
  bool hasAlbum = snap.track.album[0] != '\0';
  const int smallLineH = 10;

  int blockH = titleLines * titleLineH + 4;
  if (hasArtist) blockH += smallLineH;
  if (hasAlbum) blockH += smallLineH;

  const int bodyTop = 4;
  const int bodyBottom = 132;  // progress bar sits below this
  int y = bodyTop + (bodyBottom - bodyTop - blockH) / 2;
  if (y < bodyTop) y = bodyTop;

  for (int i = 0; i < titleLines; i++) {
    gfxTextCentered(y, titleScale, lines[i], GFX_WHITE);
    y += titleLineH;
  }
  y += 4;

  char line[64];
  if (hasArtist) {
    // 53 characters fit at scale 1 across 320px; cut with a marker instead of
    // running off the edge or silently losing the end.
    textFoldAscii(snap.track.artist, folded, sizeof(folded));
    ellipsize(line, sizeof(line), folded, DISPLAY_W / 6 - 1);
    gfxTextCentered(y, 1, line, gfxHex(0xC8C8C8));
    y += smallLineH;
  }
  if (hasAlbum) {
    textFoldAscii(snap.track.album, folded, sizeof(folded));
    ellipsize(line, sizeof(line), folded, DISPLAY_W / 6 - 1);
    gfxTextCentered(y, 1, line, CHROME_DIM);
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
    const int barX = 8;
    const int barW = DISPLAY_W - 16;
    gfxRect(barX, 138, barW, 6, CHROME_LINE);
    int filled = (int)((long long)(barW - 2) * progress / duration);
    if (filled > 0) gfxFillRect(barX + 1, 139, filled, 4, gfxHex(0x1DB954));
    char left[12];
    char right[12];
    formatTime(progress, left, sizeof(left));
    formatTime(duration, right, sizeof(right));
    gfxText(barX, 148, 1, left, CHROME_DIM);
    gfxTextRight(barX + barW, 148, 1, right, CHROME_DIM);
  }

  chromeFooter(snap.track.isPlaying ? "SONANDO AHORA" : "EN PAUSA");
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

  gfxClear(GFX_BLACK);

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
      if (snap.feed == SPOTIFY_FEED_TRACK) {
        drawTrack(snap, nowMs);
      } else if (snap.feed == SPOTIFY_FEED_LOADING ||
                 snap.feed == SPOTIFY_FEED_IDLE) {
        drawMessage("CONSULTANDO...", "Spotify");
      } else if (snap.feed == SPOTIFY_FEED_NOTHING) {
        drawMessage("NADA SONANDO", "Dale play en Spotify");
      } else {
        drawMessage("ERROR", snap.error[0] ? snap.error : "sin detalle");
      }
      break;
  }
  return true;
}

void musicShortPress() {
  // Read-only by design: the short press does not touch playback, which is what
  // keeps this screen usable without Spotify Premium.
}

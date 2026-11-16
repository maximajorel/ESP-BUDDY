// Host-side test for the pure Spotify layers: the currently-playing parser and
// the PKCE helpers.
//
//   g++ -std=c++17 -O1 -I esp32_gadget -I "<ArduinoJson>/src" \
//       tools/spotify_core_test.cpp esp32_gadget/spotify_parse.cpp \
//       esp32_gadget/spotify_pkce.cpp -o /tmp/spotify_core_test
//   /tmp/spotify_core_test
//
// Both files under test are compiled for the host as well, on purpose: the
// parser and the challenge computation are the two places where a wrong answer
// is invisible on the panel (a truncated title, a challenge Spotify silently
// rejects), so they are pinned here instead of by squinting at the glass.

#include <cstdio>
#include <cstring>

#include "spotify_parse.h"
#include "spotify_pkce.h"

static int g_failures = 0;

static void check(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-58s got=%d want=%d\n", what, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-58s %d\n", what, (int)got);
  }
}

static void checkStr(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-58s got=\"%s\" want=\"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-58s \"%s\"\n", what, got);
  }
}

static void checkU32(const char *what, unsigned long got, unsigned long want) {
  if (got != want) {
    printf("FAIL %-58s got=%lu want=%lu\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-58s %lu\n", what, got);
  }
}

// A playing track, with the artist list in the shape Spotify really sends and
// the image array deliberately not in descending order: picking images[0] would
// pass a naive test and fail on the panel with a 640px JPEG.
static const char *PLAYING_JSON =
    "{\"is_playing\":true,"
    "\"progress_ms\":102000,"
    "\"item\":{\"type\":\"track\",\"id\":\"4uLU6hMCjMI75M1A2tKUQC\","
    "\"name\":\"Never Gonna Give You Up\","
    "\"duration_ms\":213573,"
    "\"artists\":[{\"name\":\"Rick Astley\"},{\"name\":\"Guest Voice\"}],"
    "\"album\":{\"name\":\"Whenever You Need Somebody\","
    "\"images\":[{\"url\":\"https://i.scdn.co/image/huge640\",\"width\":640,\"height\":640},"
    "{\"url\":\"https://i.scdn.co/image/mid300\",\"width\":300,\"height\":300},"
    "{\"url\":\"https://i.scdn.co/image/small64\",\"width\":64,\"height\":64}]}}}";

// Paused: the item is still there, only the flag changes.
static const char *PAUSED_JSON =
    "{\"is_playing\":false,\"progress_ms\":5000,"
    "\"item\":{\"type\":\"track\",\"id\":\"abc\",\"name\":\"Nightcall\","
    "\"duration_ms\":258000,\"artists\":[{\"name\":\"Kavinsky\"}],"
    "\"album\":{\"name\":\"OutRun\",\"images\":[]}}}";

// A podcast episode: no album, a show instead. The screen must still have
// something honest to print rather than going blank while audio is playing.
static const char *EPISODE_JSON =
    "{\"is_playing\":true,\"progress_ms\":1000,"
    "\"item\":{\"type\":\"episode\",\"id\":\"ep1\",\"name\":\"Episode 42\","
    "\"duration_ms\":3600000,"
    "\"show\":{\"name\":\"Darknet Diaries\","
    "\"images\":[{\"url\":\"https://i.scdn.co/image/show64\",\"width\":64,\"height\":64}]}}}";

// Hex rendering of a digest, so the known-answer vectors are readable.
static void sha256Hex(const char *input, char *out, size_t outCap) {
  uint8_t digest[32];
  spotifySha256((const uint8_t *)input, strlen(input), digest);
  static const char HEX[] = "0123456789abcdef";
  size_t o = 0;
  for (size_t i = 0; i < 32 && o + 2 < outCap; i++) {
    out[o++] = HEX[digest[i] >> 4];
    out[o++] = HEX[digest[i] & 0x0F];
  }
  out[o] = '\0';
}

int main() {
  // --- parser ---------------------------------------------------------------

  // 1. A playing track exposes every field the screen prints.
  {
    SpotifyTrack t;
    SpotifyParseResult r =
        spotifyParseCurrentlyPlaying(PLAYING_JSON, strlen(PLAYING_JSON), &t);
    check("playing track parses", r == SPOTIFY_PARSE_OK, true);
    check("playing flag carried", t.isPlaying, true);
    checkStr("title", t.title, "Never Gonna Give You Up");
    checkStr("artists joined with a comma", t.artist, "Rick Astley, Guest Voice");
    checkStr("album", t.album, "Whenever You Need Somebody");
    checkStr("track id", t.trackId, "4uLU6hMCjMI75M1A2tKUQC");
    checkU32("progress ms", t.progressMs, 102000);
    checkU32("duration ms", t.durationMs, 213573);
  }

  // 2. The artwork keeps enough source detail to be downscaled cleanly: the 64px
  //    thumbnail is already a destroyed JPEG, so the smallest image that still
  //    has detail (>=128px) wins, regardless of array position.
  {
    SpotifyTrack t;
    spotifyParseCurrentlyPlaying(PLAYING_JSON, strlen(PLAYING_JSON), &t);
    checkStr("a >=128px source is preferred over the thumbnail", t.artUrl,
             "https://i.scdn.co/image/mid300");
  }

  // 3. With nothing at >=128px, the smallest available is used, never the largest.
  {
    static const char *json =
        "{\"is_playing\":true,\"item\":{\"type\":\"track\",\"name\":\"X\","
        "\"artists\":[{\"name\":\"Y\"}],"
        "\"album\":{\"name\":\"Z\",\"images\":["
        "{\"url\":\"https://i.scdn.co/image/big640\",\"width\":640},"
        "{\"url\":\"https://i.scdn.co/image/mid300\",\"width\":300}]}}}";
    SpotifyTrack t;
    SpotifyParseResult r = spotifyParseCurrentlyPlaying(json, strlen(json), &t);
    check("no large image still parses", r == SPOTIFY_PARSE_OK, true);
    checkStr("smallest image wins", t.artUrl, "https://i.scdn.co/image/mid300");
  }

  // 4. Paused playback is still a track: the screen shows it greyed, not empty.
  {
    SpotifyTrack t;
    SpotifyParseResult r =
        spotifyParseCurrentlyPlaying(PAUSED_JSON, strlen(PAUSED_JSON), &t);
    check("paused track parses", r == SPOTIFY_PARSE_OK, true);
    check("paused flag carried", t.isPlaying, false);
    checkStr("paused title", t.title, "Nightcall");
    checkStr("empty image array leaves no artwork", t.artUrl, "");
  }

  // 5. A podcast episode uses the show as the second line.
  {
    SpotifyTrack t;
    SpotifyParseResult r =
        spotifyParseCurrentlyPlaying(EPISODE_JSON, strlen(EPISODE_JSON), &t);
    check("episode parses", r == SPOTIFY_PARSE_OK, true);
    checkStr("episode title", t.title, "Episode 42");
    checkStr("episode falls back to the show name", t.artist, "Darknet Diaries");
    checkStr("episode artwork from the show", t.artUrl,
             "https://i.scdn.co/image/show64");
  }

  // 6. Nothing playing (204, or an ad break with item:null) is EMPTY, and the
  //    caller's struct is not left holding the previous track.
  {
    SpotifyTrack t;
    memset(&t, 'x', sizeof(t));
    SpotifyParseResult r = spotifyParseCurrentlyPlaying("", 0, &t);
    check("empty body is EMPTY", r == SPOTIFY_PARSE_EMPTY, true);
    checkStr("empty body leaves title blank", t.title, "");
  }
  {
    static const char *json = "{\"is_playing\":true,\"item\":null}";
    SpotifyTrack t;
    memset(&t, 'x', sizeof(t));
    SpotifyParseResult r = spotifyParseCurrentlyPlaying(json, strlen(json), &t);
    check("null item is EMPTY", r == SPOTIFY_PARSE_EMPTY, true);
    checkStr("null item leaves title blank", t.title, "");
    check("null item clears the playing flag", t.isPlaying, false);
  }

  // 7. Broken JSON is an error, not a crash and not an empty screen.
  {
    static const char *json = "{\"is_playing\":";
    SpotifyTrack t;
    SpotifyParseResult r = spotifyParseCurrentlyPlaying(json, strlen(json), &t);
    check("truncated json is ERROR", r == SPOTIFY_PARSE_ERROR, true);
  }

  // 8. Over-long names are truncated to the buffer, always NUL-terminated.
  {
    char longName[400];
    memset(longName, 'A', sizeof(longName) - 1);
    longName[sizeof(longName) - 1] = '\0';
    static char json[1600];
    snprintf(json, sizeof(json),
             "{\"is_playing\":true,\"item\":{\"type\":\"track\",\"name\":\"%s\","
             "\"artists\":[{\"name\":\"%s\"}],\"album\":{\"name\":\"%s\"}}}",
             longName, longName, longName);
    SpotifyTrack t;
    SpotifyParseResult r = spotifyParseCurrentlyPlaying(json, strlen(json), &t);
    check("long names still parse", r == SPOTIFY_PARSE_OK, true);
    check("title truncated to the buffer", strlen(t.title) == SPOTIFY_TITLE_MAX, true);
    check("artist truncated to the buffer",
          strlen(t.artist) == SPOTIFY_ARTIST_MAX, true);
    check("album truncated to the buffer", strlen(t.album) == SPOTIFY_ALBUM_MAX, true);
  }

  // --- pkce -----------------------------------------------------------------

  // 9. RFC 7636 Appendix B: the published verifier must produce the published
  //    challenge. This is the one check that proves the whole S256 path.
  {
    char out[64];
    SpotifyPkceResult r = spotifyPkceChallenge(
        "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", out, sizeof(out));
    check("rfc7636 challenge computed", r == SPOTIFY_PKCE_OK, true);
    checkStr("rfc7636 challenge matches", out,
             "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
  }

  // 10. Base64url: no padding, url-safe alphabet.
  {
    char out[8];
    spotifyBase64UrlEncode((const uint8_t *)"", 0, out, sizeof(out));
    checkStr("base64url of nothing", out, "");
    const uint8_t zero = 0x00;
    spotifyBase64UrlEncode(&zero, 1, out, sizeof(out));
    checkStr("base64url of one zero byte", out, "AA");
    const uint8_t fs[3] = {0xFB, 0xFF, 0xBF};
    spotifyBase64UrlEncode(fs, 3, out, sizeof(out));
    checkStr("base64url uses - and _ not + and /", out, "-_-_");
  }

  // 11. Verifier length bounds from RFC 7636 (43..128 characters).
  {
    char v[200];
    memset(v, 'a', sizeof(v));
    v[42] = '\0';
    check("42 char verifier rejected", spotifyPkceVerifierValid(v), false);
    v[42] = 'a';
    v[43] = '\0';
    check("43 char verifier accepted", spotifyPkceVerifierValid(v), true);
    v[43] = 'a';
    v[128] = '\0';
    check("128 char verifier accepted", spotifyPkceVerifierValid(v), true);
    v[128] = 'a';
    v[129] = '\0';
    check("129 char verifier rejected", spotifyPkceVerifierValid(v), false);
    check("verifier with a space rejected",
          spotifyPkceVerifierValid("dBjftJeZ4CVP mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), false);
    check("verifier with a plus rejected",
          spotifyPkceVerifierValid("dBjftJeZ4CVP+mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), false);
    check("verifier with a slash rejected",
          spotifyPkceVerifierValid("dBjftJeZ4CVP/mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), false);
  }

  // 12. An undersized output buffer fails instead of overrunning it.
  {
    char out[8];
    memset(out, 'x', sizeof(out));
    SpotifyPkceResult r = spotifyPkceChallenge(
        "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", out, sizeof(out));
    check("short output buffer rejected", r == SPOTIFY_PKCE_BUFFER_TOO_SMALL, true);
    check("short output buffer left untouched", out[0] == '\0', true);
  }

  // 13. Form encoding, used both for the authorize URL query and the token
  //     request body. A stray '+' or an unescaped ':' makes Spotify answer with
  //     an invalid_grant that blames the user's code.
  {
    char out[128];
    spotifyUrlEncode("", out, sizeof(out));
    checkStr("urlencode of nothing", out, "");
    spotifyUrlEncode("abc-._~019", out, sizeof(out));
    checkStr("unreserved characters pass through", out, "abc-._~019");
    spotifyUrlEncode("a b", out, sizeof(out));
    checkStr("space becomes %20", out, "a%20b");
    spotifyUrlEncode("http://127.0.0.1:8888/callback", out, sizeof(out));
    checkStr("redirect uri is escaped", out,
             "http%3A%2F%2F127.0.0.1%3A8888%2Fcallback");
    spotifyUrlEncode("a b", out, 2);
    checkStr("short buffer yields nothing", out, "");
  }

  // 14. FIPS 180-2 known-answer vectors. The challenge above only proves one
  //    32-byte digest; these prove the hash over the padding boundaries too
  //    (empty, one block, and a message that spans two blocks).
  {
    char hex[80];
    sha256Hex("", hex, sizeof(hex));
    checkStr("sha256 of nothing", hex,
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256Hex("abc", hex, sizeof(hex));
    checkStr("sha256 of abc", hex,
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", hex,
             sizeof(hex));
    checkStr("sha256 across two blocks", hex,
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  }

  // 15. The paste box accepts whatever the browser left in the address bar. The
  //     parse lives in the firmware, not only in the panel's JavaScript, so the
  //     serial console and any other client get the same behaviour.
  {
    char out[256];
    struct {
      const char *input;
      SpotifyCodeResult want;
      const char *code;
    } cases[] = {
        {"https://example.com/cb?code=AQDabc123&state=xyz", SPOTIFY_CODE_OK,
         "AQDabc123"},
        {"http://127.0.0.1:8888/callback?code=AQDabc123", SPOTIFY_CODE_OK,
         "AQDabc123"},
        {"code=AQDabc123&state=1", SPOTIFY_CODE_OK, "AQDabc123"},
        {"code=AQDabc123", SPOTIFY_CODE_OK, "AQDabc123"},
        {"AQDabc123", SPOTIFY_CODE_OK, "AQDabc123"},
        {"https://example.com/cb?code=AQ%2Dbc_9&state=1", SPOTIFY_CODE_OK,
         "AQ-bc_9"},
        {"https://example.com/cb?code=AQDabc123#frag", SPOTIFY_CODE_OK,
         "AQDabc123"},
        {"https://example.com/cb?error=access_denied&state=xyz",
         SPOTIFY_CODE_DENIED, ""},
        {"https://example.com/cb?state=xyz", SPOTIFY_CODE_EMPTY, ""},
        {"", SPOTIFY_CODE_EMPTY, ""},
        {"   ", SPOTIFY_CODE_EMPTY, ""},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      memset(out, 'x', sizeof(out));
      SpotifyCodeResult got = spotifyExtractCode(cases[i].input, out, sizeof(out));
      char label[72];
      snprintf(label, sizeof(label), "code from \"%.40s\"", cases[i].input);
      check(label, got == cases[i].want, true);
      if (cases[i].want == SPOTIFY_CODE_OK) {
        snprintf(label, sizeof(label), "  value is \"%s\"", cases[i].code);
        checkStr(label, out, cases[i].code);
      }
    }

    // An undersized buffer must refuse rather than truncate: a half code is a
    // failed authorization that looks like a server problem.
    SpotifyCodeResult small =
        spotifyExtractCode("https://example.com/cb?code=AQDabc123", out, 4);
    check("short buffer refused", small == SPOTIFY_CODE_BUFFER_TOO_SMALL, true);
  }

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
  return g_failures == 0 ? 0 : 1;
}

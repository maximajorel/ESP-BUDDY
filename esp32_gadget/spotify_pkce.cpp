#include "spotify_pkce.h"

#include <string.h>

#include "sha256.h"

// --- base64url --------------------------------------------------------------

static const char B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void spotifyBase64UrlEncode(const uint8_t *data, size_t len, char *out,
                            size_t outCap) {
  if (!out || outCap == 0) return;
  // 4 characters per 3 bytes, minus the padding we never write.
  size_t needed = (len * 8 + 5) / 6;
  if (outCap < needed + 1) {
    out[0] = '\0';
    return;
  }
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)data[i] << 16;
    bool has2 = i + 1 < len;
    bool has3 = i + 2 < len;
    if (has2) v |= (uint32_t)data[i + 1] << 8;
    if (has3) v |= (uint32_t)data[i + 2];
    out[o++] = B64URL[(v >> 18) & 0x3F];
    out[o++] = B64URL[(v >> 12) & 0x3F];
    if (has2) out[o++] = B64URL[(v >> 6) & 0x3F];
    if (has3) out[o++] = B64URL[v & 0x3F];
  }
  out[o] = '\0';
}

// --- url encoding -----------------------------------------------------------

// Only the unreserved set (RFC 3986) survives unescaped, which is what both the
// authorize query and the token body need.
void spotifyUrlEncode(const char *in, char *out, size_t outCap) {
  if (!out || outCap == 0) return;
  if (!in) {
    out[0] = '\0';
    return;
  }
  static const char HEX[] = "0123456789ABCDEF";
  size_t o = 0;
  for (size_t i = 0; in[i]; i++) {
    unsigned char c = (unsigned char)in[i];
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                 c == '~';
    size_t need = plain ? 1 : 3;
    if (o + need + 1 > outCap) {
      out[0] = '\0';
      return;
    }
    if (plain) {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = HEX[c >> 4];
      out[o++] = HEX[c & 0x0F];
    }
  }
  out[o] = '\0';
}

// --- code extraction --------------------------------------------------------

static int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Finds `name` used as a query parameter: at the start of the string, or right
// after '?' or '&'. A bare substring search would match "nickname=" for "name=".
static const char *findParam(const char *input, const char *name) {
  size_t len = strlen(name);
  for (const char *p = input; *p; p++) {
    if (strncmp(p, name, len) != 0) continue;
    if (p == input || p[-1] == '?' || p[-1] == '&') return p;
  }
  return nullptr;
}

// Copies a value up to '&', '#', whitespace or the end, decoding %XX and '+'.
// Returns 0 when empty, 1 when copied, 2 when it did not fit.
static int copyParamValue(const char *p, char *out, size_t cap) {
  size_t o = 0;
  while (*p && *p != '&' && *p != '#' && *p != ' ' && *p != '\t' && *p != '\r' &&
         *p != '\n') {
    char c = *p++;
    if (c == '%') {
      int hi = hexDigit(p[0]);
      int lo = hexDigit(p[1]);
      if (hi >= 0 && lo >= 0) {
        c = (char)((hi << 4) | lo);
        p += 2;
      }
    } else if (c == '+') {
      c = ' ';
    }
    if (o + 1 >= cap) return 2;
    out[o++] = c;
  }
  out[o] = '\0';
  return o > 0 ? 1 : 0;
}

SpotifyCodeResult spotifyExtractCode(const char *input, char *out,
                                    size_t outCap) {
  if (!out || outCap == 0) return SPOTIFY_CODE_BUFFER_TOO_SMALL;
  out[0] = '\0';
  if (!input) return SPOTIFY_CODE_EMPTY;

  while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') input++;
  if (!*input) return SPOTIFY_CODE_EMPTY;

  // A callback that was refused carries error=... and no code at all; reporting
  // that beats sending "access_denied" as if it were a code.
  if (findParam(input, "error=")) return SPOTIFY_CODE_DENIED;

  const char *codeAt = findParam(input, "code=");
  const char *value = codeAt ? codeAt + 5 : input;

  // A URL with no code parameter is not a code, no matter how it looks.
  if (!codeAt && (strncmp(input, "http://", 7) == 0 ||
                  strncmp(input, "https://", 8) == 0)) {
    return SPOTIFY_CODE_EMPTY;
  }

  switch (copyParamValue(value, out, outCap)) {
    case 1: return SPOTIFY_CODE_OK;
    case 2: return SPOTIFY_CODE_BUFFER_TOO_SMALL;
    default: return SPOTIFY_CODE_EMPTY;
  }
}

// --- pkce -------------------------------------------------------------------

bool spotifyPkceVerifierValid(const char *verifier) {
  if (!verifier) return false;
  size_t len = strlen(verifier);
  if (len < 43 || len > 128) return false;
  for (size_t i = 0; i < len; i++) {
    char c = verifier[i];
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                      c == '_' || c == '~';
    if (!unreserved) return false;
  }
  return true;
}

SpotifyPkceResult spotifyPkceChallengeFromDigest(const uint8_t digest[32],
                                                 char *out, size_t outCap) {
  if (!out || outCap == 0) return SPOTIFY_PKCE_BUFFER_TOO_SMALL;
  out[0] = '\0';
  // 43 characters plus the terminator.
  if (outCap < 44) return SPOTIFY_PKCE_BUFFER_TOO_SMALL;
  spotifyBase64UrlEncode(digest, 32, out, outCap);
  return SPOTIFY_PKCE_OK;
}

SpotifyPkceResult spotifyPkceChallenge(const char *verifier, char *out,
                                       size_t outCap) {
  if (out && outCap) out[0] = '\0';
  if (!spotifyPkceVerifierValid(verifier)) return SPOTIFY_PKCE_INVALID_VERIFIER;
  uint8_t digest[32];
  sha256((const uint8_t *)verifier, strlen(verifier), digest);
  return spotifyPkceChallengeFromDigest(digest, out, outCap);
}

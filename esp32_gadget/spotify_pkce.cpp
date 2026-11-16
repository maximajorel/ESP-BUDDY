#include "spotify_pkce.h"

#include <string.h>

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

// --- sha256 -----------------------------------------------------------------

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t rotr32(uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

static void sha256Block(uint32_t h[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
  uint32_t e = h[4], f = h[5], g = h[6], k = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = k + s1 + ch + K256[i] + w[i];
    uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    k = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += k;
}

void spotifySha256(const uint8_t *data, size_t len, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  size_t offset = 0;
  while (len - offset >= 64) {
    sha256Block(h, data + offset);
    offset += 64;
  }
  uint8_t tail[64];
  memset(tail, 0, sizeof(tail));
  size_t rest = len - offset;
  if (rest) memcpy(tail, data + offset, rest);
  tail[rest] = 0x80;
  // The length field needs eight bytes; if they do not fit, spill to a second
  // block. This is the boundary a hand-rolled hash usually gets wrong.
  if (rest >= 56) {
    sha256Block(h, tail);
    memset(tail, 0, sizeof(tail));
  }
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++) {
    tail[63 - i] = (uint8_t)(bits >> (8 * i));
  }
  sha256Block(h, tail);
  for (int i = 0; i < 8; i++) {
    out[i * 4 + 0] = (uint8_t)(h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)h[i];
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
  spotifySha256((const uint8_t *)verifier, strlen(verifier), digest);
  return spotifyPkceChallengeFromDigest(digest, out, outCap);
}

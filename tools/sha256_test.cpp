// Host-side test for the SHA-256 the firmware carries.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/sha256_test.cpp esp32_gadget/sha256.cpp -o /tmp/sha && /tmp/sha
//
// SHA-256 is used twice in this firmware and both uses are security-relevant: the
// S256 code challenge of the Spotify PKCE flow and the iterated digest behind the
// web panel's password. A hash that is subtly wrong still produces plausible hex,
// so the only honest check is the published vectors, and the only interesting
// cases are the ones a hand-rolled hash usually gets wrong: the padding boundary
// where the 64-bit length no longer fits the last block, an exact block, and a
// message long enough to need many blocks.
//
// Expected digests were generated with Python's hashlib (the FIPS 180-4 values
// for the empty message, "abc", the 448-bit and 896-bit messages and one million
// 'a's; the boundary lengths are the same algorithm applied to 'a' repeats).

#include <cstdio>
#include <cstring>

#include "sha256.h"

static int g_failures = 0;

static void hexOf(const uint8_t digest[32], char out[65]) {
  static const char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = kHex[digest[i] >> 4];
    out[i * 2 + 1] = kHex[digest[i] & 0x0F];
  }
  out[64] = '\0';
}

static void checkDigest(const char *what, const uint8_t *data, size_t len,
                        const char *want) {
  uint8_t digest[32];
  char got[65];
  sha256(data, len, digest);
  hexOf(digest, got);
  if (strcmp(got, want) != 0) {
    printf("FAIL %-46s\n     got  %s\n     want %s\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-46s %s\n", what, got);
  }
}

int main() {
  printf("-- FIPS 180-4 published vectors\n");
  checkDigest("the empty message", (const uint8_t *)"", 0,
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  checkDigest("\"abc\"", (const uint8_t *)"abc", 3,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // 56 bytes: the length field no longer fits the last block, so the padding
  // spills into a second one. This is the boundary a hand-rolled hash breaks.
  checkDigest("448-bit message (56 B, padding spills)",
              (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
              56, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  checkDigest("896-bit message (112 B)",
              (const uint8_t *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                              "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
              112, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");

  printf("-- lengths around one block (64 B)\n");
  static uint8_t a55[55], a56[56], a64[64];
  memset(a55, 'a', sizeof(a55));
  memset(a56, 'a', sizeof(a56));
  memset(a64, 'a', sizeof(a64));
  checkDigest("55 bytes: longest message that fits with its length", a55, 55,
              "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
  checkDigest("56 bytes: shortest message that spills", a56, 56,
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
  checkDigest("64 bytes: an exact block with no data left over", a64, 64,
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");

  printf("-- many blocks\n");
  static uint8_t million[1000000];
  memset(million, 'a', sizeof(million));
  checkDigest("one million 'a'", million, sizeof(million),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  printf("-- a null pointer with zero length is the empty message\n");
  checkDigest("null, 0", nullptr, 0,
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}

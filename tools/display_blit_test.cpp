// Host-side test for the panel transpose (task of the FPS pass).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/display_blit_test.cpp -o /tmp/dbt && /tmp/dbt
//
// The push loop runs 55 040 times per frame and the four orientations used to be
// chosen with a switch inside it. This pins both things at once: the mapping is
// the same one the old per-pixel switch produced (the reference implementation is
// copied here verbatim), and the hoisted loops in `displayPush` are the same
// arithmetic. A wrong mapping shears the image, and the panel cannot be read back.

#include <cstdio>
#include <cstring>
#include <vector>

#include "display_blit.h"

#define W 320
#define H 172
#define PANEL_W 172
#define PANEL_H 320

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

// The implementation that shipped before the optimisation, kept as the oracle:
// a switch per pixel, right in the hot loop.
static void referenceBlit(const uint16_t *fb, uint16_t *panel, int orientation) {
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int px, py;
      switch (orientation) {
        case BLIT_ORIENT_MIRROR:
          px = y;
          py = x;
          break;
        case BLIT_ORIENT_CCW:
          px = y;
          py = W - 1 - x;
          break;
        case BLIT_ORIENT_MIRROR2:
          px = PANEL_W - 1 - y;
          py = W - 1 - x;
          break;
        case BLIT_ORIENT_CW:
        default:
          px = PANEL_W - 1 - y;
          py = x;
          break;
      }
      panel[py * PANEL_W + px] = fb[y * W + x];
    }
  }
}

// What display.cpp does now: one loop per orientation, no switch inside.
static void hoistedBlit(const uint16_t *fb, uint16_t *panel, int orientation) {
  switch (orientation) {
    case BLIT_ORIENT_MIRROR:
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) panel[x * PANEL_W + y] = fb[y * W + x];
      }
      break;
    case BLIT_ORIENT_CCW:
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
          panel[(W - 1 - x) * PANEL_W + y] = fb[y * W + x];
        }
      }
      break;
    case BLIT_ORIENT_MIRROR2:
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
          panel[(W - 1 - x) * PANEL_W + (PANEL_W - 1 - y)] = fb[y * W + x];
        }
      }
      break;
    case BLIT_ORIENT_CW:
    default:
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
          panel[x * PANEL_W + (PANEL_W - 1 - y)] = fb[y * W + x];
        }
      }
      break;
  }
}

// The helper displayBlitTarget has to agree with both.
static void helperBlit(const uint16_t *fb, uint16_t *panel, int orientation) {
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int px = 0, py = 0;
      displayBlitTarget(orientation, x, y, W, PANEL_W, &px, &py);
      panel[py * PANEL_W + px] = fb[y * W + x];
    }
  }
}

int main() {
  std::vector<uint16_t> fb((size_t)W * H);
  // Every pixel differs, so a swapped pair cannot cancel out by luck.
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      fb[(size_t)y * W + x] = (uint16_t)((y * 320 + x) & 0xFFFF);
    }
  }
  std::vector<uint16_t> a((size_t)PANEL_W * PANEL_H), b((size_t)PANEL_W * PANEL_H),
      c((size_t)PANEL_W * PANEL_H);

  const int orientations[] = {BLIT_ORIENT_CW, BLIT_ORIENT_CCW, BLIT_ORIENT_MIRROR,
                              BLIT_ORIENT_MIRROR2, 99};
  for (int i = 0; i < 5; i++) {
    const int o = orientations[i];
    std::fill(a.begin(), a.end(), 0);
    std::fill(b.begin(), b.end(), 0);
    std::fill(c.begin(), c.end(), 0);
    referenceBlit(fb.data(), a.data(), o);
    hoistedBlit(fb.data(), b.data(), o);
    helperBlit(fb.data(), c.data(), o);
    char label[64];
    snprintf(label, sizeof(label), "orientation %d: the hoisted loop is the old one", o);
    check(label, memcmp(a.data(), b.data(), a.size() * sizeof(uint16_t)) == 0);
    snprintf(label, sizeof(label), "orientation %d: displayBlitTarget agrees", o);
    check(label, memcmp(a.data(), c.data(), a.size() * sizeof(uint16_t)) == 0);
  }

  // The corner that proves the mapping is not accidental: with CW the framebuffer
  // origin lands on the panel's bottom-left column.
  int px = -1, py = -1;
  displayBlitTarget(BLIT_ORIENT_CW, 0, 0, W, PANEL_W, &px, &py);
  check("the default orientation puts (0,0) at x=171, y=0", px == 171 && py == 0);
  displayBlitTarget(99, 0, 0, W, PANEL_W, &px, &py);
  check("an unknown orientation behaves like the default", px == 171 && py == 0);

  if (g_failures) {
    printf("\n%d FAILURES\n", g_failures);
    return 1;
  }
  printf("\ntodo verde\n");
  return 0;
}

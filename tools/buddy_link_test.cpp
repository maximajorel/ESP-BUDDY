// Host-side test for the bridge link cause classifier (S4/S5 of
// buddy-bridge-https).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_link_test.cpp -o /tmp/blt && /tmp/blt
//
// One rule per case, in the order the firmware applies them: a missing target is
// not "offline", an HTTPS attempt without a synced clock is not "offline" either,
// a Cloudflare block is not "the bridge did not answer", and a wrong token is not
// a block. The panel turns each name into one sentence the owner can act on, so
// the names are part of the contract.

#include <cstdio>
#include <cstring>

#include "buddy_link.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void expect(const char *what, BuddyLinkFacts facts, BuddyLink want) {
  BuddyLink got = buddyLinkFromFacts(facts);
  bool ok = got == want;
  if (!ok) printf("FAIL %s: got %s want %s\n", what, buddyLinkWireName(got),
                  buddyLinkWireName(want));
  check(what, ok);
}

int main() {
  printf("-- a healthy poll\n");
  expect("http 200", BuddyLinkFacts{true, true, false, false, 200, false, false},
         BUDDY_LINK_OK);
  expect("https 200 with clock",
         BuddyLinkFacts{true, true, true, true, 200, false, false}, BUDDY_LINK_OK);
  expect("202 accepted", BuddyLinkFacts{true, true, false, false, 202, false, false},
         BUDDY_LINK_OK);

  printf("-- nothing to talk to\n");
  expect("no url or token", BuddyLinkFacts{false, false, false, false, 0, false, false},
         BUDDY_LINK_UNCONFIGURED);
  expect("unconfigured beats a bad url",
         BuddyLinkFacts{false, false, false, false, 403, false, false},
         BUDDY_LINK_UNCONFIGURED);
  expect("bad url", BuddyLinkFacts{true, false, false, false, 0, false, false},
         BUDDY_LINK_BAD_URL);

  printf("-- https needs a clock before it needs a socket\n");
  expect("https without time",
         BuddyLinkFacts{true, true, true, false, 0, false, false},
         BUDDY_LINK_NO_TIME);
  expect("https without time beats the transport error",
         BuddyLinkFacts{true, true, true, false, 0, true, false},
         BUDDY_LINK_NO_TIME);
  expect("http without time is fine",
         BuddyLinkFacts{true, true, false, false, 0, false, false},
         BUDDY_LINK_OFFLINE);

  printf("-- a rejected handshake is not a silent bridge\n");
  expect("tls failure", BuddyLinkFacts{true, true, true, true, 0, true, false},
         BUDDY_LINK_TLS);

  printf("-- Cloudflare in front\n");
  expect("403 bot fight", BuddyLinkFacts{true, true, true, true, 403, false, false},
         BUDDY_LINK_BLOCKED);
  expect("403 over http", BuddyLinkFacts{true, true, false, false, 403, false, false},
         BUDDY_LINK_BLOCKED);
  expect("access redirect", BuddyLinkFacts{true, true, true, true, 0, false, true},
         BUDDY_LINK_BLOCKED);
  expect("302 is a block, not an offline bridge",
         BuddyLinkFacts{true, true, false, false, 302, false, false},
         BUDDY_LINK_BLOCKED);

  printf("-- the bridge answered, just not with a state\n");
  expect("wrong token", BuddyLinkFacts{true, true, false, false, 401, false, false},
         BUDDY_LINK_UNAUTHORIZED);
  expect("bridge 500", BuddyLinkFacts{true, true, false, false, 500, false, false},
         BUDDY_LINK_HTTP);
  expect("rate limited", BuddyLinkFacts{true, true, false, false, 429, false, false},
         BUDDY_LINK_HTTP);

  printf("-- never answered\n");
  expect("connection refused",
         BuddyLinkFacts{true, true, false, false, 0, false, false},
         BUDDY_LINK_OFFLINE);

  printf("-- names are stable\n");
  const char *names[] = {"ok",      "unconfigured", "bad-url", "no-time", "tls",
                         "blocked", "unauthorized", "http",    "offline"};
  for (int i = 0; i < BUDDY_LINK_COUNT; i++) {
    char what[64];
    snprintf(what, sizeof(what), "wire name %d", i);
    bool ok = strcmp(buddyLinkWireName((BuddyLink)i), names[i]) == 0;
    if (!ok) printf("FAIL wire name %d: got %s want %s\n", i,
                    buddyLinkWireName((BuddyLink)i), names[i]);
    check(what, ok);
  }
  check("unknown link falls back to offline",
        strcmp(buddyLinkWireName((BuddyLink)99), "offline") == 0);

  if (g_failures) {
    printf("\n%d failure(s)\n", g_failures);
    return 1;
  }
  printf("\nall ok\n");
  return 0;
}

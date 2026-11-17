// Host-side test for the bridge URL parser (S6 of buddy-bridge-https).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_url_test.cpp -o /tmp/but && /tmp/but
//
// The owner pastes this value by hand into the panel, so the parser is the only
// thing standing between a typo and a face that goes offline with no
// explanation. Two rules matter: both schemes are accepted (the LAN address and
// the public HTTPS one), and anything that is not a usable URL is rejected with
// a named cause instead of being stored.

#include <cstdio>
#include <cstring>

#include "buddy_url.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void checkStr(const char *what, const char *got, const char *want) {
  bool ok = strcmp(got, want) == 0;
  if (!ok) printf("FAIL %s: got \"%s\" want \"%s\"\n", what, got, want);
  check(what, ok);
}

static void checkErr(const char *what, BuddyUrlError got, BuddyUrlError want) {
  bool ok = got == want;
  if (!ok) printf("FAIL %s: got %s want %s\n", what, buddyUrlErrorName(got),
                  buddyUrlErrorName(want));
  check(what, ok);
}

int main() {
  BuddyUrl u;
  char buf[BUDDY_URL_MAX + 32];

  printf("-- the LAN address keeps working\n");
  checkErr("parse http ip+port", buddyUrlParse("http://192.168.100.64:8787", &u),
           BUDDY_URL_OK);
  check("http ip is not tls", !u.tls);
  checkStr("http ip host", u.host, "192.168.100.64");
  check("http ip port", u.port == 8787);
  checkStr("http ip base", u.base, "http://192.168.100.64:8787");
  check("join /state", buddyUrlJoin(u, "/state", buf, sizeof(buf)));
  checkStr("http endpoint", buf, "http://192.168.100.64:8787/state");
  check("join /buddy/ask", buddyUrlJoin(u, "/buddy/ask", buf, sizeof(buf)));
  checkStr("http ask endpoint", buf, "http://192.168.100.64:8787/buddy/ask");

  printf("-- the public https URL\n");
  checkErr("parse https host", buddyUrlParse("https://bridge.example.com", &u),
           BUDDY_URL_OK);
  check("https is tls", u.tls);
  checkStr("https host", u.host, "bridge.example.com");
  check("https default port", u.port == 443);
  checkStr("https base has no port", u.base, "https://bridge.example.com");
  check("join https", buddyUrlJoin(u, "/state", buf, sizeof(buf)));
  checkStr("https endpoint", buf, "https://bridge.example.com/state");

  printf("-- explicit https port\n");
  checkErr("parse https port", buddyUrlParse("https://host.lan:8443", &u),
           BUDDY_URL_OK);
  check("https port kept", u.port == 8443);
  checkStr("https base keeps port", u.base, "https://host.lan:8443");

  printf("-- http without port defaults to 80\n");
  checkErr("parse http bare", buddyUrlParse("http://host.lan", &u), BUDDY_URL_OK);
  check("http default port", u.port == 80);

  printf("-- pasted shapes\n");
  checkErr("trailing slash", buddyUrlParse("https://host.lan/", &u), BUDDY_URL_OK);
  checkStr("trailing slash stripped", u.base, "https://host.lan");
  check("endpoint from trailing slash", buddyUrlJoin(u, "/state", buf, sizeof(buf)));
  checkStr("trailing slash endpoint", buf, "https://host.lan/state");

  checkErr("surrounding spaces", buddyUrlParse("  https://host.lan  ", &u),
           BUDDY_URL_OK);
  checkStr("spaces trimmed", u.base, "https://host.lan");

  checkErr("uppercase scheme", buddyUrlParse("HTTPS://host.lan", &u), BUDDY_URL_OK);
  check("uppercase scheme is tls", u.tls);
  checkStr("scheme normalized", u.base, "https://host.lan");

  checkErr("base path", buddyUrlParse("https://host.lan/bridge", &u), BUDDY_URL_OK);
  checkStr("base path kept", u.base, "https://host.lan/bridge");
  check("join under base path", buddyUrlJoin(u, "/state", buf, sizeof(buf)));
  checkStr("base path endpoint", buf, "https://host.lan/bridge/state");

  checkErr("base path with slash", buddyUrlParse("https://host.lan/bridge/", &u),
           BUDDY_URL_OK);
  checkStr("base path slash stripped", u.base, "https://host.lan/bridge");

  checkErr("ipv6 literal", buddyUrlParse("http://[fe80::1]:8787", &u), BUDDY_URL_OK);
  checkStr("ipv6 host unbracketed", u.host, "fe80::1");
  check("ipv6 port", u.port == 8787);
  checkStr("ipv6 base keeps brackets", u.base, "http://[fe80::1]:8787");

  printf("-- rejected, never stored\n");
  checkErr("empty", buddyUrlParse("", &u), BUDDY_URL_EMPTY);
  checkErr("spaces only", buddyUrlParse("   ", &u), BUDDY_URL_EMPTY);
  checkErr("null", buddyUrlParse(nullptr, &u), BUDDY_URL_EMPTY);
  checkErr("no scheme", buddyUrlParse("192.168.100.64:8787", &u),
           BUDDY_URL_NO_SCHEME);
  checkErr("ftp scheme", buddyUrlParse("ftp://host.lan", &u), BUDDY_URL_BAD_SCHEME);
  checkErr("empty host", buddyUrlParse("https://", &u), BUDDY_URL_NO_HOST);
  checkErr("host with space", buddyUrlParse("https://ho st.lan", &u),
           BUDDY_URL_BAD_HOST);
  checkErr("non numeric port", buddyUrlParse("http://host.lan:http", &u),
           BUDDY_URL_BAD_PORT);
  checkErr("port zero", buddyUrlParse("http://host.lan:0", &u), BUDDY_URL_BAD_PORT);
  checkErr("port out of range", buddyUrlParse("http://host.lan:70000", &u),
           BUDDY_URL_BAD_PORT);
  checkErr("too long", buddyUrlParse(
                            "https://a-very-long-host-name-that-keeps-going-and-"
                            "going-until-it-does-not-fit.example.com/x",
                            &u),
           BUDDY_URL_TOO_LONG);
  checkErr("bad ipv6", buddyUrlParse("http://[fe80::1:8787", &u),
           BUDDY_URL_BAD_HOST);
  checkErr("space in base path", buddyUrlParse("http://host.lan/a b/c", &u),
           BUDDY_URL_BAD_PATH);
  checkErr("query in base path", buddyUrlParse("https://host.lan/base?x=1", &u),
           BUDDY_URL_BAD_PATH);
  checkErr("fragment in base path", buddyUrlParse("https://host.lan/base#f", &u),
           BUDDY_URL_BAD_PATH);

  printf("-- boundaries\n");
  checkErr("port 65535 ok", buddyUrlParse("http://host.lan:65535", &u), BUDDY_URL_OK);
  check("port 65535 stored", u.port == 65535);
  checkErr("port 65536 bad", buddyUrlParse("http://host.lan:65536", &u),
           BUDDY_URL_BAD_PORT);
  {
    // A value that fills the budget exactly must still parse and keep a NUL.
    char full[BUDDY_URL_MAX + 1];
    memset(full, 'a', sizeof(full));
    memcpy(full, "https://h.lan/", 14);
    full[BUDDY_URL_MAX] = '\0';
    checkErr("96 chars with a path", buddyUrlParse(full, &u), BUDDY_URL_OK);
    check("base fits the budget", strlen(u.base) == BUDDY_URL_MAX);
    full[BUDDY_URL_MAX] = 'a';
    full[BUDDY_URL_MAX + 1] = '\0';
    checkErr("97 chars", buddyUrlParse(full, &u), BUDDY_URL_TOO_LONG);
  }

  printf("-- endpoint buffer\n");
  checkErr("small buffer setup", buddyUrlParse("https://host.lan", &u), BUDDY_URL_OK);
  check("tiny buffer refuses", !buddyUrlJoin(u, "/state", buf, 8));

  printf("-- error names are stable\n");
  checkStr("ok name", buddyUrlErrorName(BUDDY_URL_OK), "ok");
  checkStr("no scheme name", buddyUrlErrorName(BUDDY_URL_NO_SCHEME), "no-scheme");
  checkStr("bad scheme name", buddyUrlErrorName(BUDDY_URL_BAD_SCHEME), "bad-scheme");
  checkStr("no host name", buddyUrlErrorName(BUDDY_URL_NO_HOST), "no-host");
  checkStr("bad host name", buddyUrlErrorName(BUDDY_URL_BAD_HOST), "bad-host");
  checkStr("bad port name", buddyUrlErrorName(BUDDY_URL_BAD_PORT), "bad-port");
  checkStr("too long name", buddyUrlErrorName(BUDDY_URL_TOO_LONG), "too-long");
  checkStr("bad path name", buddyUrlErrorName(BUDDY_URL_BAD_PATH), "bad-path");
  checkStr("empty name", buddyUrlErrorName(BUDDY_URL_EMPTY), "empty");

  if (g_failures) {
    printf("\n%d failure(s)\n", g_failures);
    return 1;
  }
  printf("\nall ok\n");
  return 0;
}

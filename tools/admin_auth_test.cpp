// Host-side test for the web authentication core (S5, S7, S8 of web-auth).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/admin_auth_test.cpp
//       esp32_gadget/admin_auth.cpp esp32_gadget/spotify_pkce.cpp -o /tmp/aat
//   /tmp/aat
//
// What matters here is the shape of the rules, not the plumbing: which requests
// need a session, when a token stops being good, when a failing login gets
// locked out, and that the stored digest is a function of salt, user and
// password together. Every case names the spec it pins.

#include <cstdio>
#include <cstring>

#include "admin_auth.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(const char *what, bool ok) {
  g_checks++;
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static const char *HEX_REFERENCE =
    "6e0c104b74e78fb9e3616b8c0051fbf0675c5702f7bfa3cbc8bf9fb4f90b79ba";

static void fillSalt(uint8_t salt[ADMIN_SALT_LEN]) {
  for (int i = 0; i < ADMIN_SALT_LEN; i++) salt[i] = (uint8_t)i;
}

// --- S8: derivation ---------------------------------------------------------

static void testDerivation() {
  uint8_t salt[ADMIN_SALT_LEN];
  fillSalt(salt);
  uint8_t digest[ADMIN_HASH_LEN];
  char hex[ADMIN_HASH_LEN * 2 + 1];

  // The reference digest was produced by an independent implementation
  // (Python hashlib in tools/gen_admin_defaults.py) from the same definition, so
  // the firmware and the build step cannot drift apart silently.
  adminHashDerive(salt, "admin", "s3cret-pass", digest);
  adminHexEncode(digest, ADMIN_HASH_LEN, hex, sizeof(hex));
  check("S8 derive matches the reference vector", strcmp(hex, HEX_REFERENCE) == 0);

  uint8_t again[ADMIN_HASH_LEN];
  adminHashDerive(salt, "admin", "s3cret-pass", again);
  check("S8 derive is deterministic",
        adminEqualConstantTime(digest, again, ADMIN_HASH_LEN));

  uint8_t other[ADMIN_HASH_LEN];
  uint8_t otherSalt[ADMIN_SALT_LEN];
  fillSalt(otherSalt);
  otherSalt[0] ^= 0xFF;
  adminHashDerive(otherSalt, "admin", "s3cret-pass", other);
  check("S8 a different salt gives a different digest",
        !adminEqualConstantTime(digest, other, ADMIN_HASH_LEN));

  adminHashDerive(salt, "admin2", "s3cret-pass", other);
  check("S8 a different user gives a different digest",
        !adminEqualConstantTime(digest, other, ADMIN_HASH_LEN));

  adminHashDerive(salt, "admin", "s3cret-pass2", other);
  check("S8 a different password gives a different digest",
        !adminEqualConstantTime(digest, other, ADMIN_HASH_LEN));

  adminHashDerive(salt, "", "", other);
  check("S8 an empty seed still derives (no crash, non-zero)",
        !adminEqualConstantTime(digest, other, ADMIN_HASH_LEN));

  check("S8 constant-time compare accepts equal bytes",
        adminEqualConstantTime(digest, digest, ADMIN_HASH_LEN));
  memset(other, 0, sizeof(other));
  memcpy(other, digest, ADMIN_HASH_LEN);
  other[ADMIN_HASH_LEN - 1] ^= 0x01;
  check("S8 constant-time compare rejects the last byte differing",
        !adminEqualConstantTime(digest, other, ADMIN_HASH_LEN));
  check("S8 constant-time compare rejects a null operand",
        !adminEqualConstantTime(nullptr, digest, ADMIN_HASH_LEN));
}

static void testCredentialsMatch() {
  uint8_t salt[ADMIN_SALT_LEN];
  fillSalt(salt);
  uint8_t stored[ADMIN_HASH_LEN];
  adminHashDerive(salt, "owner", "correct-horse", stored);

  check("S8 the stored pair matches", adminCredentialsMatch("owner", "correct-horse",
                                                            "owner", salt, stored));
  check("S8 a wrong password does not match",
        !adminCredentialsMatch("owner", "correct-hors", "owner", salt, stored));
  check("S8 a wrong user does not match",
        !adminCredentialsMatch("intruder", "correct-horse", "owner", salt, stored));
  check("S8 an empty password does not match",
        !adminCredentialsMatch("owner", "", "owner", salt, stored));
  check("S8 a null password does not match",
        !adminCredentialsMatch("owner", nullptr, "owner", salt, stored));
  check("S8 a null stored hash does not match",
        !adminCredentialsMatch("owner", "correct-horse", "owner", salt, nullptr));
}

// --- S8: input rules --------------------------------------------------------

static void testInputRules() {
  check("S8 user accepts a short name", adminUserValid("admin"));
  check("S8 user accepts dots and dashes", adminUserValid("maxi.home-1"));
  check("S8 user rejects empty", !adminUserValid(""));
  check("S8 user rejects null", !adminUserValid(nullptr));
  check("S8 user rejects a space", !adminUserValid("ad min"));
  check("S8 user rejects a colon", !adminUserValid("adm:in"));
  check("S8 user rejects a slash", !adminUserValid("adm/in"));
  check("S8 user rejects a quote", !adminUserValid("adm\"in"));
  check("S8 user rejects a newline", !adminUserValid("adm\nin"));
  check("S8 user rejects 33 characters",
        !adminUserValid("abcdefghijabcdefghijabcdefghijabc"));

  char maxUser[ADMIN_USER_MAX + 1];
  memset(maxUser, 'u', ADMIN_USER_MAX);
  maxUser[ADMIN_USER_MAX] = '\0';
  check("S8 user accepts exactly 32 characters", adminUserValid(maxUser));

  check("S8 password rejects 7 characters", !adminPassValid("1234567"));
  check("S8 password accepts 8 characters", adminPassValid("12345678"));
  check("S8 password rejects null", !adminPassValid(nullptr));
  check("S8 password rejects a control byte", !adminPassValid("1234\n678"));

  char maxPass[ADMIN_PASS_MAX + 1];
  memset(maxPass, 'p', ADMIN_PASS_MAX);
  maxPass[ADMIN_PASS_MAX] = '\0';
  check("S8 password accepts exactly 64 characters", adminPassValid(maxPass));
  char tooLong[ADMIN_PASS_MAX + 2];
  memset(tooLong, 'p', ADMIN_PASS_MAX + 1);
  tooLong[ADMIN_PASS_MAX + 1] = '\0';
  check("S8 password rejects 65 characters", !adminPassValid(tooLong));
}

static void testHex() {
  uint8_t bytes[4] = {0x00, 0x0F, 0xA5, 0xFF};
  char hex[9];
  adminHexEncode(bytes, sizeof(bytes), hex, sizeof(hex));
  check("S8 hex encodes lowercase", strcmp(hex, "000fa5ff") == 0);

  uint8_t back[4];
  check("S8 hex decodes what it encoded", adminHexDecode(hex, back, sizeof(back)));
  check("S8 hex round trip is byte exact", memcmp(bytes, back, sizeof(bytes)) == 0);

  check("S8 uppercase hex is accepted", adminHexDecode("000FA5FF", back, sizeof(back)));
  check("S8 hex rejects a short string", !adminHexDecode("000fa5f", back, sizeof(back)));
  check("S8 hex rejects a long string", !adminHexDecode("000fa5fff", back, sizeof(back)));
  check("S8 hex rejects a non-hex character",
        !adminHexDecode("000fa5fz", back, sizeof(back)));

  char tiny[4];
  adminHexEncode(bytes, sizeof(bytes), tiny, sizeof(tiny));
  check("S8 hex encode clears the output when the buffer is too small",
        tiny[0] == '\0');
}

// --- S1/S5: sessions --------------------------------------------------------

static void token(uint8_t out[ADMIN_HASH_LEN], uint8_t seed) {
  for (int i = 0; i < ADMIN_HASH_LEN; i++) out[i] = (uint8_t)(seed + i);
}

static void testSessions() {
  AdminSessionTable table;
  adminSessionReset(&table);
  uint8_t t1[ADMIN_HASH_LEN];
  uint8_t t2[ADMIN_HASH_LEN];
  token(t1, 1);
  token(t2, 200);
  char hex1[ADMIN_TOKEN_CAP];
  char hex2[ADMIN_TOKEN_CAP];
  adminHexEncode(t1, ADMIN_HASH_LEN, hex1, sizeof(hex1));
  adminHexEncode(t2, ADMIN_HASH_LEN, hex2, sizeof(hex2));

  check("S1 an empty table has no live session", adminSessionLive(&table, 0) == 0);
  check("S1 an unknown token is rejected", !adminSessionValid(&table, hex1, 0));

  adminSessionOpen(&table, 1000, t1);
  check("S1 a freshly opened session is valid",
        adminSessionValid(&table, hex1, 1000));
  check("S1 the table reports one live session",
        adminSessionLive(&table, 1000) == 1);
  check("S1 another token is still unknown",
        !adminSessionValid(&table, hex2, 1000));

  check("S1 a truncated token is rejected",
        !adminSessionValid(&table, "abc", 1000));
  char prefix[ADMIN_TOKEN_CAP];
  memcpy(prefix, hex1, ADMIN_TOKEN_HEX - 1);
  prefix[ADMIN_TOKEN_HEX - 1] = '\0';
  check("S1 a one-character-short prefix is rejected",
        !adminSessionValid(&table, prefix, 1000));
  check("S1 an empty token is rejected", !adminSessionValid(&table, "", 1000));

  // Absolute lifetime: it does not matter that the session was used a second
  // ago, it still expires, which is the point of capping it.
  check("S1 a session is valid one millisecond before its deadline",
        adminSessionValid(&table, hex1, 1000 + ADMIN_SESSION_TTL_MS - 1));
  check("S1 a session is dead at its deadline",
        !adminSessionValid(&table, hex1, 1000 + ADMIN_SESSION_TTL_MS));
  check("S1 an expired session is not counted",
        adminSessionLive(&table, 1000 + ADMIN_SESSION_TTL_MS) == 0);

  // The millis() wraparound must not resurrect or kill sessions by accident.
  uint32_t wrapBase = 0xFFFFFF00u;
  AdminSessionTable wrap;
  adminSessionReset(&wrap);
  adminSessionOpen(&wrap, wrapBase, t1);
  check("S1 a session survives the millis() wraparound",
        adminSessionValid(&wrap, hex1, (uint32_t)(wrapBase + 5000)));
  check("S1 a session still expires after the wraparound",
        !adminSessionValid(&wrap, hex1,
                           (uint32_t)(wrapBase + ADMIN_SESSION_TTL_MS + 1)));

  adminSessionOpen(&table, 2000, t2);
  check("S1 two sessions are both valid",
        adminSessionValid(&table, hex1, 2500) && adminSessionValid(&table, hex2, 2500));

  adminSessionClose(&table, hex1);
  check("S1 closing a session kills only that one",
        !adminSessionValid(&table, hex1, 2600) && adminSessionValid(&table, hex2, 2600));

  // Changing the password logs out the other devices but not the browser that
  // made the change.
  adminSessionOpen(&table, 3000, t1);
  adminSessionCloseOthers(&table, hex2);
  check("S1 closing others keeps the caller's session",
        adminSessionValid(&table, hex2, 3100));
  check("S1 closing others drops the rest",
        !adminSessionValid(&table, hex1, 3100));

  // A full table drops the oldest instead of failing the login.
  AdminSessionTable full;
  adminSessionReset(&full);
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    uint8_t bytes[ADMIN_HASH_LEN];
    token(bytes, (uint8_t)(10 + i));
    adminSessionOpen(&full, (uint32_t)(1000 + i), bytes);
  }
  check("S1 the table holds its maximum",
        adminSessionLive(&full, 5000) == ADMIN_SESSION_MAX);
  uint8_t extra[ADMIN_HASH_LEN];
  token(extra, 250);
  char hexExtra[ADMIN_TOKEN_CAP];
  adminHexEncode(extra, ADMIN_HASH_LEN, hexExtra, sizeof(hexExtra));
  uint8_t oldest[ADMIN_HASH_LEN];
  token(oldest, 10);
  char hexOldest[ADMIN_TOKEN_CAP];
  adminHexEncode(oldest, ADMIN_HASH_LEN, hexOldest, sizeof(hexOldest));
  adminSessionOpen(&full, 6000, extra);
  check("S1 a full table still accepts a login",
        adminSessionValid(&full, hexExtra, 6000));
  check("S1 the oldest session was the one dropped",
        !adminSessionValid(&full, hexOldest, 6000));
  check("S1 the table never exceeds its maximum",
        adminSessionLive(&full, 6000) == ADMIN_SESSION_MAX);

  adminSessionReset(&full);
  check("S1 reset empties the table", adminSessionLive(&full, 0) == 0);
}

// --- S7: throttle -----------------------------------------------------------

static void testThrottle() {
  AdminThrottle throttle;
  adminThrottleReset(&throttle);
  check("S7 a fresh throttle lets the first attempt through",
        !adminThrottleBlocked(&throttle, 0));

  for (int i = 0; i < ADMIN_MAX_FAILS - 1; i++) adminThrottleFail(&throttle, 1000);
  check("S7 four failures do not lock", !adminThrottleBlocked(&throttle, 1500));

  adminThrottleFail(&throttle, 1000);
  check("S7 the fifth failure locks", adminThrottleBlocked(&throttle, 1500));
  check("S7 the lock covers the whole window",
        adminThrottleBlocked(&throttle, 1000 + ADMIN_LOCK_MS - 1));
  check("S7 the lock is over at its deadline",
        !adminThrottleBlocked(&throttle, 1000 + ADMIN_LOCK_MS));

  // After the lock the counter starts again: the next wrong password must not
  // re-lock the device instantly, which would leave the owner with no way in.
  adminThrottleFail(&throttle, 1000 + ADMIN_LOCK_MS);
  check("S7 one failure after the lock does not re-lock immediately",
        !adminThrottleBlocked(&throttle, 1000 + ADMIN_LOCK_MS + 10));

  AdminThrottle reset;
  adminThrottleReset(&reset);
  for (int i = 0; i < ADMIN_MAX_FAILS - 1; i++) adminThrottleFail(&reset, 500);
  adminThrottleReset(&reset);
  for (int i = 0; i < ADMIN_MAX_FAILS - 1; i++) adminThrottleFail(&reset, 600);
  check("S7 a successful login clears the failure count",
        !adminThrottleBlocked(&reset, 700));

  AdminThrottle wrap;
  adminThrottleReset(&wrap);
  uint32_t base = 0xFFFFFF00u;
  for (int i = 0; i < ADMIN_MAX_FAILS; i++) adminThrottleFail(&wrap, base);
  check("S7 the lock survives the millis() wraparound",
        adminThrottleBlocked(&wrap, (uint32_t)(base + 10)));
  check("S7 the lock ends correctly after the wraparound",
        !adminThrottleBlocked(&wrap, (uint32_t)(base + ADMIN_LOCK_MS + 1)));
}

// --- S5: which requests need a session --------------------------------------

struct RouteCase {
  const char *method;
  const char *path;
  bool protectedRoute;
  const char *why;
};

static void testRoutes() {
  static const RouteCase cases[] = {
      // The configuration area.
      {"GET", "/admin", true, "GET /admin is the configuration panel"},
      {"GET", "/admin.html", true, "the packed name of the panel"},
      {"GET", "/admin/", true, "the panel with a trailing slash"},
      {"GET", "/admin/cuenta", true, "any page under /admin"},
      // Public pages and assets.
      {"GET", "/", false, "the future screen picker is public"},
      {"GET", "/index.html", false, "the picker's packed name"},
      {"GET", "/login", false, "the login page has to be reachable"},
      {"GET", "/login.html", false, "the login page's packed name"},
      {"GET", "/styles.css", false, "assets carry no secrets"},
      {"GET", "/app.js", false, "assets carry no secrets"},
      {"GET", "/admin.js", false, "assets carry no secrets"},
      // Public reads, so the games can read device state without a login.
      {"GET", "/api/status", false, "status is a read"},
      {"GET", "/api/scan", false, "the network scan is a read"},
      {"GET", "/api/spotify", false, "the Spotify state is a read"},
      {"GET", "/api/session", false, "the login page asks whether it is logged in"},
      // Public session endpoints.
      {"POST", "/api/session", false, "logging in cannot require being logged in"},
      {"POST", "/api/session/logout", false, "logging out is idempotent"},
      // The one public write: the screen picker lives outside the login.
      {"POST", "/api/screen", false,
       "choosing what the glass shows needs no session"},
      {"POST", "/api/screen/", true,
       "the public write is compared exactly"},
      {"POST", "/api/face", false,
       "the web game moves the face without a session"},
      {"POST", "/api/face/", true,
       "the face write is compared exactly"},
      {"POST", "/api/buddy/ask", true,
       "asking the bridge stays protected"},
      {"POST", "/api/press", true,
       "a button press is not the picker"},
      {"POST", "/api/screens/order", true,
       "reordering screens is configuration, not the picker"},
      // Everything that mutates.
      {"POST", "/api/wifi", true, "WiFi credentials"},
      {"POST", "/api/ap", true, "access point credentials"},
      {"POST", "/api/settings", true, "device settings"},
      {"POST", "/api/factory", true, "factory reset"},
      {"POST", "/api/restart", true, "restart"},
      {"POST", "/api/spotify/forget", true, "discards a stored token"},
      {"POST", "/api/screens/names", true, "renames screens"},
      {"POST", "/api/admin/credentials", true, "changing the password"},
      {"POST", "/api/does-not-exist", true,
       "an unknown write is refused before the dispatcher sees it"},
      {"POST", "/api/status", true, "a write against a read path is not public"},
      {"DELETE", "/api/wifi", true, "an unknown method under /api is not public"},
      {"PUT", "/api/settings", true, "an unknown method under /api is not public"},
      {"PATCH", "/api/settings", true, "an unknown method under /api is not public"},
      // Nothing a client can send may turn a protected route public.
      {"get", "/admin", true, "the method comparison is exact"},
      {"POST", "/admin", true, "the panel is protected whatever the method"},
      {"POST", "/api/../admin", true, "there is no path traversal shortcut"},
      {"GET", "/ADMIN", false, "the panel path is compared exactly as served"},
  };

  for (const RouteCase &c : cases) {
    bool got = adminRequiresSession(c.method, c.path);
    if (got != c.protectedRoute) {
      printf("FAIL S5 %s %s: got %s want %s (%s)\n", c.method, c.path,
             got ? "protected" : "public", c.protectedRoute ? "protected" : "public",
             c.why);
      g_failures++;
    }
    g_checks++;
  }
  check("S5 route table evaluated", true);

  check("S9 a null path fails closed", adminRequiresSession("GET", nullptr));
  check("S9 a null method fails closed", adminRequiresSession(nullptr, "/api/status"));
  check("S9 a relative path fails closed", adminRequiresSession("GET", "admin"));
}

int main() {
  testDerivation();
  testCredentialsMatch();
  testInputRules();
  testHex();
  testSessions();
  testThrottle();
  testRoutes();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

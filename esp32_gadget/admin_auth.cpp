#include "admin_auth.h"

#include <string.h>

#include "sha256.h"

// --- hashing ----------------------------------------------------------------

void adminHashDerive(const uint8_t salt[ADMIN_SALT_LEN], const char *user,
                     const char *pass, uint8_t out[ADMIN_HASH_LEN]) {
  if (!out) return;
  memset(out, 0, ADMIN_HASH_LEN);
  if (!salt || !user || !pass) return;

  // One buffer for the whole first round: salt, then the name, then the secret.
  // Bounded copies so a caller passing a runaway string cannot walk past it.
  uint8_t seed[ADMIN_SALT_LEN + ADMIN_USER_MAX + ADMIN_PASS_MAX];
  size_t userLen = strnlen(user, ADMIN_USER_MAX);
  size_t passLen = strnlen(pass, ADMIN_PASS_MAX);
  size_t len = 0;
  memcpy(seed, salt, ADMIN_SALT_LEN);
  len += ADMIN_SALT_LEN;
  memcpy(seed + len, user, userLen);
  len += userLen;
  memcpy(seed + len, pass, passLen);
  len += passLen;

  sha256(seed, len, out);
  uint8_t previous[ADMIN_HASH_LEN];
  memcpy(previous, out, ADMIN_HASH_LEN);
  for (uint32_t round = 1; round < ADMIN_HASH_ROUNDS; round++) {
    sha256(previous, ADMIN_HASH_LEN, out);
    memcpy(previous, out, ADMIN_HASH_LEN);
  }

  // The plaintext never leaves this function; overwrite the scratch copy.
  memset(seed, 0, sizeof(seed));
  memset(previous, 0, sizeof(previous));
}

bool adminEqualConstantTime(const uint8_t *a, const uint8_t *b, size_t len) {
  if (!a || !b) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

bool adminCredentialsMatch(const char *user, const char *pass,
                           const char *storedUser,
                           const uint8_t salt[ADMIN_SALT_LEN],
                           const uint8_t storedHash[ADMIN_HASH_LEN]) {
  if (!user || !pass || !storedUser || !salt || !storedHash) return false;
  if (!user[0] || !pass[0]) return false;
  uint8_t candidate[ADMIN_HASH_LEN];
  // Always derive, even when the names differ: an early return on the username
  // would answer a wrong user faster than a wrong password.
  adminHashDerive(salt, storedUser, pass, candidate);
  bool nameOk = strcmp(user, storedUser) == 0;
  bool hashOk = adminEqualConstantTime(candidate, storedHash, ADMIN_HASH_LEN);
  memset(candidate, 0, sizeof(candidate));
  return nameOk && hashOk;
}

// --- hex ---------------------------------------------------------------------

static char hexDigit(uint8_t value) {
  return (char)(value < 10 ? '0' + value : 'a' + (value - 10));
}

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void adminHexEncode(const uint8_t *data, size_t len, char *out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!data || cap < len * 2 + 1) return;
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = hexDigit((uint8_t)(data[i] >> 4));
    out[i * 2 + 1] = hexDigit((uint8_t)(data[i] & 0x0F));
  }
  out[len * 2] = '\0';
}

bool adminHexDecode(const char *hex, uint8_t *out, size_t len) {
  if (!hex || !out) return false;
  if (strlen(hex) != len * 2) return false;
  for (size_t i = 0; i < len; i++) {
    int high = hexValue(hex[i * 2]);
    int low = hexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = (uint8_t)((high << 4) | low);
  }
  return true;
}

// --- input rules -------------------------------------------------------------

bool adminUserValid(const char *user) {
  if (!user) return false;
  size_t len = strnlen(user, ADMIN_USER_MAX + 1);
  if (len == 0 || len > ADMIN_USER_MAX) return false;
  for (size_t i = 0; i < len; i++) {
    char c = user[i];
    if (c < 0x21 || c > 0x7E) return false;  // spaces and control bytes
    if (c == ':' || c == '"' || c == '/' || c == '\\' || c == '%') return false;
  }
  return true;
}

bool adminPassValid(const char *pass) {
  if (!pass) return false;
  size_t len = strnlen(pass, ADMIN_PASS_MAX + 1);
  if (len < ADMIN_PASS_MIN || len > ADMIN_PASS_MAX) return false;
  for (size_t i = 0; i < len; i++) {
    char c = pass[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

// --- sessions ---------------------------------------------------------------

void adminSessionReset(AdminSessionTable *table) {
  if (!table) return;
  memset(table, 0, sizeof(*table));
}

static bool sessionLive(const AdminSession *slot, uint32_t nowMs) {
  return slot->used &&
         (uint32_t)(nowMs - slot->openedMs) < ADMIN_SESSION_TTL_MS;
}

int adminSessionLive(const AdminSessionTable *table, uint32_t nowMs) {
  if (!table) return 0;
  int live = 0;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    if (sessionLive(&table->slots[i], nowMs)) live++;
  }
  return live;
}

void adminSessionOpen(AdminSessionTable *table, uint32_t nowMs,
                      const uint8_t tokenBytes[ADMIN_HASH_LEN]) {
  if (!table || !tokenBytes) return;
  AdminSession *target = nullptr;
  // An expired slot is free; otherwise the oldest one goes. Tracking the oldest
  // by elapsed time rather than by timestamp keeps it correct across the
  // millis() wraparound at 49 days.
  uint32_t oldestElapsed = 0;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    AdminSession *slot = &table->slots[i];
    if (!sessionLive(slot, nowMs)) {
      target = slot;
      break;
    }
    uint32_t elapsed = (uint32_t)(nowMs - slot->openedMs);
    if (!target || elapsed > oldestElapsed) {
      target = slot;
      oldestElapsed = elapsed;
    }
  }
  target->used = true;
  target->openedMs = nowMs;
  adminHexEncode(tokenBytes, ADMIN_HASH_LEN, target->token, ADMIN_TOKEN_CAP);
}

bool adminSessionValid(const AdminSessionTable *table, const char *token,
                       uint32_t nowMs) {
  if (!table || !token) return false;
  size_t len = strnlen(token, ADMIN_TOKEN_CAP + 1);
  // A token of the wrong length cannot match, and refusing it before the byte
  // loop keeps a short or unterminated string out of the comparison.
  if (len != ADMIN_TOKEN_HEX) return false;
  bool found = false;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    const AdminSession *slot = &table->slots[i];
    if (!sessionLive(slot, nowMs)) continue;
    if (adminEqualConstantTime((const uint8_t *)slot->token,
                               (const uint8_t *)token, ADMIN_TOKEN_HEX)) {
      found = true;
    }
  }
  return found;
}

void adminSessionClose(AdminSessionTable *table, const char *token) {
  if (!table || !token) return;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    AdminSession *slot = &table->slots[i];
    if (!slot->used) continue;
    if (adminEqualConstantTime((const uint8_t *)slot->token,
                               (const uint8_t *)token, ADMIN_TOKEN_HEX)) {
      memset(slot, 0, sizeof(*slot));
    }
  }
}

void adminSessionCloseOthers(AdminSessionTable *table, const char *keepToken) {
  if (!table) return;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    AdminSession *slot = &table->slots[i];
    if (!slot->used) continue;
    bool keep = keepToken && adminEqualConstantTime((const uint8_t *)slot->token,
                                                    (const uint8_t *)keepToken,
                                                    ADMIN_TOKEN_HEX);
    if (!keep) memset(slot, 0, sizeof(*slot));
  }
}

// --- login throttle ---------------------------------------------------------

void adminThrottleReset(AdminThrottle *throttle) {
  if (!throttle) return;
  throttle->fails = 0;
  throttle->locked = false;
  throttle->lockedUntilMs = 0;
}

bool adminThrottleBlocked(const AdminThrottle *throttle, uint32_t nowMs) {
  if (!throttle || !throttle->locked) return false;
  // Signed difference, so the comparison survives the millis() wraparound.
  return (int32_t)(throttle->lockedUntilMs - nowMs) > 0;
}

void adminThrottleFail(AdminThrottle *throttle, uint32_t nowMs) {
  if (!throttle) return;
  if (throttle->locked && !adminThrottleBlocked(throttle, nowMs)) {
    throttle->locked = false;
    throttle->fails = 0;
  }
  if (throttle->locked) return;  // still serving the same lock
  if (throttle->fails < 255) throttle->fails++;
  if (throttle->fails >= ADMIN_MAX_FAILS) {
    throttle->locked = true;
    throttle->lockedUntilMs = nowMs + ADMIN_LOCK_MS;
    throttle->fails = 0;
  }
}

// --- route protection -------------------------------------------------------

bool adminIsLoginPath(const char *method, const char *path) {
  if (!method || !path) return false;
  if (strcmp(method, "POST") != 0) return false;
  return !strcmp(path, "/api/session") || !strcmp(path, "/api/session/logout");
}

bool adminIsPublicWrite(const char *method, const char *path) {
  if (!method || !path) return false;
  // Exact match only. A trailing slash, a different verb or any other write is
  // not one of these and stays protected.
  if (strcmp(method, "POST") != 0) return false;
  // What the glass shows, and the face the web game makes it react with: both
  // are cosmetic, reversible and carry no credentials.
  if (!strcmp(path, "/api/focus")) return true;
  return !strcmp(path, "/api/screen") || !strcmp(path, "/api/face");
}

bool adminRequiresSession(const char *method, const char *path) {
  // Fail closed: a request the transport could not describe is never public.
  if (!method || !path) return true;
  if (path[0] != '/') return true;
  if (adminIsLoginPath(method, path)) return false;
  if (adminIsPublicWrite(method, path)) return false;

  if (!strcmp(path, "/admin") || !strcmp(path, "/admin/") ||
      !strcmp(path, "/admin.html") || !strncmp(path, "/admin/", 7)) {
    return true;
  }

  bool isApi = strncmp(path, "/api/", 5) == 0;
  // El calendario es privado incluso para leer: los títulos de los eventos y qué
  // credenciales hay guardadas no son para cualquiera que se una al AP del
  // equipo. Las demás lecturas siguen públicas porque alimentan la cara y el
  // selector de pantalla.
  if (isApi && strncmp(path, "/api/calendar", 13) == 0) return true;
  if (isApi && strcmp(method, "GET") != 0) {
    // Every other write verb under /api, including the ones no route knows:
    // answering a 401 for an unknown path costs nothing and an unauthenticated
    // write can only reach the one endpoint named above.
    return true;
  }
  return false;
}

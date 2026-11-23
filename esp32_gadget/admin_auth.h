// Web credentials, sessions and route protection, with no Arduino includes.
//
// The firmware and `tools/admin_auth_test.cpp` compile this same file, so the
// rules that decide "does this request need a session" and "does this password
// match" are pinned on the host instead of only on the board. Everything that
// needs hardware (the random token bytes, NVS, the clock) enters through
// arguments or comes back as a plain value.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Credentials the panel can hold. The password floor is the same eight
// characters WPA2 demands, for the same reason: shorter secrets on a device that
// broadcasts its own access point are a formality.
#define ADMIN_USER_MAX 32
#define ADMIN_PASS_MIN 8
#define ADMIN_PASS_MAX 64
#define ADMIN_SALT_LEN 16
#define ADMIN_HASH_LEN 32

// Iterated so a keyboard-frequency guess costs more than one hash. 2048 rounds
// of the same SHA-256 the Spotify code already carries keeps a login under a
// few tens of milliseconds on a 240 MHz S3 while staying cheap to implement
// twice, once in C++ and once in the build step that seeds `.env`.
#define ADMIN_HASH_ROUNDS 2048

// A session is a random 32-byte token in a cookie. Twelve hours is a whole
// evening of tinkering without asking for the password again. Desde esta
// feature la sesión se guarda en NVS (`admin_session_store.*`) para que sobreviva
// un reboot: es la decisión del dueño de no volver a tipear la clave cada vez que
// el equipo se reinicia. El token sigue siendo aleatorio e independiente de la
// contraseña, y se puede revocar con logout, cambiando la clave o con el reset de
// fábrica.
#define ADMIN_SESSION_MAX 4
#define ADMIN_TOKEN_HEX 64
#define ADMIN_TOKEN_CAP (ADMIN_TOKEN_HEX + 1)
#define ADMIN_SESSION_TTL_MS (12UL * 60UL * 60UL * 1000UL)
#define ADMIN_SESSION_TTL_S (12L * 60L * 60L)
// Un epoch menor que esto es el reloj sin sincronizar (el RTC arranca en 1970).
// Una sesión guardada no se puede evaluar antes de NTP, así que se rechaza.
#define ADMIN_CLOCK_MIN_EPOCH 1600000000LL

// Failed logins are counted device-wide, because behind the access point there
// is no per-client identity worth trusting. Five tries, then half a minute.
#define ADMIN_MAX_FAILS 5
#define ADMIN_LOCK_MS (30UL * 1000UL)

// --- hashing ----------------------------------------------------------------

// salt || user || pass hashed, then re-hashed ADMIN_HASH_ROUNDS-1 times.
// Including the username makes the stored digest useless for a rename: the same
// password under a new name produces a different digest.
void adminHashDerive(const uint8_t salt[ADMIN_SALT_LEN], const char *user,
                     const char *pass, uint8_t out[ADMIN_HASH_LEN]);

// True when `user` matches and `pass` derives to `storedHash`. Derives first and
// compares the digests without an early exit, so a wrong password costs the same
// as a right one and neither the stored hash nor its prefix leaks by timing.
bool adminCredentialsMatch(const char *user, const char *pass,
                           const char *storedUser,
                           const uint8_t salt[ADMIN_SALT_LEN],
                           const uint8_t storedHash[ADMIN_HASH_LEN]);

// Length-equal comparison with no early return.
bool adminEqualConstantTime(const uint8_t *a, const uint8_t *b, size_t len);

// --- hex ---------------------------------------------------------------------

// Lowercase hex plus its terminator. Writes "" when `cap` cannot hold it.
void adminHexEncode(const uint8_t *data, size_t len, char *out, size_t cap);

// Reads exactly `len` bytes. False on wrong length, non-hex input or a null
// argument, which is what keeps a candidate token from being half-accepted.
bool adminHexDecode(const char *hex, uint8_t *out, size_t len);

// --- input rules -------------------------------------------------------------

// Visible ASCII, no spaces and no separator characters the panel or the cookie
// header would have to escape.
bool adminUserValid(const char *user);

// A new password: ADMIN_PASS_MIN..ADMIN_PASS_MAX characters, printable ASCII.
// The seeded default may be shorter; this only guards what the owner sets.
bool adminPassValid(const char *pass);

// --- sessions ---------------------------------------------------------------

struct AdminSession {
  bool used;
  char token[ADMIN_TOKEN_CAP];
  // La sesión tiene dos relojes a propósito. `openedAtEpoch` es la hora
  // absoluta: es lo único que sobrevive un reboot, y sólo se puede evaluar con
  // NTP en hora. `openedMs` es el arranque actual, para que el panel funcione
  // en un equipo sin internet (donde no hay NTP) — esa sesión no se guarda.
  uint32_t openedMs;
  int64_t openedAtEpoch;
};

struct AdminSessionTable {
  AdminSession slots[ADMIN_SESSION_MAX];
};

void adminSessionReset(AdminSessionTable *table);

// Opens a session for `tokenBytes` (ADMIN_HASH_LEN random bytes, from
// esp_random in the firmware). A full table drops the oldest session, so a third
// device never locks the owner out of the panel they are holding.
void adminSessionOpen(AdminSessionTable *table, uint32_t nowMs,
                      int64_t nowEpoch,
                      const uint8_t tokenBytes[ADMIN_HASH_LEN]);

// True only for an unexpired token that matches a slot exactly. `nowEpoch` is
// `time(nullptr)`; un synced clock (or 0) means a persisted session cannot be
// proven alive and is refused until NTP answers.
bool adminSessionValid(const AdminSessionTable *table, const char *token,
                       uint32_t nowMs, int64_t nowEpoch);

void adminSessionClose(AdminSessionTable *table, const char *token);

// Drops every session except `keepToken`, so changing the password logs out
// every other device without kicking the browser that made the change.
void adminSessionCloseOthers(AdminSessionTable *table, const char *keepToken);

// Sessions that are still within their lifetime.
int adminSessionLive(const AdminSessionTable *table, uint32_t nowMs,
                     int64_t nowEpoch);

// --- login throttle ---------------------------------------------------------

struct AdminThrottle {
  uint8_t fails;
  bool locked;
  uint32_t lockedUntilMs;
};

void adminThrottleReset(AdminThrottle *throttle);

bool adminThrottleBlocked(const AdminThrottle *throttle, uint32_t nowMs);

// Counts one failed attempt and arms the lock on the fifth. The counter restarts
// after a lock, so the next round gets a full five tries instead of locking
// again on the first typo.
void adminThrottleFail(AdminThrottle *throttle, uint32_t nowMs);

// --- route protection -------------------------------------------------------

// True when the HTTP transport must see a valid session before handling this
// request:
//   * every page under /admin, the configuration area;
//   * every /api request that is not a read, because those are the ones that
//     change WiFi credentials, reset the device or wipe its storage.
// Reads stay public so the screen picker and the games planned for `/` can read
// device state with no login. The access point password they can already see is
// printed on the device screen by design; the station password and the tokens
// are not part of any public read.
bool adminRequiresSession(const char *method, const char *path);

// Public endpoints that change the session itself. They are the two exceptions
// the rule above allows, named once so the transport and the test agree.
bool adminIsLoginPath(const char *method, const char *path);

// The two public writes: `POST /api/screen`, which only changes what the panel
// shows, and `POST /api/face`, which lets the web game move the face. Both exist
// because the picker and the games live outside the login, both are cosmetic and
// reversible, and both are matched exactly so a trailing slash or any other
// write stays protected.
bool adminIsPublicWrite(const char *method, const char *path);

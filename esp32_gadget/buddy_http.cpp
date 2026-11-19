#include "buddy_http.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "buddy_link.h"

// A poll now crosses the internet, so the budget is the tunnel's, not the LAN's.
// Keep-alive keeps the healthy case at one round trip; this number only bounds
// the afternoon the tunnel is down.
#define BUDDY_HTTP_TIMEOUT_MS 5000
#define BUDDY_TLS_HANDSHAKE_S 8
// Long enough for one poll to finish before an ask gives up on the lock: both
// come from different tasks and the clients below are shared.
#define BUDDY_TRANSPORT_LOCK_MS 12000

// Same acknowledgement the panel sends; a stable agent string is easier to allow
// in a Cloudflare rule than the client library's default.
#define BUDDY_HTTP_USER_AGENT "ESP32-Gadget/1.0 (Sofia bridge)"

// One client per transport, both reused. Without the reuse the 1 s poll would pay
// a TLS handshake every second: ~40 KB of heap and ~1.5 s per attempt. The
// secure client is only touched when the stored URL is https.
static WiFiClient s_plain;
static WiFiClientSecure s_tls;
static bool s_tlsReady = false;
// The long-poll gets its own socket on purpose. `GET /events?wait=25` holds the
// read for up to 25 s, and sharing one client would make the panel's `ask` wait
// behind it — a button that does nothing for half a minute. Two sockets cost one
// extra TLS session when the bridge is https, and buy a face that answers while
// the event stream is parked.
static WiFiClient s_eventsPlain;
static WiFiClientSecure s_eventsTls;
static bool s_eventsTlsReady = false;
// The poller task and the panel's ask run in different tasks and share those
// clients. Without this lock a POST could interleave with a GET on the same
// socket and corrupt the session, which would look like a flaky bridge.
static SemaphoreHandle_t s_lock = nullptr;
static SemaphoreHandle_t s_eventsLock = nullptr;

static bool lockTransport() {
  if (!s_lock) return false;
  return xSemaphoreTake(s_lock, pdMS_TO_TICKS(BUDDY_TRANSPORT_LOCK_MS)) == pdTRUE;
}

static void unlockTransport() {
  if (s_lock) xSemaphoreGive(s_lock);
}

void buddyHttpBegin() {
  // Created once at boot: both transports come from different tasks, and a lazy
  // creation could race the poller against the first ask from the panel.
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  if (!s_eventsLock) s_eventsLock = xSemaphoreCreateMutex();
}

static void ensureTls() {
  if (s_tlsReady) return;
  s_tls.useBuiltinCACertBundle();
  s_tls.setHandshakeTimeout(BUDDY_TLS_HANDSHAKE_S);
  s_tlsReady = true;
}

static void ensureEventsTls() {
  if (s_eventsTlsReady) return;
  s_eventsTls.useBuiltinCACertBundle();
  s_eventsTls.setHandshakeTimeout(BUDDY_TLS_HANDSHAKE_S);
  s_eventsTlsReady = true;
}

static void reset(BuddyHttpResult *out) {
  out->status = 0;
  out->transportError = 0;
  out->tlsFailed = false;
  out->redirected = false;
  out->detail[0] = '\0';
  out->body = "";
}

// Opens the request and applies the headers both endpoints share. Returns false
// when the URL could not be built, which is the only failure that has no
// transport error code.
static bool beginRequest(HTTPClient &http, const BuddyUrl &url, const char *path,
                         const char *token, uint32_t readTimeoutMs = 0,
                         bool events = false) {
  char endpoint[BUDDY_URL_MAX + 24];
  if (!buddyUrlJoin(url, path, endpoint, sizeof(endpoint))) return false;

  http.setReuse(true);
  if (readTimeoutMs > 0) {
    // The long-poll is the only request allowed to wait longer than the tunnel
    // budget: the bridge holds the answer on purpose. Connect stays short, so a
    // dead host is still detected in five seconds.
    http.setTimeout((uint16_t)readTimeoutMs);
  } else {
    http.setTimeout(BUDDY_HTTP_TIMEOUT_MS);
  }
  http.setConnectTimeout(BUDDY_HTTP_TIMEOUT_MS);
  http.setUserAgent(BUDDY_HTTP_USER_AGENT);

  bool begun;
  if (url.tls) {
    if (events) {
      ensureEventsTls();
      begun = http.begin(s_eventsTls, endpoint);
    } else {
      ensureTls();
      begun = http.begin(s_tls, endpoint);
    }
  } else {
    begun = http.begin(events ? s_eventsPlain : s_plain, endpoint);
  }
  if (!begun) return false;

  http.addHeader("X-Buddy-Token", token);
  http.addHeader("Accept", "application/json");
  return true;
}

// A DNS failure never reaches the TLS client's error slot: NetworkClientSecure
// returns from connect() before assigning `last_error`, which keeps whatever the
// previous attempt left there, and stop() preserves it. Resolving first means the
// reason is only ever read after an attempt that could actually have failed, so a
// stale X509 code can no longer be blamed on a host that does not even resolve.
// The lookup is the same one the client would do, so lwIP's cache serves it.
static bool hostReachable(const BuddyUrl &url, BuddyHttpResult *out) {
  if (!url.tls) return true;  // nothing downstream reads a TLS code over http
  IPAddress literal;
  if (literal.fromString(url.host)) return true;
  IPAddress resolved;
  if (Network.hostByName(url.host, resolved)) return true;
  out->transportError = -1;
  snprintf(out->detail, sizeof(out->detail), "dns");
  return false;
}

// Turns the code HTTPClient returned into facts the classifier can use. Any
// negative code is a transport failure, and on TLS that usually means the
// handshake was rejected — mbedtls keeps the reason, so it travels to the panel
// instead of being swallowed.
static void absorb(HTTPClient &http, int code, bool tls, BuddyHttpResult *out,
                   WiFiClientSecure *tlsClient = nullptr) {
  if (code <= 0) {
    out->transportError = code;
    if (tls) {
      WiFiClientSecure *client = tlsClient ? tlsClient : &s_tls;
      char detail[sizeof(out->detail)];
      int err = client->lastError(detail, sizeof(detail));
      // The raw reason always travels; only the X509 range is a rejected
      // certificate. A connect that never got to a handshake reports -1, and that
      // is "nobody answered", not a certificate problem.
      if (err < 0) {
        snprintf(out->detail, sizeof(out->detail), "%s", detail);
        out->tlsFailed = buddyTlsErrorIsCertFailure(err);
      }
    }
    return;
  }
  out->status = code;
  out->redirected = code >= 300 && code < 400;
}

void buddyHttpGetState(const BuddyUrl &url, const char *token, BuddyHttpResult *out) {
  if (!out) return;
  reset(out);
  if (!lockTransport()) return;

  HTTPClient http;
  if (hostReachable(url, out) && beginRequest(http, url, "/state", token)) {
    int code = http.GET();
    absorb(http, code, url.tls, out);
    // Only a 200 carries a state body. Reading a body off an error or a
    // keep-alive response without a length would sit on the timeout.
    if (code == 200) out->body = http.getString();
  }
  http.end();
  unlockTransport();
}

// GET <base>/events?since=<seq>&wait=<sec>. The bridge holds the request for up
// to `waitSec` and answers `[]` when nothing happened, which is a timeout and
// not a failure. The read budget is `waitSec` plus a margin: a client that gives
// up before the bridge answers turns every long-poll into a retry loop.
void buddyHttpGetEvents(const BuddyUrl &url, const char *token, int since,
                        int waitSec, BuddyHttpResult *out) {
  if (!out) return;
  reset(out);
  if (waitSec < 1) waitSec = 1;
  if (waitSec > BUDDY_EVENTS_MAX_WAIT_SEC) waitSec = BUDDY_EVENTS_MAX_WAIT_SEC;
  if (since < 0) since = 0;
  char path[48];
  snprintf(path, sizeof(path), "/events?since=%d&wait=%d", since, waitSec);
  uint32_t readMs = (uint32_t)(waitSec + BUDDY_EVENTS_MARGIN_SEC) * 1000u;

  if (!s_eventsLock) return;
  if (xSemaphoreTake(s_eventsLock, pdMS_TO_TICKS(BUDDY_TRANSPORT_LOCK_MS)) !=
      pdTRUE) {
    return;
  }

  HTTPClient http;
  if (hostReachable(url, out) &&
      beginRequest(http, url, path, token, readMs, true)) {
    int code = http.GET();
    absorb(http, code, url.tls, out, &s_eventsTls);
    if (code == 200) out->body = http.getString();
  }
  http.end();
  xSemaphoreGive(s_eventsLock);
}

void buddyHttpAsk(const BuddyUrl &url, const char *token, const String &json,
                  BuddyHttpResult *out) {
  if (!out) return;
  reset(out);
  if (!lockTransport()) return;

  HTTPClient http;
  if (hostReachable(url, out) && beginRequest(http, url, "/buddy/ask", token)) {
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)json.c_str(), json.length());
    absorb(http, code, url.tls, out);
  }
  http.end();
  unlockTransport();
}

// El mismo socket y el mismo lock que el poll: es un pedido de administración,
// no un stream, así que esperar el turno del poller es lo correcto. El cuerpo se
// lee en cualquier 2xx porque el bridge contesta 200 al estado y 202 al ask.
void buddyHttpCall(const BuddyUrl &url, const char *path, const char *verb,
                   const char *token, const String &json, BuddyHttpResult *out) {
  if (!out) return;
  reset(out);
  if (!path || !*path) return;
  const bool post = verb && !strcmp(verb, "POST");
  if (!lockTransport()) return;

  HTTPClient http;
  if (hostReachable(url, out) && beginRequest(http, url, path, token)) {
    int code;
    if (post) {
      http.addHeader("Content-Type", "application/json");
      code = http.POST((uint8_t *)json.c_str(), json.length());
    } else {
      code = http.GET();
    }
    absorb(http, code, url.tls, out);
    if (code >= 200 && code < 300) out->body = http.getString();
  }
  http.end();
  unlockTransport();
}

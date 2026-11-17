#include "buddy_http.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

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
// The poller task and the panel's ask run in different tasks and share those
// clients. Without this lock a POST could interleave with a GET on the same
// socket and corrupt the session, which would look like a flaky bridge.
static SemaphoreHandle_t s_lock = nullptr;

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
}

static void ensureTls() {
  if (s_tlsReady) return;
  s_tls.useBuiltinCACertBundle();
  s_tls.setHandshakeTimeout(BUDDY_TLS_HANDSHAKE_S);
  s_tlsReady = true;
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
                         const char *token) {
  char endpoint[BUDDY_URL_MAX + 24];
  if (!buddyUrlJoin(url, path, endpoint, sizeof(endpoint))) return false;

  http.setReuse(true);
  http.setTimeout(BUDDY_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(BUDDY_HTTP_TIMEOUT_MS);
  http.setUserAgent(BUDDY_HTTP_USER_AGENT);

  bool begun;
  if (url.tls) {
    ensureTls();
    begun = http.begin(s_tls, endpoint);
  } else {
    begun = http.begin(s_plain, endpoint);
  }
  if (!begun) return false;

  http.addHeader("X-Buddy-Token", token);
  http.addHeader("Accept", "application/json");
  return true;
}

// Turns the code HTTPClient returned into facts the classifier can use. Any
// negative code is a transport failure, and on TLS that usually means the
// handshake was rejected — mbedtls keeps the reason, so it travels to the panel
// instead of being swallowed.
static void absorb(HTTPClient &http, int code, bool tls, BuddyHttpResult *out) {
  if (code <= 0) {
    out->transportError = code;
    if (tls) {
      char detail[sizeof(out->detail)];
      int err = s_tls.lastError(detail, sizeof(detail));
      // mbedtls reports failures as negative codes and leaves a non-negative
      // value behind after a successful handshake, so a plain "nonzero" test
      // would blame the certificate for a host that simply did not answer.
      if (err < 0) {
        out->tlsFailed = true;
        snprintf(out->detail, sizeof(out->detail), "%s", detail);
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
  if (beginRequest(http, url, "/state", token)) {
    int code = http.GET();
    absorb(http, code, url.tls, out);
    // Only a 200 carries a state body. Reading a body off an error or a
    // keep-alive response without a length would sit on the timeout.
    if (code == 200) out->body = http.getString();
  }
  http.end();
  unlockTransport();
}

void buddyHttpAsk(const BuddyUrl &url, const char *token, const String &json,
                  BuddyHttpResult *out) {
  if (!out) return;
  reset(out);
  if (!lockTransport()) return;

  HTTPClient http;
  if (beginRequest(http, url, "/buddy/ask", token)) {
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)json.c_str(), json.length());
    absorb(http, code, url.tls, out);
  }
  http.end();
  unlockTransport();
}

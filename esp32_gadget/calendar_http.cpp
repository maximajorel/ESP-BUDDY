#include "calendar_http.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

// One client for the calendar's three requests: token, calendar list, events.
// HTTPClient's reuse keeps the TLS session alive between them, which is the
// difference between one handshake and three per refresh. It is deliberately
// not shared with the bridge's or Spotify's client: a TLS client torn down by
// another task while this one uses it already cost this project a heap crash.
WiFiClientSecure s_tls;
bool s_tlsReady = false;
SemaphoreHandle_t s_lock = nullptr;

constexpr int kTimeoutMs = 8000;

void ensureTls() {
  if (s_tlsReady) return;
  s_tls.useBuiltinCACertBundle();
  s_tls.setHandshakeTimeout(8);
  s_tlsReady = true;
}

// Keeps both entry points serialized: the web task starts a flow while the
// calendar task refreshes a token, and one WiFiClientSecure cannot serve two
// requests at once.
class Guard {
 public:
  Guard() {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
  }
  ~Guard() {
    if (s_lock) xSemaphoreGive(s_lock);
  }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;
};

void describeFailure(HTTPClient &http, int code, CalendarHttpResult *out) {
  out->status = 0;
  out->transportError = code;
  // A failed connect with a nonzero mbedtls error is a certificate/protocol
  // rejection, which the panel reports differently from "no internet".
  char tlsDetail[96];
  out->tlsFailed = (code == HTTPC_ERROR_CONNECTION_REFUSED) &&
                   s_tls.lastError(tlsDetail, sizeof(tlsDetail)) != 0;
  out->body = HTTPClient::errorToString(code);
}

// Reads the body, refusing anything larger than the caller budget. Google's
// answers here are a few kilobytes; a captive portal can answer with megabytes
// and there is no reason to hold that in RAM.
bool readBody(HTTPClient &http, int status, size_t maxBody,
              CalendarHttpResult *out) {
  out->status = status;
  const int declared = http.getSize();
  if (declared > 0 && (size_t)declared > maxBody) {
    out->transportError = CALENDAR_HTTP_BODY_TOO_LARGE;
    return true;
  }
  String body = http.getString();
  if (body.length() > maxBody) {
    out->transportError = CALENDAR_HTTP_BODY_TOO_LARGE;
    return true;
  }
  out->body = body;
  return true;
}

// Diagnóstico en el puerto serie: el primer intercambio HTTPS después de
// arrancar falla de vez en cuando y el panel sólo muestra "no pude pedir los
// eventos". Esta línea dice cuál de las tres peticiones fue, con qué código y
// con qué error de transporte, y sólo se imprime cuando algo salió mal.
void noteFailure(const char *method, const char *host, const char *path,
                 const CalendarHttpResult &res) {
  if (res.status > 0 && res.transportError == 0) return;
  Serial.printf("[calendar] %s %s%s: status=%d transport=%d tls=%d bytes=%u\n",
                method, host, path, res.status, res.transportError,
                (int)res.tlsFailed, (unsigned)res.body.length());
}

bool prepare(const char *host, const char *path, HTTPClient &http,
             String *urlOut) {
  ensureTls();
  *urlOut = String("https://") + host + path;
  http.setReuse(true);
  http.setTimeout(kTimeoutMs);
  http.setConnectTimeout(kTimeoutMs);
  return http.begin(s_tls, *urlOut);
}

}  // namespace

void calendarHttpBegin() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  ensureTls();
}

bool calendarHttpGet(const char *host, const char *path, const char *bearer,
                     size_t maxBody, CalendarHttpResult *out) {
  if (!out) return false;
  out->status = 0;
  out->transportError = 0;
  out->tlsFailed = false;
  out->body = "";
  Guard guard;

  HTTPClient http;
  String url;
  if (!prepare(host, path, http, &url)) {
    out->transportError = -1;
    out->body = "no se pudo iniciar";
    return false;
  }
  if (bearer && *bearer) {
    http.addHeader("Authorization", String("Bearer ") + bearer);
  }
  const int code = http.GET();
  if (code <= 0) {
    describeFailure(http, code, out);
    http.end();
    noteFailure("GET", host, path, *out);
    return true;
  }
  readBody(http, code, maxBody, out);
  http.end();
  noteFailure("GET", host, path, *out);
  return true;
}

bool calendarHttpPostForm(const char *host, const char *path, const String &form,
                          size_t maxBody, CalendarHttpResult *out) {
  if (!out) return false;
  out->status = 0;
  out->transportError = 0;
  out->tlsFailed = false;
  out->body = "";
  Guard guard;

  HTTPClient http;
  String url;
  if (!prepare(host, path, http, &url)) {
    out->transportError = -1;
    out->body = "no se pudo iniciar";
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const int code = http.POST((uint8_t *)form.c_str(), form.length());
  if (code <= 0) {
    describeFailure(http, code, out);
    http.end();
    noteFailure("POST", host, path, *out);
    return true;
  }
  readBody(http, code, maxBody, out);
  http.end();
  noteFailure("POST", host, path, *out);
  return true;
}

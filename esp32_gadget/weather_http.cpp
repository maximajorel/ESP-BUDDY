#include "weather_http.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

// One client for the weather service and the IP lookup. They never run at the
// same time (both are serialized behind this mutex), and HTTPClient's reuse keeps
// the TLS session alive between refreshes.
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

}  // namespace

void weatherHttpBegin() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  ensureTls();
}

bool weatherHttpGet(const char *host, const char *path, size_t maxBody,
                    WeatherHttpResult *out) {
  if (!host || !path || !out) return false;
  out->status = 0;
  out->transportError = 0;
  out->tlsFailed = false;
  out->body = "";
  Guard guard;

  ensureTls();
  HTTPClient http;
  const String url = String("https://") + host + path;
  http.setReuse(true);
  http.setTimeout(kTimeoutMs);
  http.setConnectTimeout(kTimeoutMs);
  if (!http.begin(s_tls, url)) {
    out->transportError = -1;
    out->body = "no se pudo iniciar";
    return false;
  }

  const int code = http.GET();
  if (code <= 0) {
    out->status = 0;
    out->transportError = code;
    char tlsDetail[96];
    out->tlsFailed = (code == HTTPC_ERROR_CONNECTION_REFUSED) &&
                     s_tls.lastError(tlsDetail, sizeof(tlsDetail)) != 0;
    out->body = HTTPClient::errorToString(code);
    http.end();
    return true;
  }

  out->status = code;
  const int declared = http.getSize();
  if (declared > 0 && (size_t)declared > maxBody) {
    out->transportError = WEATHER_HTTP_BODY_TOO_LARGE;
    http.end();
    return true;
  }
  String body = http.getString();
  if (body.length() > maxBody) {
    out->transportError = WEATHER_HTTP_BODY_TOO_LARGE;
  } else {
    out->body = body;
  }
  http.end();
  return true;
}

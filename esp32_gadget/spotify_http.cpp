#include "spotify_http.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// One client for the whole module. HTTPClient's reuse keeps the TLS session
// alive between polls, which is what makes an 8 s cadence affordable.
static WiFiClientSecure s_tls;
static bool s_tlsReady = false;

static void ensureTls() {
  if (s_tlsReady) return;
  s_tls.useBuiltinCACertBundle();
  s_tls.setHandshakeTimeout(8);
  s_tlsReady = true;
}

static void describeFailure(HTTPClient &http, int code, SpotifyHttpResult *out) {
  out->status = 0;
  out->transportError = code;
  if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
    out->body = "conexion rechazada";
  } else if (code == HTTPC_ERROR_READ_TIMEOUT) {
    out->body = "sin respuesta";
  } else if (code == HTTPC_ERROR_NOT_CONNECTED) {
    out->body = "sin internet";
  } else {
    out->body = HTTPClient::errorToString(code);
  }
}

void spotifyHttpGet(const char *host, const char *path, const char *bearer,
                    size_t maxBody, SpotifyHttpResult *out) {
  if (!out) return;
  out->status = 0;
  out->transportError = 0;
  out->retryAfterSeconds = 0;
  out->body = "";
  ensureTls();

  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(8000);
  http.setConnectTimeout(8000);
  String url = String("https://") + host + path;
  if (!http.begin(s_tls, url)) {
    out->body = "no se pudo iniciar";
    return;
  }
  if (bearer && *bearer) {
    http.addHeader("Authorization", String("Bearer ") + bearer);
  }
  int code = http.GET();
  if (code <= 0) {
    describeFailure(http, code, out);
    http.end();
    return;
  }
  out->status = code;
  if (code == 429) {
    String retry = http.header("Retry-After");
    if (retry.length()) out->retryAfterSeconds = retry.toInt();
  }
  // 204 has no body and getString() would sit on the timeout waiting for one.
  if (code != 204) {
    if (maxBody > 0) {
      out->body = http.getString();
      if (out->body.length() > maxBody) out->body.remove(maxBody);
    }
  }
  http.end();
}

void spotifyHttpPostForm(const char *host, const char *path, const String &form,
                         SpotifyHttpResult *out) {
  if (!out) return;
  out->status = 0;
  out->transportError = 0;
  out->retryAfterSeconds = 0;
  out->body = "";
  ensureTls();

  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(8000);
  http.setConnectTimeout(8000);
  String url = String("https://") + host + path;
  if (!http.begin(s_tls, url)) {
    out->body = "no se pudo iniciar";
    return;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST((uint8_t *)form.c_str(), form.length());
  if (code <= 0) {
    describeFailure(http, code, out);
    http.end();
    return;
  }
  out->status = code;
  if (code == 429) {
    String retry = http.header("Retry-After");
    if (retry.length()) out->retryAfterSeconds = retry.toInt();
  }
  out->body = http.getString();
  http.end();
}

bool spotifyHttpGetBytes(const char *url, uint8_t *out, size_t maxBytes,
                         size_t *outLen, int *statusOut) {
  if (outLen) *outLen = 0;
  if (statusOut) *statusOut = 0;
  if (!url || !*url || !out || maxBytes == 0) return false;
  ensureTls();

  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(8000);
  http.setConnectTimeout(8000);
  if (!http.begin(s_tls, url)) return false;
  int code = http.GET();
  if (statusOut) *statusOut = code;
  if (code != 200) {
    http.end();
    return false;
  }
  int declared = http.getSize();
  if (declared > 0 && (size_t)declared > maxBytes) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t got = 0;
  uint32_t deadline = millis() + 8000;
  while (got < maxBytes && (millis() < deadline)) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(out + got, min((size_t)avail, maxBytes - got));
      if (n <= 0) break;
      got += n;
      deadline = millis() + 8000;
      continue;
    }
    if (!http.connected()) break;
    delay(1);
  }
  http.end();
  if (outLen) *outLen = got;
  return got > 0;
}

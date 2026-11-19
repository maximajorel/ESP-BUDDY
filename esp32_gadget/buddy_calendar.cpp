#include "buddy_calendar.h"

#include <ArduinoJson.h>

#include "buddy_http.h"
#include "buddy_link.h"
#include "buddy_url.h"
#include "net.h"
#include "settings.h"

namespace {

// Una respuesta del bridge son dos o trescientos bytes; el documento vive en el
// stack de la tarea del servidor web y se va al terminar el pedido.
constexpr size_t kDocBytes = 1536;

// El mismo orden que el poll de la cara: primero se mira qué falta del lado del
// dueño (URL, token, reloj) y sólo después qué contestó la red, porque un error
// de certificado por reloj no sincronizado se leería como "el bridge está roto".
const char *linkSentence(BuddyLink link) {
  switch (link) {
    case BUDDY_LINK_UNCONFIGURED: return "Falta la URL o el token del bridge";
    case BUDDY_LINK_BAD_URL: return "La URL del bridge no es valida";
    case BUDDY_LINK_NO_TIME: return "El reloj todavia no se sincronizo";
    case BUDDY_LINK_TLS: return "Rechazaron el certificado del bridge";
    case BUDDY_LINK_BLOCKED: return "Cloudflare bloqueo el pedido";
    case BUDDY_LINK_UNAUTHORIZED: return "El token del bridge no es valido";
    case BUDDY_LINK_OFFLINE: return "No pude conectar al bridge";
    case BUDDY_LINK_HTTP: break;
    case BUDDY_LINK_OK: break;
  }
  return "El bridge respondio con un error";
}

// Deja lista la URL y el token de lectura, o explica qué falta. `ask` elige el
// token de administración en lugar del del buddy.
bool loadBridge(BuddyUrl *url, char *token, size_t cap, bool ask, char *err,
                size_t errCap) {
  char urlText[SETTINGS_BUDDY_URL_MAX + 1];
  settingsGetBuddyUrl(urlText, sizeof(urlText));
  if (ask) {
    settingsGetBuddyAskToken(token, cap);
  } else {
    settingsGetBuddyToken(token, cap);
  }
  if (!urlText[0]) {
    if (err && errCap) snprintf(err, errCap, "Falta la URL del bridge");
    return false;
  }
  if (buddyUrlParse(urlText, url) != BUDDY_URL_OK) {
    if (err && errCap) snprintf(err, errCap, "La URL del bridge no es valida");
    return false;
  }
  if (!token[0]) {
    if (err && errCap) {
      snprintf(err, errCap, "%s",
               ask ? "Falta el token de administracion del bridge"
                   : "Falta el token del bridge");
    }
    return false;
  }
  if (url->tls && !netStatus().timeSynced) {
    if (err && errCap) snprintf(err, errCap, "El reloj todavia no se sincronizo");
    return false;
  }
  return true;
}

BuddyLink linkOf(const BuddyUrl &url, const BuddyHttpResult &res) {
  BuddyLinkFacts facts;
  facts.configured = true;
  facts.urlValid = true;
  facts.tls = url.tls;
  facts.timeValid = netStatus().timeSynced;
  facts.status = res.status;
  facts.tlsFailed = res.tlsFailed;
  facts.redirected = res.redirected;
  return buddyLinkFromFacts(facts);
}

// Traduce el resultado de un POST de administración a la frase que el panel
// muestra. El 403 tiene su propia oración porque no es un token equivocado: es
// el caso en que el bridge no tiene token de administración configurado.
bool postAdmin(const char *path, const String &body, String *reply, char *err,
               size_t errCap) {
  BuddyUrl url;
  char token[SETTINGS_BUDDY_ASK_TOKEN_MAX + 1];
  if (!loadBridge(&url, token, sizeof(token), true, err, errCap)) return false;
  if (url.tls && !netStatus().timeSynced) {
    if (err && errCap) snprintf(err, errCap, "El reloj todavia no se sincronizo");
    return false;
  }

  BuddyHttpResult res;
  buddyHttpCall(url, path, "POST", token, body, &res);
  BuddyLink link = linkOf(url, res);
  if (link == BUDDY_LINK_HTTP && res.status == 403) {
    if (err && errCap) {
      snprintf(err, errCap, "El bridge no tiene token de administracion configurado");
    }
    return false;
  }
  if (link != BUDDY_LINK_OK) {
    if (err && errCap) snprintf(err, errCap, "%s", linkSentence(link));
    return false;
  }
  if (reply) *reply = res.body;
  return true;
}

void copyStr(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  snprintf(dst, cap, "%s", src ? src : "");
}

}  // namespace

bool buddyCalendarStatus(BuddyCalendarStatus *out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
  copyStr(out->calendarId, sizeof(out->calendarId), "primary");
  out->leadMin = 60;
  out->graceMin = 5;
  {
    char ask[SETTINGS_BUDDY_ASK_TOKEN_MAX + 1];
    settingsGetBuddyAskToken(ask, sizeof(ask));
    out->hasAskToken = ask[0] != '\0';
  }

  BuddyUrl url;
  char token[SETTINGS_BUDDY_TOKEN_MAX + 1];
  char err[64];
  if (!loadBridge(&url, token, sizeof(token), false, err, sizeof(err))) return false;

  BuddyHttpResult res;
  buddyHttpCall(url, "/calendar", "GET", token, String(), &res);
  out->status = res.status;
  if (linkOf(url, res) != BUDDY_LINK_OK) return false;

  JsonDocument doc;
  if (deserializeJson(doc, res.body) != DeserializationError::Ok) return false;
  out->configured = doc["configured"] | false;
  out->active = doc["active"] | false;
  out->hasClientId = doc["has_client_id"] | false;
  out->hasClientSecret = doc["has_client_secret"] | false;
  out->hasRefreshToken = doc["has_refresh_token"] | false;
  copyStr(out->calendarId, sizeof(out->calendarId), doc["calendar_id"] | "primary");
  out->leadMin = doc["lead_min"] | 60;
  out->graceMin = doc["grace_min"] | 5;
  copyStr(out->redirect, sizeof(out->redirect), doc["oauth_redirect"] | "");
  if (doc["next"].is<JsonObjectConst>()) {
    JsonObjectConst next = doc["next"].as<JsonObjectConst>();
    copyStr(out->nextSummary, sizeof(out->nextSummary), next["summary"] | "");
    out->nextInMin = next["in_min"] | 0;
    out->hasNext = out->nextSummary[0] != '\0';
  }
  return true;
}

bool buddyCalendarSave(const BuddyCalendarEdit &edit, char *err, size_t errCap) {
  if (err && errCap) err[0] = '\0';
  JsonDocument doc;
  // Un campo vacío no se manda: el bridge conserva lo que ya tenía. Así el panel
  // puede cambiar el calendario o el lead sin volver a tipear los secretos.
  if (edit.clientId && *edit.clientId) doc["GOOGLE_CLIENT_ID"] = edit.clientId;
  if (edit.clientSecret && *edit.clientSecret) {
    doc["GOOGLE_CLIENT_SECRET"] = edit.clientSecret;
  }
  if (edit.refreshToken && *edit.refreshToken) {
    doc["GOOGLE_REFRESH_TOKEN"] = edit.refreshToken;
  }
  if (edit.calendarId && *edit.calendarId) doc["GOOGLE_CALENDAR_ID"] = edit.calendarId;
  if (edit.leadMin > 0 && edit.leadMin <= 720) doc["GOOGLE_CALENDAR_LEAD_MIN"] = edit.leadMin;
  if (edit.graceMin >= 0 && edit.graceMin <= 120) {
    doc["GOOGLE_CALENDAR_GRACE_MIN"] = edit.graceMin;
  }
  String body;
  serializeJson(doc, body);
  return postAdmin("/calendar", body, nullptr, err, errCap);
}

bool buddyCalendarForget(char *err, size_t errCap) {
  if (err && errCap) err[0] = '\0';
  return postAdmin("/calendar", String("{\"clear\":true}"), nullptr, err, errCap);
}

bool buddyCalendarOAuthStart(const char *clientId, const char *clientSecret,
                             char *urlOut, size_t urlCap, char *err,
                             size_t errCap) {
  if (err && errCap) err[0] = '\0';
  if (urlOut && urlCap) urlOut[0] = '\0';

  JsonDocument req;
  if (clientId && *clientId) req["client_id"] = clientId;
  if (clientSecret && *clientSecret) req["client_secret"] = clientSecret;
  String body;
  serializeJson(req, body);

  String reply;
  if (!postAdmin("/calendar/oauth/start", body, &reply, err, errCap)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, reply) != DeserializationError::Ok) {
    if (err && errCap) snprintf(err, errCap, "El bridge no devolvio una URL valida");
    return false;
  }
  const char *url = doc["authorize_url"] | "";
  if (!*url) {
    if (err && errCap) snprintf(err, errCap, "El bridge no devolvio una URL valida");
    return false;
  }
  copyStr(urlOut, urlCap, url);
  return true;
}

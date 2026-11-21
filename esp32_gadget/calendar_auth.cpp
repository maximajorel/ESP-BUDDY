#include "calendar_auth.h"

#include <ArduinoJson.h>
#include <string.h>

#include "calendar_auth_parse.h"
#include "calendar_forms.h"
#include "calendar_http.h"
#include "calendar_store.h"

namespace {

constexpr size_t kFormBytes = 2600;
constexpr size_t kTokenBodyBytes = 4096;
// Refresh a little before the hour is up, so a request never leaves with a token
// that expires while it is in flight.
constexpr uint32_t kRefreshMarginMs = 30000;

SemaphoreHandle_t s_lock = nullptr;
CalendarFlow s_flow;
char s_deviceCode[CALENDAR_DEVICE_CODE_MAX + 1];
uint32_t s_lastPollMs = 0;
char s_access[CALENDAR_TOKEN_MAX + 1];
uint32_t s_accessExpiresAtMs = 0;

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

void copyErr(char *err, size_t cap, const char *text) {
  if (!err || cap == 0) return;
  snprintf(err, cap, "%s", text ? text : "");
}

void storeAccess(const char *token, long expiresInSec) {
  snprintf(s_access, sizeof(s_access), "%s", token ? token : "");
  s_accessExpiresAtMs = millis() + (uint32_t)expiresInSec * 1000u;
}

void clearFlow() {
  memset(&s_flow, 0, sizeof(s_flow));
  s_deviceCode[0] = '\0';
  s_lastPollMs = 0;
}

}  // namespace

void calendarAuthBegin() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  clearFlow();
  s_access[0] = '\0';
  s_accessExpiresAtMs = 0;
}

bool calendarAuthStartFlow(char *err, size_t errCap) {
  Guard guard;
  if (err && errCap) err[0] = '\0';
  clearFlow();
  if (!calendarStoreConfigured()) {
    copyErr(err, errCap, "Guarda el Client ID y el secret primero");
    return false;
  }

  char form[kFormBytes];
  if (!calendarDeviceCodeForm(calendarStoreClientId(), form, sizeof(form))) {
    copyErr(err, errCap, "Las credenciales no entran en el pedido");
    return false;
  }

  CalendarHttpResult res;
  if (!calendarHttpPostForm(CALENDAR_OAUTH_HOST, CALENDAR_DEVICE_CODE_PATH,
                            String(form), kTokenBodyBytes, &res)) {
    copyErr(err, errCap, "No pude iniciar la conexion con Google");
    return false;
  }
  // The device-code answer is classified by the same pure parser that reads a
  // token answer: the panel gets one Spanish sentence and no raw body.
  JsonDocument doc;
  const bool parsed =
      deserializeJson(doc, res.body) == DeserializationError::Ok;
  if (res.status != 200) {
    CalendarOAuthResponse mapped;
    calendarParseTokenResponse(parsed, doc.as<JsonVariantConst>(), res.status,
                               &mapped);
    copyErr(err, errCap,
            mapped.error[0] ? mapped.error : "Google rechazo el pedido");
    return false;
  }
  if (!parsed) {
    copyErr(err, errCap, "Respuesta de Google ilegible");
    return false;
  }

  CalendarDeviceCode code;
  calendarParseDeviceCodeResponse(doc.as<JsonVariantConst>(), &code);
  if (!code.deviceCode[0] || !code.userCode[0]) {
    copyErr(err, errCap, "Google no devolvio el codigo de dispositivo");
    return false;
  }

  snprintf(s_deviceCode, sizeof(s_deviceCode), "%s", code.deviceCode);
  snprintf(s_flow.userCode, sizeof(s_flow.userCode), "%s", code.userCode);
  snprintf(s_flow.verificationUrl, sizeof(s_flow.verificationUrl), "%s",
           code.verificationUrl);
  s_flow.intervalSec = code.intervalSec;
  s_flow.expiresAtMs = millis() + (uint32_t)code.expiresInSec * 1000u;
  s_flow.pending = true;
  s_lastPollMs = millis();
  return true;
}

void calendarAuthCancelFlow() {
  Guard guard;
  clearFlow();
}

bool calendarAuthFlowPending() {
  Guard guard;
  return s_flow.pending;
}

void calendarAuthFlowCopy(CalendarFlow *out) {
  if (!out) return;
  Guard guard;
  *out = s_flow;
}

bool calendarAuthPollFlow(bool *authorized, char *err, size_t errCap) {
  if (authorized) *authorized = false;
  if (err && errCap) err[0] = '\0';
  Guard guard;
  if (!s_flow.pending) return false;

  const uint32_t now = millis();
  if (now >= s_flow.expiresAtMs) {
    clearFlow();
    copyErr(err, errCap, "El codigo vencio, pedi uno nuevo");
    return false;
  }
  if (s_lastPollMs && now - s_lastPollMs < (uint32_t)s_flow.intervalSec * 1000u) {
    return false;
  }
  s_lastPollMs = now;

  char form[kFormBytes];
  if (!calendarTokenFormDevice(calendarStoreClientId(),
                               calendarStoreClientSecret(), s_deviceCode, form,
                               sizeof(form))) {
    clearFlow();
    copyErr(err, errCap, "Las credenciales no entran en el pedido");
    return false;
  }

  CalendarHttpResult res;
  if (!calendarHttpPostForm(CALENDAR_OAUTH_HOST, CALENDAR_TOKEN_PATH,
                            String(form), kTokenBodyBytes, &res)) {
    copyErr(err, errCap, "No pude consultar a Google");
    return false;
  }

  JsonDocument doc;
  const bool parsed =
      deserializeJson(doc, res.body) == DeserializationError::Ok;
  CalendarOAuthResponse answer;
  calendarParseTokenResponse(parsed, doc.as<JsonVariantConst>(), res.status,
                             &answer);

  switch (answer.outcome) {
    case CALENDAR_OAUTH_AUTHORIZED:
      if (!answer.refreshToken[0]) {
        // Without it the board would ask for the code again every hour. Stop now,
        // while the owner is still in front of the panel.
        clearFlow();
        copyErr(err, errCap,
                "Google no devolvio el refresh token, proba de nuevo");
        return false;
      }
      if (!calendarStoreSetRefreshToken(answer.refreshToken)) {
        clearFlow();
        copyErr(err, errCap, "El refresh token no se pudo guardar");
        return false;
      }
      storeAccess(answer.accessToken, answer.expiresInSec);
      clearFlow();
      if (authorized) *authorized = true;
      return true;
    case CALENDAR_OAUTH_PENDING:
      return false;  // the owner has not approved yet; this is not an error
    case CALENDAR_OAUTH_SLOW_DOWN:
      s_flow.intervalSec += 5;
      if (s_flow.intervalSec > 60) s_flow.intervalSec = 60;
      return false;
    default:
      clearFlow();
      copyErr(err, errCap, answer.error);
      return false;
  }
}

bool calendarAuthEnsureAccessToken(bool force, char *err, size_t errCap) {
  if (err && errCap) err[0] = '\0';
  Guard guard;
  const uint32_t now = millis();
  if (!force && s_access[0] &&
      (int32_t)(s_accessExpiresAtMs - kRefreshMarginMs - now) > 0) {
    return true;
  }
  s_access[0] = '\0';
  if (!calendarStoreAuthorized()) {
    copyErr(err, errCap, "Falta conectar Google");
    return false;
  }

  char form[kFormBytes];
  if (!calendarTokenFormRefresh(calendarStoreClientId(),
                                calendarStoreClientSecret(),
                                calendarStoreRefreshToken(), form,
                                sizeof(form))) {
    copyErr(err, errCap, "Las credenciales no entran en el pedido");
    return false;
  }

  CalendarHttpResult res;
  if (!calendarHttpPostForm(CALENDAR_OAUTH_HOST, CALENDAR_TOKEN_PATH,
                            String(form), kTokenBodyBytes, &res)) {
    copyErr(err, errCap, "No pude consultar a Google");
    return false;
  }

  JsonDocument doc;
  const bool parsed =
      deserializeJson(doc, res.body) == DeserializationError::Ok;
  CalendarOAuthResponse answer;
  calendarParseTokenResponse(parsed, doc.as<JsonVariantConst>(), res.status,
                             &answer);
  if (answer.outcome == CALENDAR_OAUTH_AUTHORIZED) {
    storeAccess(answer.accessToken, answer.expiresInSec);
    // Google usually does not rotate the refresh token; keep the new one when it
    // does, so a rotation never leaves the old one stored.
    if (answer.refreshToken[0]) {
      calendarStoreSetRefreshToken(answer.refreshToken);
    }
    return true;
  }
  if (answer.outcome == CALENDAR_OAUTH_INVALID_GRANT) {
    calendarStoreForgetToken();
    copyErr(err, errCap, answer.error);
    return false;
  }
  copyErr(err, errCap,
          answer.error[0] ? answer.error : "Google no renovo el permiso");
  return false;
}

const char *calendarAuthAccessToken() {
  Guard guard;
  return s_access;
}

long calendarAuthSecondsLeft() {
  Guard guard;
  if (!s_access[0]) return 0;
  const int32_t delta = (int32_t)(s_accessExpiresAtMs - millis());
  return delta > 0 ? delta / 1000 : 0;
}

void calendarAuthResetAccess() {
  Guard guard;
  s_access[0] = '\0';
  s_accessExpiresAtMs = 0;
}

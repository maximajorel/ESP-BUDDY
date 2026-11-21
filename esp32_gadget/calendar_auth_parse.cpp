#include "calendar_auth_parse.h"

#include <string.h>

namespace {

void copyText(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  snprintf(dst, cap, "%s", src ? src : "");
}

long clampLong(long value, long low, long high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void describeError(JsonVariantConst root, const char *fallback,
                   CalendarOAuthResponse *out) {
  const char *description = root["error_description"] | "";
  copyText(out->error, sizeof(out->error),
           *description ? description : fallback);
}

}  // namespace

void calendarParseDeviceCodeResponse(JsonVariantConst root,
                                     CalendarDeviceCode *out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->intervalSec = 5;
  out->expiresInSec = 1800;
  if (root.isNull()) return;

  copyText(out->deviceCode, sizeof(out->deviceCode), root["device_code"] | "");
  copyText(out->userCode, sizeof(out->userCode), root["user_code"] | "");
  const char *url = root["verification_url"] | "";
  copyText(out->verificationUrl, sizeof(out->verificationUrl),
           *url ? url : "https://www.google.com/device");
  out->intervalSec =
      (int)clampLong(root["interval"] | 5, 1, 60);
  out->expiresInSec = clampLong(root["expires_in"] | 1800, 60, 3600);
}

void calendarParseTokenResponse(bool parsed, JsonVariantConst root, int status,
                                CalendarOAuthResponse *out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->outcome = CALENDAR_OAUTH_UNREADABLE;
  out->expiresInSec = 3600;
  copyText(out->error, sizeof(out->error), "Google rechazo la autorizacion");
  if (!parsed) return;

  if (status == 200) {
    const char *access = root["access_token"] | "";
    if (!*access) {
      out->outcome = CALENDAR_OAUTH_ERROR;
      copyText(out->error, sizeof(out->error),
               "Google no devolvio el access token");
      return;
    }
    const char *refresh = root["refresh_token"] | "";
    copyText(out->accessToken, sizeof(out->accessToken), access);
    copyText(out->refreshToken, sizeof(out->refreshToken), refresh);
    out->expiresInSec = clampLong(root["expires_in"] | 3600, 60, 86400);
    out->outcome = CALENDAR_OAUTH_AUTHORIZED;
    out->error[0] = '\0';
    return;
  }

  const char *error = root["error"] | "";
  if (strcmp(error, "authorization_pending") == 0) {
    out->outcome = CALENDAR_OAUTH_PENDING;
    out->error[0] = '\0';
    return;
  }
  if (strcmp(error, "slow_down") == 0) {
    out->outcome = CALENDAR_OAUTH_SLOW_DOWN;
    out->error[0] = '\0';
    return;
  }
  if (strcmp(error, "access_denied") == 0) {
    out->outcome = CALENDAR_OAUTH_DENIED;
    copyText(out->error, sizeof(out->error), "Cancelaste el permiso en Google");
    return;
  }
  if (strcmp(error, "expired_token") == 0) {
    out->outcome = CALENDAR_OAUTH_EXPIRED;
    copyText(out->error, sizeof(out->error), "El codigo vencio, pedi uno nuevo");
    return;
  }
  if (strcmp(error, "invalid_grant") == 0) {
    out->outcome = CALENDAR_OAUTH_INVALID_GRANT;
    copyText(out->error, sizeof(out->error),
             "El permiso vencio o lo revocaste; conecta Google de nuevo");
    return;
  }
  out->outcome = CALENDAR_OAUTH_ERROR;
  describeError(root, "Google rechazo la autorizacion", out);
}

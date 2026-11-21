// Google's OAuth answers, classified once.
//
// Pure enough for the host (ArduinoJson is the only dependency, no Arduino.h):
// the device flow's success path is the one thing the agent cannot exercise
// against a real account, so at least the response handling is pinned by
// `tools/calendar_core_test.cpp` instead of only being read. `calendar_auth.cpp`
// keeps the transport and the storage; every decision about what a body means
// lives here.
#pragma once

#include <ArduinoJson.h>

#include "calendar_model.h"

enum CalendarOAuthOutcome {
  CALENDAR_OAUTH_PENDING,        // authorization_pending: keep polling
  CALENDAR_OAUTH_SLOW_DOWN,      // slow_down: keep polling with a longer interval
  CALENDAR_OAUTH_AUTHORIZED,     // access token (and maybe a refresh token)
  CALENDAR_OAUTH_DENIED,         // access_denied: the owner cancelled
  CALENDAR_OAUTH_EXPIRED,        // expired_token: ask for a new code
  CALENDAR_OAUTH_INVALID_GRANT,  // the refresh token is dead
  CALENDAR_OAUTH_ERROR,          // Google answered with something else
  CALENDAR_OAUTH_UNREADABLE,     // the body was not JSON at all
};

struct CalendarDeviceCode {
  char deviceCode[CALENDAR_DEVICE_CODE_MAX + 1];
  char userCode[CALENDAR_USER_CODE_MAX + 1];
  char verificationUrl[CALENDAR_VERIFY_URL_MAX + 1];
  int intervalSec;
  long expiresInSec;
};

struct CalendarOAuthResponse {
  CalendarOAuthOutcome outcome;
  long expiresInSec;
  char accessToken[CALENDAR_TOKEN_MAX + 1];
  // Optional in a refresh exchange: Google usually does not rotate it. The device
  // flow's caller enforces its presence, because a missing refresh token there
  // would mean asking for a new code every hour.
  char refreshToken[CALENDAR_TOKEN_MAX + 1];
  char error[80];  // Spanish sentence for the panel; empty for pending outcomes
};

// Reads `{device_code, user_code, verification_url, expires_in, interval}` and
// clamps the values to what the poll loop can afford. An empty `deviceCode`
// means the body was not a device-code answer.
void calendarParseDeviceCodeResponse(JsonVariantConst root, CalendarDeviceCode *out);

// Classifies a token-endpoint answer. `parsed` is false when the body was not
// JSON; `status` is the HTTP code. On AUTHORIZED it copies both tokens and the
// lifetime; on a terminal outcome it fills `error` with the sentence the panel
// shows, never Google's raw wording when a mapped one exists.
void calendarParseTokenResponse(bool parsed, JsonVariantConst root, int status,
                                CalendarOAuthResponse *out);

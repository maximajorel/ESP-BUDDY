// Google's device flow on the board.
//
// The device has no public URL, so it cannot receive a redirect. Google's device
// flow exists exactly for that: the board asks for a short code, the owner types
// it at google.com/device, and the board polls until the permission is granted.
// No redirect URI, no copy of a broken address bar, no refresh token pasted by
// hand.
//
// The device code, the user code and the tokens never leave RAM except for the
// refresh token, which is the one long-lived secret and lives in
// `calendar_store`. The access token expires hourly and is not persisted.
#pragma once

#include <Arduino.h>

#define CALENDAR_DEVICE_CODE_MAX 256
#define CALENDAR_USER_CODE_MAX 16
#define CALENDAR_VERIFY_URL_MAX 96

struct CalendarFlow {
  bool pending;
  char userCode[CALENDAR_USER_CODE_MAX + 1];
  char verificationUrl[CALENDAR_VERIFY_URL_MAX + 1];
  int intervalSec;
  uint32_t expiresAtMs;
};

void calendarAuthBegin();

// Asks Google for a device code. Requires the client pair stored. On success the
// panel can show `userCode` and `verificationUrl`; the flow stays pending until
// the owner approves or it expires.
bool calendarAuthStartFlow(char *err, size_t errCap);

void calendarAuthCancelFlow();
bool calendarAuthFlowPending();
void calendarAuthFlowCopy(CalendarFlow *out);

// One poll of the token endpoint, rate-limited by Google's `interval`. Returns
// false while the owner has not approved yet (with an empty `err`), and true when
// the refresh token was stored (`*authorized` is then true). A terminal problem
// also returns false but fills `err`.
bool calendarAuthPollFlow(bool *authorized, char *err, size_t errCap);

// A usable access token in RAM, refreshed when `force` or near expiry. A revoked
// or expired permission drops the refresh token and explains it in `err`.
bool calendarAuthEnsureAccessToken(bool force, char *err, size_t errCap);

const char *calendarAuthAccessToken();
long calendarAuthSecondsLeft();
void calendarAuthResetAccess();

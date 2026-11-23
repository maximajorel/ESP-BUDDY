// The calendar service: credentials, the Google poll and the answer the face asks
// for every frame.
//
// Its own task, its own TLS client (through `calendar_http`) and its own mutex.
// The face never blocks on the network: it reads the last snapshot and the pure
// rules in `calendar_plan.h` decide which event owns the glass right now.
#pragma once

#include <Arduino.h>

#include "calendar_auth.h"
#include "calendar_model.h"

// The month payload's shape: `CalendarMonthInfo`, the same struct the JSON
// builder takes, so the caller cannot describe the month differently from the
// way the device fetched it.
#include "calendar_month.h"

enum CalendarStatusKind {
  CALENDAR_UNCONFIGURED = 0,  // no client id / secret yet
  CALENDAR_NEEDS_AUTH,        // credentials stored, Google not connected
  CALENDAR_AUTH_PENDING,      // the owner is entering the code on Google
  CALENDAR_READY,             // authorized; lastError says if the last fetch failed
};

// Everything the panel needs, small enough to copy by value (~700 bytes).
struct CalendarStatusInfo {
  CalendarStatusKind kind;
  bool configured;
  bool authorized;
  bool pending;
  bool syncing;
  char userCode[CALENDAR_USER_CODE_MAX + 1];
  char verificationUrl[CALENDAR_VERIFY_URL_MAX + 1];
  int pendingSecondsLeft;
  char selectedId[CALENDAR_ID_MAX + 1];
  char selectedName[CALENDAR_NAME_MAX + 1];
  int fallbackLeadMin;
  int graceMin;
  char lastError[96];
  uint32_t lastSyncMs;
  size_t calendarCount;
  size_t eventCount;
  bool hasNext;
  char nextSummary[CALENDAR_SUMMARY_MAX];
  int32_t nextMsToStart;
  int nextLeadMin;
};

void calendarBegin();

// Called by the service task every second. Also exposed so the console can pump
// it in-process when the board has no network task for a probe.
void calendarTick();

// Copies the status under the lock. Always safe to call from the web task.
CalendarStatusInfo calendarStatusInfo();

// Copies one calendar of the list. Returns false past the end.
bool calendarCalendarAt(size_t index, CalendarInfo *out);

// Copies up to `cap` stored events into `out`, in the order the device keeps
// them, and returns how many were copied. It copies instead of lending the
// internal array because the sync tick rewrites that array with a memcpy under
// the lock: a pointer would dangle the moment the next fetch lands, and the web
// task serializes the events long after it took them.
size_t calendarEventsCopy(CalendarEvent *out, size_t cap);

// The selected calendar's display name, for the split frame's footer. Empty
// selection reports "PRINCIPAL" so the line never reads as a missing label.
void calendarSelectedName(char *out, size_t cap);

// The face's question: does an event own the glass right now? Cheap, no network,
// and it reads the stored events under the lock. Inactive when the clock is not
// synchronized, because the window is arithmetic on real time.
CalendarReaction calendarReactionNow();

// Asks the task for a fetch on its next tick (panel "actualizar", or a change of
// calendar). Returns false when the calendar is not authorized yet.
bool calendarRequestRefresh();

// One month of the selected calendar, fetched on demand for the tray's grid.
//
// This one talks to Google from the caller's task — the web task — instead of
// waiting for the face's fifteen-minute poll, because the owner asked for a month
// that the poll never fetches: the poll only ever holds today plus a day. It is
// synchronous and bounded by the TLS timeout, `calendarMonthWindow` decides which
// days the grid covers, and `out` must be a `cap`-sized array of
// `CalendarMonthEvent`, which the caller owns (PSRAM on the board).
//
// `info->truncated` says Google returned at least as many events as fit, so the
// app can tell the owner the month is cut instead of drawing a full-looking lie.
// `count` is how many rows were written into `out`.
bool calendarFetchMonth(int year, int month, CalendarMonthEvent *out, size_t cap,
                        size_t *count, CalendarMonthInfo *info, char *err,
                        size_t errCap);

// The result of one write, with the sentence the tray shows the owner.
//
// `ok` is the only thing the tray branches on. `status` is Google's code (0 when
// the request never left the board) and exists for diagnosis, never for the
// owner: turning 412 into "recargá el mes" is this file's job, not the app's.
struct CalendarWriteOutcome {
  bool ok;
  int status;
  char message[160];
};

// Create, change and delete, in the selected calendar, through the device's own
// Google grant. All three refuse before touching the network when the saved grant
// was made with the read-only scope: Google answers 403 and the owner would lose
// the edit without knowing why.
//
// `etag` is what makes an update honest: a mismatch is reported as a conflict
// instead of overwriting someone else's change.
CalendarWriteOutcome calendarCreateEvent(const char *summary,
                                         const char *location,
                                         int64_t startEpoch, int64_t endEpoch,
                                         bool allDay);
CalendarWriteOutcome calendarUpdateEvent(const char *id, const char *etag,
                                         const char *summary,
                                         const char *location,
                                         int64_t startEpoch, int64_t endEpoch,
                                         bool allDay);
CalendarWriteOutcome calendarDeleteEvent(const char *id, const char *etag);

// Serial-console dump hook, the same trust model as `buddyface` and `imufake`:
// fabricates one reaction so the glass can be proven without a Google account,
// NTP or network. Never persisted; the next real fetch replaces nothing because
// it only affects `calendarReactionNow()`. `minutesToStart` may be negative for
// an event that already started.
void calendarDebugFakeEvent(int minutesToStart, int leadMin);
void calendarDebugFakeOff();

// Panel actions. StartFlow talks to Google and may take a couple of seconds.
bool calendarStartFlow(char *err, size_t errCap);
void calendarCancelFlow();
bool calendarSelect(const char *id, const char *name);

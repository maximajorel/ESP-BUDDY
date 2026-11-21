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

// The face's question: does an event own the glass right now? Cheap, no network,
// and it reads the stored events under the lock. Inactive when the clock is not
// synchronized, because the window is arithmetic on real time.
CalendarReaction calendarReactionNow();

// Asks the task for a fetch on its next tick (panel "actualizar", or a change of
// calendar). Returns false when the calendar is not authorized yet.
bool calendarRequestRefresh();

// Panel actions. StartFlow talks to Google and may take a couple of seconds.
bool calendarStartFlow(char *err, size_t errCap);
void calendarCancelFlow();
bool calendarSelect(const char *id, const char *name);

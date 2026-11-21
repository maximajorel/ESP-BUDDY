// The local calendar's data, shared by the pure rules and the service.
//
// No `Arduino.h` here on purpose: `calendar_time.h`, `calendar_plan.h` and the
// parse half compile on the host, and `tools/calendar_core_test.cpp` is the
// proof that the arithmetic behind the face is right before a board sees it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define CALENDAR_ID_MAX 128
// Protocol limits, shared by the pure OAuth response parser and the store so the
// two cannot disagree about how much a token can be.
#define CALENDAR_TOKEN_MAX 511
#define CALENDAR_DEVICE_CODE_MAX 256
#define CALENDAR_USER_CODE_MAX 16
#define CALENDAR_VERIFY_URL_MAX 96
#define CALENDAR_NAME_MAX 64
#define CALENDAR_SUMMARY_MAX 96
#define CALENDAR_LOCATION_MAX 96

// One page of Google's list is plenty for a personal account, and a fixed array
// keeps the selector out of the heap.
#define CALENDAR_MAX_LIST 24
#define CALENDAR_MAX_EVENTS 12

// Google accepts reminders up to four weeks ahead. A face that stays alert for
// a month is not a warning any more, so the reaction caps the lead at one day.
#define CALENDAR_MAX_LEAD_MIN 1440

struct CalendarInfo {
  char id[CALENDAR_ID_MAX];
  char name[CALENDAR_NAME_MAX];
  bool primary;
  // The calendar's own default reminder, in minutes. -1 means "this calendar
  // declares no default", which lets the panel's fallback decide.
  int defaultLeadMin;
};

struct CalendarEvent {
  char summary[CALENDAR_SUMMARY_MAX];
  char location[CALENDAR_LOCATION_MAX];
  int64_t startEpoch;  // UTC seconds
  int64_t endEpoch;    // UTC seconds; for an all-day event, the next midnight
  // The reminder that opens the window, already resolved. -1 means this event
  // must never react: the owner turned its reminders off.
  int leadMin;
  bool allDay;
};

enum CalendarPhase {
  CALENDAR_PHASE_NONE = 0,
  CALENDAR_PHASE_EARLY,     // inside the window, more than half of it left
  CALENDAR_PHASE_NEAR,      // half to the last stretch
  CALENDAR_PHASE_STARTING,  // the final 15% of the lead window
  CALENDAR_PHASE_ONGOING,   // the event already started, within the grace
};

struct CalendarReaction {
  bool active;
  CalendarPhase phase;
  uint8_t intensity;  // 0..100, how much of the lead window is consumed
  int32_t msToStart;  // negative once the event started
  int leadMin;
  int graceMin;
  int64_t startEpoch;  // for the clock in the split frame
  char summary[CALENDAR_SUMMARY_MAX];
  char location[CALENDAR_LOCATION_MAX];
};

// The calendar's decision: which event owns the face right now.
//
// Pure and header-only. The service collects, this file decides; the face asks
// it every frame. The rules come from the owner's request: the reminder stored
// on the event sets the warning window, and the reaction grows as the event gets
// closer. A reminder the owner turned off is respected: the event never reacts.
#pragma once

#include <stdio.h>
#include <string.h>

#include "calendar_model.h"

static inline const char *calendarPhaseName(CalendarPhase phase) {
  switch (phase) {
    case CALENDAR_PHASE_EARLY: return "early";
    case CALENDAR_PHASE_NEAR: return "near";
    case CALENDAR_PHASE_STARTING: return "starting";
    case CALENDAR_PHASE_ONGOING: return "ongoing";
    case CALENDAR_PHASE_NONE: break;
  }
  return "none";
}

// Whether `now` falls in [start - lead, min(end, start + grace)]. Separated so
// the window and the intensity cannot drift apart.
static inline bool calendarInWindow(const CalendarEvent &event, int64_t nowEpoch,
                                    int graceMin) {
  if (event.leadMin < 0) return false;
  const int64_t leadMs = (int64_t)event.leadMin * 60000;
  const int64_t startMs = event.startEpoch * 1000;
  const int64_t endMs = event.endEpoch * 1000;
  const int64_t graceEnd = startMs + (int64_t)graceMin * 60000;
  const int64_t windowEnd = endMs < graceEnd ? endMs : graceEnd;
  const int64_t nowMs = nowEpoch * 1000;
  return nowMs >= startMs - leadMs && nowMs <= windowEnd;
}

// Chooses the event the face should react to. A timed event always beats an
// all-day one, because a birthday banner should not hide the meeting in twenty
// minutes. Among equals the earliest start wins, so a window can never show the
// later of two events that overlap.
static inline void calendarPickReaction(const CalendarEvent *events, size_t count,
                                        int64_t nowEpoch, int graceMin,
                                        CalendarReaction *out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->phase = CALENDAR_PHASE_NONE;
  out->graceMin = graceMin;
  if (!events) return;

  const CalendarEvent *best = nullptr;
  for (size_t i = 0; i < count; i++) {
    const CalendarEvent &e = events[i];
    if (!calendarInWindow(e, nowEpoch, graceMin)) continue;
    if (!best) {
      best = &e;
      continue;
    }
    if (best->allDay && !e.allDay) {
      best = &e;
    } else if (best->allDay == e.allDay && e.startEpoch < best->startEpoch) {
      best = &e;
    }
  }
  if (!best) return;

  const int64_t deltaMs = (best->startEpoch - nowEpoch) * 1000;
  int32_t msToStart;
  if (deltaMs > 2147483647LL) {
    msToStart = 2147483647;
  } else if (deltaMs < -2147483647LL) {
    msToStart = -2147483647;
  } else {
    msToStart = (int32_t)deltaMs;
  }

  uint8_t intensity = 100;
  CalendarPhase phase = CALENDAR_PHASE_ONGOING;
  if (deltaMs > 0) {
    const int64_t leadMs = (int64_t)best->leadMin * 60000;
    if (leadMs > 0) {
      const int64_t consumed = leadMs - deltaMs;
      long pct = (long)(consumed * 100 / leadMs);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      intensity = (uint8_t)pct;
    } else {
      intensity = 100;
    }
    if (intensity >= 85) {
      phase = CALENDAR_PHASE_STARTING;
    } else if (intensity >= 50) {
      phase = CALENDAR_PHASE_NEAR;
    } else {
      phase = CALENDAR_PHASE_EARLY;
    }
  }

  out->active = true;
  out->phase = phase;
  out->intensity = intensity;
  out->msToStart = msToStart;
  out->leadMin = best->leadMin;
  snprintf(out->summary, sizeof(out->summary), "%s", best->summary);
}

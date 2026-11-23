#include "calendar_write.h"

#include <stdio.h>

#include "calendar_time.h"

namespace {

// The local date of an instant, `YYYY-MM-DD`. Used for all-day events, where
// Google wants a date and not a timestamp.
bool localDate(int64_t epoch, int tzOffsetHours, char *out, size_t cap) {
  int64_t local = epoch + (int64_t)tzOffsetHours * 3600;
  int64_t days = local / 86400;
  if (local % 86400 < 0) days -= 1;
  int year;
  unsigned month, day;
  calendarCivilFromDays(days, &year, &month, &day);
  const int n = snprintf(out, cap, "%04d-%02u-%02u", year, month, day);
  return n > 0 && (size_t)n < cap;
}

// The local wall clock of an instant with the owner's offset written out, the
// same shape Google sends back. The device stores whole hours, so the offset is
// always `+HH:00` or `-HH:00`; sending `Z` instead would move the event's
// timezone to UTC on every edit.
bool localRfc3339(int64_t epoch, int tzOffsetHours, char *out, size_t cap) {
  const int64_t offset = (int64_t)tzOffsetHours * 3600;
  const int64_t local = epoch + offset;
  int64_t days = local / 86400;
  int64_t rest = local % 86400;
  if (rest < 0) {
    rest += 86400;
    days -= 1;
  }
  int year;
  unsigned month, day;
  calendarCivilFromDays(days, &year, &month, &day);
  const int hours = (int)(rest / 3600);
  const int minutes = (int)((rest % 3600) / 60);
  const int seconds = (int)(rest % 60);
  const int absolute = tzOffsetHours < 0 ? -tzOffsetHours : tzOffsetHours;
  const char sign = tzOffsetHours < 0 ? '-' : '+';
  const int n = snprintf(out, cap, "%04d-%02u-%02uT%02d:%02d:%02d%c%02d:00",
                         year, month, day, hours, minutes, seconds, sign,
                         absolute);
  return n > 0 && (size_t)n < cap;
}

}  // namespace

bool calendarEventBodyJson(const char *summary, const char *location,
                           int64_t startEpoch, int64_t endEpoch, bool allDay,
                           int tzOffsetHours, JsonDocument &doc) {
  doc["summary"] = summary ? summary : "";
  // Always sent, even when empty: a PATCH that leaves it out keeps the old place,
  // and the tray's "borrar el lugar" would look like it did nothing.
  doc["location"] = location ? location : "";

  char from[32];
  char to[32];
  if (allDay) {
    if (!localDate(startEpoch, tzOffsetHours, from, sizeof(from))) return false;
    // Exclusive end: the day after the last second the event covers.
    if (!localDate(endEpoch + 1, tzOffsetHours, to, sizeof(to))) return false;
    doc["start"]["date"] = from;
    doc["end"]["date"] = to;
    return true;
  }

  if (!localRfc3339(startEpoch, tzOffsetHours, from, sizeof(from))) return false;
  if (!localRfc3339(endEpoch, tzOffsetHours, to, sizeof(to))) return false;
  doc["start"]["dateTime"] = from;
  doc["end"]["dateTime"] = to;
  return true;
}

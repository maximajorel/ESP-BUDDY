// Calendar timestamps without a timezone database.
//
// Google sends RFC 3339 for timed events (`2026-11-21T15:00:00-03:00`) and a
// plain date for all-day ones. The device has a fixed UTC offset, not a zone
// database, so the offset in the string is used when it is there and the
// owner's configured offset when it is not. Pure and header-only: the epoch is
// the input to every reaction rule, so it is the first thing pinned by
// `tools/calendar_core_test.cpp`.
#pragma once

#include <stdint.h>
#include <stdio.h>

// Days since 1970-01-01 for a proleptic Gregorian date. Howard Hinnant's
// civil-from-days, integer only: no <time.h>, no locale, no libc clock.
static inline int64_t calendarDaysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static inline bool calendarLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static inline unsigned calendarDaysInMonth(int y, unsigned m) {
  static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  if (m == 2 && calendarLeapYear(y)) return 29;
  return kDays[m - 1];
}

static inline bool calendarReadDigits(const char *&p, int count, int *out) {
  int value = 0;
  for (int i = 0; i < count; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    value = value * 10 + (p[i] - '0');
  }
  p += count;
  *out = value;
  return true;
}

static inline int64_t calendarEpochFromParts(int y, int mo, int d, int h, int mi,
                                             int s, int offsetSeconds) {
  const int64_t days = calendarDaysFromCivil(y, (unsigned)mo, (unsigned)d);
  return days * 86400 + (int64_t)h * 3600 + (int64_t)mi * 60 + s - offsetSeconds;
}

// `YYYY-MM-DDTHH:MM:SS` plus an optional fraction and either `Z`, `+HH:MM` or
// `-HH:MM`. Returns false on anything else, including a date that does not
// exist, so a malformed event is dropped instead of landing in 1970.
static inline bool calendarParseRfc3339(const char *text, int64_t *epochOut) {
  if (!text || !epochOut) return false;
  const char *p = text;
  int y, mo, d, h, mi, s;
  if (!calendarReadDigits(p, 4, &y)) return false;
  if (*p++ != '-') return false;
  if (!calendarReadDigits(p, 2, &mo)) return false;
  if (*p++ != '-') return false;
  if (!calendarReadDigits(p, 2, &d)) return false;
  if (*p != 'T' && *p != 't' && *p != ' ') return false;
  p++;
  if (!calendarReadDigits(p, 2, &h)) return false;
  if (*p++ != ':') return false;
  if (!calendarReadDigits(p, 2, &mi)) return false;
  if (*p++ != ':') return false;
  if (!calendarReadDigits(p, 2, &s)) return false;

  // Google can send sub-second precision; the reaction works in seconds.
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') p++;
  }

  int offsetSeconds = 0;
  if (*p == 'Z' || *p == 'z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    const int sign = *p == '-' ? -1 : 1;
    p++;
    int oh, om;
    if (!calendarReadDigits(p, 2, &oh)) return false;
    if (*p == ':') p++;
    if (!calendarReadDigits(p, 2, &om)) return false;
    offsetSeconds = sign * (oh * 3600 + om * 60);
  } else {
    return false;
  }
  if (*p != '\0') return false;

  if (mo < 1 || mo > 12) return false;
  if (d < 1 || (unsigned)d > calendarDaysInMonth(y, (unsigned)mo)) return false;
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) return false;

  *epochOut = calendarEpochFromParts(y, mo, d, h, mi, s, offsetSeconds);
  return true;
}

// `YYYY-MM-DD` at local midnight, using the owner's fixed UTC offset (hours,
// -3 for Argentina). An all-day event has no time, and the device has no zone
// database, so this is the honest reading.
static inline bool calendarParseDate(const char *text, int tzOffsetHours,
                                     int64_t *epochOut) {
  if (!text || !epochOut) return false;
  const char *p = text;
  int y, mo, d;
  if (!calendarReadDigits(p, 4, &y)) return false;
  if (*p++ != '-') return false;
  if (!calendarReadDigits(p, 2, &mo)) return false;
  if (*p++ != '-') return false;
  if (!calendarReadDigits(p, 2, &d)) return false;
  if (*p != '\0') return false;
  if (mo < 1 || mo > 12) return false;
  if (d < 1 || (unsigned)d > calendarDaysInMonth(y, (unsigned)mo)) return false;
  *epochOut = calendarEpochFromParts(y, mo, d, 0, 0, 0, tzOffsetHours * 3600);
  return true;
}

// Civil date from days since 1970, for building the query's RFC 3339 bounds.
static inline void calendarCivilFromDays(int64_t days, int *y, unsigned *m,
                                         unsigned *d) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = (unsigned)(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int year = (int)yoe + (int)era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = year + (*m <= 2 ? 1 : 0);
}

// `YYYY-MM-DDTHH:MM:SSZ` for an epoch. Google's `timeMin`/`timeMax` accept it
// and `calendarParseRfc3339` reads it back, which the host test checks.
static inline bool calendarRfc3339FromEpoch(int64_t epoch, char *out,
                                            size_t cap) {
  if (!out || cap < 21) return false;
  int64_t days = epoch / 86400;
  int64_t rest = epoch % 86400;
  if (rest < 0) {
    rest += 86400;
    days -= 1;
  }
  int y;
  unsigned m, d;
  calendarCivilFromDays(days, &y, &m, &d);
  const int h = (int)(rest / 3600);
  const int mi = (int)((rest % 3600) / 60);
  const int s = (int)(rest % 60);
  snprintf(out, cap, "%04d-%02u-%02uT%02d:%02d:%02dZ", y, m, d, h, mi, s);
  return true;
}

// The epoch of the local midnight of one day, in the owner's fixed offset.
// `dayOffset` 0 is today and 1 is tomorrow. A local day is exactly 86400 s here
// because the device stores an offset and not a zone: there is no DST to fix.
// The popover's "today" list and the events query both start from this number,
// so the app never derives a midnight from its own clock.
static inline bool calendarLocalMidnight(int64_t nowEpoch, int tzOffsetHours,
                                         int dayOffset, int64_t *out) {
  if (!out) return false;
  const int64_t offset = (int64_t)tzOffsetHours * 3600;
  const int64_t local = nowEpoch + offset;
  // The C remainder truncates toward zero: normalise it so instants before
  // 1970 land on their own day instead of the next one.
  int64_t intoDay = local % 86400;
  if (intoDay < 0) intoDay += 86400;
  *out = local - intoDay + (int64_t)dayOffset * 86400 - offset;
  return true;
}

// The lower bound of the events query. Normally the owner's local midnight, so
// the day's agenda is complete, but never less than an hour back: Google filters
// by start time, and an event that began before midnight and is still running
// has to stay in the answer.
static inline bool calendarDayQueryStart(int64_t nowEpoch, int tzOffsetHours,
                                         int64_t *out) {
  int64_t midnight = 0;
  if (!calendarLocalMidnight(nowEpoch, tzOffsetHours, 0, &midnight)) {
    return false;
  }
  const int64_t floor = nowEpoch - 3600;
  *out = midnight < floor ? midnight : floor;
  return true;
}

// Local wall-clock `HH:MM` for an event start, using the owner's fixed offset.
// The split frame shows the time the event says in the calendar, not a
// countdown: that is what the panel and the phone show too.
static inline bool calendarFormatClock(int64_t epoch, int tzOffsetHours,
                                       char *out, size_t cap) {
  if (!out || cap < 6) return false;
  int64_t local = epoch + (int64_t)tzOffsetHours * 3600;
  int64_t days = local / 86400;
  int64_t rest = local % 86400;
  if (rest < 0) {
    rest += 86400;
    days -= 1;
  }
  const int h = (int)(rest / 3600);
  const int mi = (int)((rest % 3600) / 60);
  snprintf(out, cap, "%02d:%02d", h, mi);
  return true;
}

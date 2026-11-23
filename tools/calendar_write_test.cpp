// Host-side test for what the device sends Google when the tray edits an event.
//
//   g++ -std=c++17 -O1 -I esp32_gadget -I <ArduinoJson>/src \
//       tools/calendar_write_test.cpp esp32_gadget/calendar_write.cpp \
//       esp32_gadget/url_codec.cpp -o /tmp/cal_write && /tmp/cal_write
//
// The owner types an event once. If these bytes are wrong the event lands on the
// wrong day, in the wrong timezone, or Google rejects it and the edit is lost:
//  1. A timed event carries `dateTime` with the *owner's* offset written out, so an
//     edit does not silently move the event to UTC.
//  2. An all-day event carries `date`, and its end is the day after the last second
//     it covers, because Google's all-day end is exclusive. The reader reads
//     exactly that back.
//  3. `location` always travels, even empty: a PATCH that leaves it out keeps the
//     old place and "borrar el lugar" would look like it did nothing.
//  4. The write paths point at the event, escape what needs escaping, and ask for
//     no email to the guests.
//  5. A change or a delete without a usable `etag` is refused instead of sent: an
//     unconditional write loses a change made elsewhere and nobody finds out.

#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "calendar_forms.h"
#include "calendar_write.h"

static int g_failures = 0;

static void checkText(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-46s\n     got  \"%s\"\n     want \"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-46s \"%s\"\n", what, got);
  }
}

static void checkInt(const char *what, long long got, long long want) {
  if (got != want) {
    printf("FAIL %-46s got=%lld want=%lld\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-46s %lld\n", what, got);
  }
}

static void render(const char *summary, const char *location, int64_t start,
                   int64_t end, bool allDay, int tz, char *out, size_t cap) {
  JsonDocument doc;
  calendarEventBodyJson(summary, location, start, end, allDay, tz, doc);
  out[0] = '\0';
  serializeJson(doc, out, cap);
}

// 2026-11-21T15:00 and 16:00 in the owner's -03:00.
static const int64_t kStart = 1795284000LL;
static const int64_t kEnd = 1795287600LL;

static void testTimedEvent() {
  printf("-- a timed event keeps the owner's offset\n");
  char out[512];
  render("Dentista", "Palermo", kStart, kEnd, false, -3, out, sizeof(out));
  checkText("the request body", out,
            "{\"summary\":\"Dentista\",\"location\":\"Palermo\","
            "\"start\":{\"dateTime\":\"2026-11-21T15:00:00-03:00\"},"
            "\"end\":{\"dateTime\":\"2026-11-21T16:00:00-03:00\"}}");

  // UTC is the same instant and has to read as UTC, not as a local time with a Z.
  render("Dentista", "", kStart, kEnd, false, 0, out, sizeof(out));
  checkText("the same instant at UTC", out,
            "{\"summary\":\"Dentista\",\"location\":\"\","
            "\"start\":{\"dateTime\":\"2026-11-21T18:00:00+00:00\"},"
            "\"end\":{\"dateTime\":\"2026-11-21T19:00:00+00:00\"}}");

  render("Reunión", "", kStart, kEnd, false, 5, out, sizeof(out));
  checkInt("a positive offset is written with +",
           strstr(out, "2026-11-21T23:00:00+05:00") != nullptr, true);
}

static void testAllDayEvent() {
  printf("-- an all-day event is a date, and its end is exclusive\n");
  // Local midnight of the 23rd and the last second of the 23rd.
  char out[512];
  render("Feriado", "", 1795402800LL, 1795489199LL, true, -3, out, sizeof(out));
  checkText("the request body", out,
            "{\"summary\":\"Feriado\",\"location\":\"\","
            "\"start\":{\"date\":\"2026-11-23\"},"
            "\"end\":{\"date\":\"2026-11-24\"}}");

  // A three-day all-day event: the 23rd, the 24th and the 25th. The reader hands
  // back the last second it covers, so the writer adds one to get the exclusive end.
  render("Viaje", "", 1795402800LL, 1795662000LL - 1, true, -3, out,
         sizeof(out));
  checkText("a multi-day one ends the day after", out,
            "{\"summary\":\"Viaje\",\"location\":\"\","
            "\"start\":{\"date\":\"2026-11-23\"},"
            "\"end\":{\"date\":\"2026-11-26\"}}");
}

static void testEscaping() {
  printf("-- the fields are JSON-escaped, never pasted raw\n");
  char out[512];
  render("Clase \"de\" piano", "Calle 1\n2", kStart, kEnd, false, -3, out,
         sizeof(out));
  checkInt("the quote is escaped",
           strstr(out, "Clase \\\"de\\\" piano") != nullptr, true);
  checkInt("the newline is escaped", strstr(out, "Calle 1\\n2") != nullptr,
           true);
}

static void testPaths() {
  printf("-- the write paths address one event and ask for no emails\n");
  char path[512];
  checkInt("create builds", calendarCreatePath("primary", path, sizeof(path)),
           true);
  checkText("create path", path,
            "/calendar/v3/calendars/primary/events?sendUpdates=none");

  checkInt("the event path builds",
           calendarEventPath("primary", "abc123", path, sizeof(path)), true);
  checkText("event path", path,
            "/calendar/v3/calendars/primary/events/abc123"
            "?sendUpdates=none&fields=id,etag");

  calendarCreatePath("a@b.com", path, sizeof(path));
  checkInt("the calendar id is escaped", strstr(path, "a%40b.com") != nullptr,
           true);

  // A Google id can carry characters that mean something else in a URL: pasted
  // raw, the request would address a different event or nothing at all.
  calendarEventPath("primary", "abc#1/2", path, sizeof(path));
  checkInt("the event id is escaped",
           strstr(path, "/events/abc%231%2F2?") != nullptr, true);

  checkInt("an empty event id is refused",
           calendarEventPath("primary", "", path, sizeof(path)), false);
  checkInt("no calendar id is refused",
           calendarCreatePath(nullptr, path, sizeof(path)), false);
}

static void testPrecondition() {
  printf("-- a write without a version is refused, not sent unconditionally\n");
  // Google's etag is a quoted tick. Empty, null or unquoted is not one, and an
  // unconditional PATCH would silently overwrite a change made on the phone.
  checkInt("no etag", calendarEtagUsable(nullptr), false);
  checkInt("empty etag", calendarEtagUsable(""), false);
  checkInt("unquoted etag", calendarEtagUsable("3201"), false);
  checkInt("a lone quote", calendarEtagUsable("\""), false);
  checkInt("a real etag", calendarEtagUsable("\"3201\""), true);
  checkInt("an etag with escapes", calendarEtagUsable("\"C67A\\3D\""), true);
}

static void testScope() {
  printf("-- the grant asks to write, not only to read\n");
  checkInt("the scope is the read-write one",
           strcmp(CALENDAR_SCOPE, "https://www.googleapis.com/auth/calendar"),
           0);
  checkInt("and it is not the read-only one",
           strstr(CALENDAR_SCOPE, "readonly") == nullptr, true);

  // The device-code form has to carry that scope, because that is the moment the
  // owner grants it; a wrong scope here is a token that reads and cannot write.
  char form[512];
  checkInt("the device form builds",
           calendarDeviceCodeForm("id.apps.googleusercontent.com", form,
                                  sizeof(form)),
           true);
  checkInt("the form carries the read-write scope",
           strstr(form, "scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcalendar") !=
                   nullptr &&
               strstr(form, "readonly") == nullptr,
           true);
}

int main() {
  testTimedEvent();
  testAllDayEvent();
  testEscaping();
  testPaths();
  testPrecondition();
  testScope();
  printf("\n%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

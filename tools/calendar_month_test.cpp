// Host-side test for the month grid: the window, the Google page and the
// /api/calendar/month payload.
//
//   g++ -std=c++17 -O1 -I esp32_gadget -I <ArduinoJson>/src \
//       tools/calendar_month_test.cpp esp32_gadget/calendar_month.cpp \
//       esp32_gadget/calendar_parse.cpp esp32_gadget/url_codec.cpp \
//       -o /tmp/cal_month && /tmp/cal_month
//
// The tray draws the whole month from this JSON and from nothing else, so the
// exact bytes are the contract with the app:
//  1. The window is decided on the device. The app never derives a Monday or a
//     midnight from its own clock, because the owner's offset lives here.
//  2. The window is whole weeks, Monday first, so the app draws rows and decides
//     nothing.
//  3. The page asks Google for `etag` and for the month's own budget of rows.
//  4. `id` is required: an event nobody can address is not editable, so it is
//     dropped instead of drawn.
//  5. `truncated` travels so a cut month never looks complete.

#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "calendar_forms.h"
#include "calendar_model.h"
#include "calendar_month.h"
#include "calendar_parse.h"
#include "calendar_time.h"

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

static void render(const CalendarMonthEvent *events, size_t count,
                   const CalendarMonthInfo &info, char *out, size_t cap) {
  JsonDocument doc;
  calendarMonthJson(events, count, info, doc);
  out[0] = '\0';
  serializeJson(doc, out, cap);
}

// Local midnights are the whole point, so the expected numbers come from an
// independent calendar (Python's datetime), not from the code under test.
static void testWindow() {
  printf("-- the window is whole weeks, Monday first, in the owner's offset\n");
  CalendarMonthWindow w;

  // June 2026 starts on a Monday: no lead, five weeks.
  checkInt("june 2026 is a valid month",
           calendarMonthWindow(2026, 6, -3, &w), true);
  checkInt("  monthStart is local midnight", w.monthStart, 1780282800LL);
  checkInt("  monthEnd is the next 1st", w.monthEnd, 1782874800LL);
  checkInt("  a Monday 1st has no lead", w.gridStart, w.monthStart);
  checkInt("  five weeks", w.weeks, 5);
  checkInt("  gridEnd is five whole weeks", w.gridEnd, 1783306800LL);

  // November 2026 starts on a Sunday: six leading days, six weeks.
  checkInt("november 2026 is a valid month",
           calendarMonthWindow(2026, 11, -3, &w), true);
  checkInt("  monthStart is local midnight", w.monthStart, 1793502000LL);
  checkInt("  monthEnd is the next 1st", w.monthEnd, 1796094000LL);
  checkInt("  a Sunday 1st leads with six days", w.gridStart, 1792983600LL);
  checkInt("  six weeks", w.weeks, 6);
  checkInt("  gridEnd is six whole weeks", w.gridEnd, 1796612400LL);
  checkInt("  the grid always spans whole weeks",
           (w.gridEnd - w.gridStart) % (7 * 86400), 0);

  // The offset is the device's, and it moves the window by exactly that much.
  CalendarMonthWindow utc;
  calendarMonthWindow(2026, 11, 0, &utc);
  checkInt("the offset moves the window", utc.monthStart - w.monthStart,
           -3 * 3600);
  checkInt("  and does not change the shape", utc.weeks, w.weeks);

  // December 2026 proves the year boundary is not special-cased wrongly.
  checkInt("december rolls into the next year",
           calendarMonthWindow(2026, 12, -3, &w), true);
  checkInt("  monthEnd is January's midnight", w.monthEnd, 1798772400LL);
  checkInt("  five weeks", w.weeks, 5);
}

static void testWindowRejects() {
  printf("-- a month that is not a month is refused, not guessed\n");
  CalendarMonthWindow w;
  checkInt("month 0", calendarMonthWindow(2026, 0, 0, &w), false);
  checkInt("month 13", calendarMonthWindow(2026, 13, 0, &w), false);
  checkInt("year before NTP", calendarMonthWindow(2019, 11, 0, &w), false);
  checkInt("year far away", calendarMonthWindow(2226, 11, 0, &w), false);
  checkInt("no output struct", calendarMonthWindow(2026, 11, 0, nullptr), false);
  // February of a leap year still has its 29 days.
  checkInt("february 2028", calendarMonthWindow(2028, 2, -3, &w), true);
  // February 2028 starts on a Tuesday, so one leading day plus 29 fits in five
  // weeks: a leap month is not automatically a six-row month.
  checkInt("  is five weeks", w.weeks, 5);
  checkInt("  spans 29 days",
           (w.monthEnd - w.monthStart) / 86400, 29);
}

static void testPath() {
  printf("-- the page asks for the month budget and for the etag\n");
  char path[512];
  const bool ok = calendarMonthPath("primary", 1792983600LL, 1796612400LL, path,
                                    sizeof(path));
  checkInt("the path builds", ok, true);
  checkText(
      "the exact Google page", path,
      "/calendar/v3/calendars/primary/events?timeMin=2026-10-26T03:00:00Z"
      "&timeMax=2026-12-07T03:00:00Z&singleEvents=true&orderBy=startTime"
      "&maxResults=200&fields=items(id,etag,status,summary,location,start,end)");

  // The day page is a frozen contract: an old client must keep receiving the same
  // bytes after this firmware update.
  checkInt("the day page still builds",
           calendarEventsPath("primary", 1795392000LL, 1795392000LL, path,
                              sizeof(path)),
           true);
  checkText(
      "the day page did not change", path,
      "/calendar/v3/calendars/primary/events?timeMin=2026-11-23T00:00:00Z"
      "&timeMax=2026-11-23T00:00:00Z&singleEvents=true&orderBy=startTime"
      "&maxResults=24&fields=items(id,status,summary,location,start,end,reminders)");

  // A calendar id with a character that has to be escaped never lands raw in the
  // URL, or Google answers with a different resource.
  calendarMonthPath("a@b.com", 1792983600LL, 1796612400LL, path, sizeof(path));
  checkInt("the id is escaped", strstr(path, "a%40b.com") != nullptr, true);
}

static void testEmptyMonth() {
  printf("-- an empty month still answers the shape the app draws\n");
  CalendarMonthInfo info{};
  info.year = 2026;
  info.month = 11;
  info.tzOffsetHours = -3;
  info.monthStart = 1793502000LL;
  info.monthEnd = 1796094000LL;
  info.gridStart = 1792983600LL;
  info.gridEnd = 1796612400LL;
  info.truncated = false;

  char out[512];
  render(nullptr, 0, info, out, sizeof(out));
  checkText("the empty payload", out,
            "{\"ok\":true,\"tz\":-3,\"year\":2026,\"month\":11,"
            "\"monthStart\":1793502000,\"monthEnd\":1796094000,"
            "\"gridStart\":1792983600,\"gridEnd\":1796612400,"
            "\"count\":0,\"truncated\":false,\"events\":[]}");
}

static void testEventsPayload() {
  printf("-- every row carries what an edit needs\n");
  CalendarMonthInfo info{};
  info.year = 2026;
  info.month = 11;
  info.tzOffsetHours = -3;
  info.monthStart = 1793502000LL;
  info.monthEnd = 1796094000LL;
  info.gridStart = 1792983600LL;
  info.gridEnd = 1796612400LL;
  info.truncated = false;

  CalendarMonthEvent events[2];
  memset(events, 0, sizeof(events));
  snprintf(events[0].id, sizeof(events[0].id), "abc123");
  snprintf(events[0].etag, sizeof(events[0].etag), "\"3201\"");
  snprintf(events[0].summary, sizeof(events[0].summary), "Dentista");
  snprintf(events[0].location, sizeof(events[0].location), "Palermo");
  events[0].startEpoch = 1795284000LL;
  events[0].endEpoch = 1795287600LL;
  events[0].allDay = false;

  snprintf(events[1].id, sizeof(events[1].id), "def456");
  snprintf(events[1].summary, sizeof(events[1].summary), "Feriado");
  events[1].startEpoch = 1795402800LL;
  events[1].endEpoch = 1795489199LL;
  events[1].allDay = true;

  char out[768];
  render(events, 2, info, out, sizeof(out));
  checkText(
      "two rows, etag and all-day included", out,
      "{\"ok\":true,\"tz\":-3,\"year\":2026,\"month\":11,"
      "\"monthStart\":1793502000,\"monthEnd\":1796094000,"
      "\"gridStart\":1792983600,\"gridEnd\":1796612400,\"count\":2,"
      "\"truncated\":false,\"events\":["
      "{\"id\":\"abc123\",\"etag\":\"\\\"3201\\\"\",\"summary\":\"Dentista\","
      "\"location\":\"Palermo\",\"startEpoch\":1795284000,"
      "\"endEpoch\":1795287600,\"allDay\":false},"
      "{\"id\":\"def456\",\"etag\":\"\",\"summary\":\"Feriado\","
      "\"location\":\"\",\"startEpoch\":1795402800,\"endEpoch\":1795489199,"
      "\"allDay\":true}]}");

  info.truncated = true;
  render(nullptr, 0, info, out, sizeof(out));
  checkInt("a cut month says so", strstr(out, "\"truncated\":true") != nullptr,
           true);
}

static void testParse() {
  printf("-- the parse drops what cannot be edited or read\n");
  const char *json =
      "{\"items\":["
      "{\"id\":\"uno\",\"etag\":\"\\\"7\\\"\",\"status\":\"confirmed\","
      "\"summary\":\"Reunion\",\"location\":\"Sala\","
      "\"start\":{\"dateTime\":\"2026-11-21T15:00:00-03:00\"},"
      "\"end\":{\"dateTime\":\"2026-11-21T16:00:00-03:00\"}},"
      "{\"id\":\"dos\",\"status\":\"confirmed\",\"summary\":\"Feriado\","
      "\"start\":{\"date\":\"2026-11-23\"},\"end\":{\"date\":\"2026-11-24\"}},"
      "{\"status\":\"confirmed\",\"summary\":\"Sin id\","
      "\"start\":{\"dateTime\":\"2026-11-21T09:00:00-03:00\"}},"
      "{\"id\":\"tres\",\"status\":\"cancelled\",\"summary\":\"Cancelado\","
      "\"start\":{\"dateTime\":\"2026-11-21T09:00:00-03:00\"}},"
      "{\"id\":\"cuatro\",\"status\":\"confirmed\",\"summary\":\"Roto\","
      "\"start\":{\"dateTime\":\"no es una fecha\"}}"
      "]}";

  CalendarMonthEvent events[CALENDAR_MONTH_MAX_EVENTS];
  const size_t count =
      calendarParseMonthEvents(json, events, CALENDAR_MONTH_MAX_EVENTS, -3,
                               nullptr);
  checkInt("two rows survive", count, 2);
  checkText("first id", events[0].id, "uno");
  checkText("first etag", events[0].etag, "\"7\"");
  checkText("first summary", events[0].summary, "Reunion");
  checkText("first location", events[0].location, "Sala");
  // 2026-11-21T15:00:00-03:00 is 18:00 UTC.
  checkInt("first start is UTC", events[0].startEpoch, 1795284000LL);
  checkInt("first end is UTC", events[0].endEpoch, 1795287600LL);
  checkInt("first is not all-day", events[0].allDay, false);
  checkText("the all-day row has no location", events[1].location, "");
  checkInt("the all-day row is flagged", events[1].allDay, true);
  // 2026-11-23T00:00:00-03:00 is 03:00 UTC; the end is exclusive in Google, so
  // the last second of the event is one before the 24th.
  checkInt("all-day start", events[1].startEpoch, 1795402800LL);
  checkInt("all-day end is the last second", events[1].endEpoch, 1795489199LL);
  checkText("an event with no etag still parses", events[1].etag, "");

  // A body that is not JSON is not a month, and never a half month.
  checkInt("garbage", calendarParseMonthEvents("nope", events, 4, 0, nullptr), 0);
  checkInt("empty", calendarParseMonthEvents("{}", events, 4, 0, nullptr), 0);
  checkInt("no room", calendarParseMonthEvents(json, events, 1, -3, nullptr), 1);
}

int main() {
  testWindow();
  testWindowRejects();
  testPath();
  testEmptyMonth();
  testEventsPayload();
  testParse();
  printf("\n%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

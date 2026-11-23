// Host-side test for the /api/calendar/events payload.
//
//   g++ -std=c++17 -O1 -I esp32_gadget -I <ArduinoJson>/src \
//       tools/calendar_events_test.cpp esp32_gadget/calendar_events.cpp \
//       -o /tmp/cal_events && /tmp/cal_events
//
// The tray draws the day's agenda from this JSON and nothing else, so the exact
// bytes are the contract with the app:
//  1. An empty list still answers `ok`, `tz`, `count` and an empty `events`
//     array, because the panel needs the shape before it needs the data.
//  2. The events keep the order the device stored them in, all six fields
//     included, with the times as UTC epoch numbers.
//  3. An all-day event is a flag, not a formatted hour: the app decides how to
//     label it in the owner's timezone.
//  4. `tz` travels untouched, so a Sunday-night change is the device's answer
//     and not a second call.

#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "calendar_events.h"
#include "calendar_model.h"

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

static void fillEvent(CalendarEvent *event, const char *summary,
                      const char *location, long long start, long long end,
                      int leadMin, bool allDay) {
  memset(event, 0, sizeof(*event));
  snprintf(event->summary, sizeof(event->summary), "%s", summary);
  snprintf(event->location, sizeof(event->location), "%s", location);
  event->startEpoch = start;
  event->endEpoch = end;
  event->leadMin = leadMin;
  event->allDay = allDay;
}

// The JSON the app parses, rendered from the document the firmware would send.
// The board's clock during the survey: 2026-11-23 09:48:52 at -03:00.
static const long long kNow = 1795438132LL;
static void render(const CalendarEvent *events, size_t count, int tz,
                   char *out, size_t cap) {
  JsonDocument doc;
  calendarEventsJson(events, count, tz, kNow, doc);
  const size_t written = serializeJson(doc, out, cap);
  if (written < cap) out[written] = '\0';
}

static void testEmpty() {
  printf("-- an empty agenda still answers the shape\n");
  char out[512];
  render(nullptr, 0, -3, out, sizeof(out));
  checkText("empty list", out,
            "{\"ok\":true,\"tz\":-3,\"dayStart\":1795402800,"
            "\"dayEnd\":1795489200,\"count\":0,\"events\":[]}");
}

static void testTwoEvents() {
  printf("-- two events keep their order and every field\n");
  CalendarEvent events[2];
  fillEvent(&events[0], "Reunion", "Zoom", 1764000000LL, 1764003600LL, 10,
            false);
  fillEvent(&events[1], "Dentista", "", 1764020000LL, 1764023600LL, -1, false);
  char out[1024];
  render(events, 2, -3, out, sizeof(out));
  checkText(
      "two timed events", out,
      "{\"ok\":true,\"tz\":-3,\"dayStart\":1795402800,"
      "\"dayEnd\":1795489200,\"count\":2,\"events\":["
      "{\"summary\":\"Reunion\",\"location\":\"Zoom\","
      "\"startEpoch\":1764000000,\"endEpoch\":1764003600,"
      "\"leadMin\":10,\"allDay\":false},"
      "{\"summary\":\"Dentista\",\"location\":\"\","
      "\"startEpoch\":1764020000,\"endEpoch\":1764023600,"
      "\"leadMin\":-1,\"allDay\":false}]}");

  // The times are numbers, not strings: the app does the arithmetic.
  JsonDocument doc;
  calendarEventsJson(events, 2, -3, kNow, doc);
  checkInt("count is a number", doc["count"].as<long long>(), 2);
  checkInt("startEpoch stays a number",
           doc["events"][0]["startEpoch"].as<long long>(), 1764000000LL);
  checkInt("an empty location is a string, never null",
           strcmp(doc["events"][1]["location"].as<const char *>(), ""), 0);
}

static void testAllDay() {
  printf("-- an all-day event is a flag, not a formatted hour\n");
  CalendarEvent event;
  // 2026-11-22 00:00 at -03:00, the local midnight the parse stored.
  fillEvent(&event, "Feriado", "", 1795316400LL, 1795402800LL, -1, true);
  char out[512];
  render(&event, 1, -3, out, sizeof(out));
  checkText(
      "all-day event", out,
      "{\"ok\":true,\"tz\":-3,\"dayStart\":1795402800,"
      "\"dayEnd\":1795489200,\"count\":1,\"events\":["
      "{\"summary\":\"Feriado\",\"location\":\"\","
      "\"startEpoch\":1795316400,\"endEpoch\":1795402800,"
      "\"leadMin\":-1,\"allDay\":true}]}");
}

static void testDayBounds() {
  printf("-- the app never derives the day from its own clock\n");
  char out[512];
  // A positive offset moves the local day back in UTC.
  render(nullptr, 0, 5, out, sizeof(out));
  checkText("at +05", out,
            "{\"ok\":true,\"tz\":5,\"dayStart\":1795374000,"
            "\"dayEnd\":1795460400,\"count\":0,\"events\":[]}");
  render(nullptr, 0, 0, out, sizeof(out));
  checkText("at UTC", out,
            "{\"ok\":true,\"tz\":0,\"dayStart\":1795392000,"
            "\"dayEnd\":1795478400,\"count\":0,\"events\":[]}");

  // Without NTP there is no honest day: zeros are the signal that the app has to
  // fall back to `tz`, instead of a 1970 midnight that would look real.
  JsonDocument doc;
  calendarEventsJson(nullptr, 0, -3, 0, doc);
  checkInt("an unsynced clock sends 0", doc["dayStart"].as<long long>(), 0);
  checkInt("  for both bounds", doc["dayEnd"].as<long long>(), 0);
}

static void testTz() {
  printf("-- tz travels exactly as the device reports it\n");
  char out[512];
  render(nullptr, 0, 5, out, sizeof(out));
  checkInt("positive offset", strstr(out, "\"tz\":5") != nullptr, true);
  render(nullptr, 0, 0, out, sizeof(out));
  checkInt("UTC", strstr(out, "\"tz\":0") != nullptr, true);
}

int main() {
  testEmpty();
  testTwoEvents();
  testAllDay();
  testDayBounds();
  testTz();
  printf("\n%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

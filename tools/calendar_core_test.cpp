// Host-side test for the local Google Calendar rules.
//
//   g++ -std=c++17 -O1 -I esp32_gadget -I <ArduinoJson>/src \
//       tools/calendar_core_test.cpp esp32_gadget/calendar_parse.cpp \
//       -o /tmp/cal && /tmp/cal
//
// Nothing here talks to Google. Three things are pinned:
//  1. An RFC 3339 timestamp becomes an epoch, offset included, because the
//     reaction window is arithmetic and a two-hour error is an early face.
//  2. The reminder that sets the window comes from the event, then from the
//     calendar's defaults, and an event with no reminder never reacts.
//  3. Choosing the event and its phase: the nearest timed event wins, an
//     all-day event only when nothing timed is close, and intensity grows with
//     the time left in the lead window.

#include <cstdio>
#include <cstring>

#include "calendar_auth_parse.h"
#include "calendar_forms.h"
#include "calendar_model.h"
#include "calendar_parse.h"
#include "calendar_plan.h"
#include "calendar_time.h"

static int g_failures = 0;

static void checkInt(const char *what, long long got, long long want) {
  if (got != want) {
    printf("FAIL %-58s got=%lld want=%lld\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-58s %lld\n", what, got);
  }
}

static void checkText(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %-58s got=\"%s\" want=\"%s\"\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-58s \"%s\"\n", what, got);
  }
}

// 2026-11-21T15:00:00-03:00 == 2026-11-21T18:00:00Z
static const int64_t kDentistStart = 1795284000;
static const int64_t kDentistEnd = 1795287600;  // 16:00-03:00
// 2026-11-22 00:00 at -03:00, the all-day event's local midnight.
static const int64_t kHolidayStart = 1795316400;

static void testRfc3339() {
  printf("-- RFC 3339 to epoch, offset included\n");
  int64_t epoch = 0;
  checkInt("offset -03:00", calendarParseRfc3339("2026-11-21T15:00:00-03:00", &epoch),
           true);
  checkInt("  the value equals the same instant in Z", epoch, kDentistStart);
  checkInt("Z suffix", calendarParseRfc3339("2026-11-21T18:00:00Z", &epoch), true);
  checkInt("  same instant", epoch, kDentistStart);
  checkInt("fractional seconds are ignored",
           calendarParseRfc3339("2026-11-21T18:00:00.250Z", &epoch), true);
  checkInt("  same instant", epoch, kDentistStart);
  checkInt("positive offset", calendarParseRfc3339("2026-11-20T03:00:00+05:30", &epoch),
           true);
  checkInt("  the value", epoch, 1795123800);
  checkInt("a date without time is not a timestamp",
           calendarParseRfc3339("2026-11-21", &epoch), false);
  checkInt("garbage", calendarParseRfc3339("nope", &epoch), false);
  checkInt("an empty string", calendarParseRfc3339("", &epoch), false);
  checkInt("a null pointer", calendarParseRfc3339(nullptr, &epoch), false);

  printf("-- a date-only value becomes local midnight\n");
  checkInt("date in the device's offset",
           calendarParseDate("2026-11-22", -3, &epoch), true);
  checkInt("  the value", epoch, kHolidayStart);
  checkInt("a leap day", calendarParseDate("2028-02-29", 0, &epoch), true);
  checkInt("  the value", epoch, 1835395200);
  checkInt("a bad month", calendarParseDate("2026-13-01", 0, &epoch), false);
}

static const char *kCalendarList = R"({
  "kind": "calendar#calendarList",
  "items": [
    {"id": "primary", "summary": "maxi@example.com", "primary": true,
     "defaultReminders": [{"method": "popup", "minutes": 10}]},
    {"id": "abc@group.calendar.google.com", "summary": "Trabajo",
     "defaultReminders": []},
    {"id": "team@group.calendar.google.com", "summary": "Equipo"}
  ]})";

static void testCalendarList() {
  printf("-- the selector's list\n");
  CalendarInfo list[CALENDAR_MAX_LIST];
  size_t n = calendarParseCalendarList(kCalendarList, list, CALENDAR_MAX_LIST);
  checkInt("three calendars", n, 3);
  checkText("first id", list[0].id, "primary");
  checkText("first name", list[0].name, "maxi@example.com");
  checkInt("first is primary", list[0].primary, true);
  checkInt("first default lead", list[0].defaultLeadMin, 10);
  checkText("second id", list[1].id, "abc@group.calendar.google.com");
  checkInt("second is not primary", list[1].primary, false);
  checkInt("no defaults declared", list[1].defaultLeadMin, -1);
  checkInt("a calendar without defaultReminders falls back", list[2].defaultLeadMin,
           -1);

  CalendarInfo one[1];
  checkInt("the capacity is honoured", calendarParseCalendarList(kCalendarList, one, 1),
           1);
  checkInt("a body that is not the list", calendarParseCalendarList("{}", list, 3), 0);
  checkInt("a garbage body", calendarParseCalendarList("nope", list, 3), 0);
}

static const char *kEvents = R"({
  "items": [
    {"summary": "Dentista", "status": "confirmed",
     "location": "Av. Belgrano 245",
     "start": {"dateTime": "2026-11-21T15:00:00-03:00"},
     "end": {"dateTime": "2026-11-21T16:00:00-03:00"},
     "reminders": {"useDefault": false,
       "overrides": [{"method": "popup", "minutes": 30},
                     {"method": "email", "minutes": 120}]}},
    {"summary": "Cancelado", "status": "cancelled",
     "start": {"dateTime": "2026-11-21T15:30:00-03:00"},
     "end": {"dateTime": "2026-11-21T16:30:00-03:00"},
     "reminders": {"useDefault": true}},
    {"summary": "Cumple de Ana",
     "start": {"date": "2026-11-22"}, "end": {"date": "2026-11-23"},
     "reminders": {"useDefault": true}},
    {"summary": "Sin aviso",
     "start": {"dateTime": "2026-11-21T20:00:00-03:00"},
     "end": {"dateTime": "2026-11-21T21:00:00-03:00"},
     "reminders": {"useDefault": false, "overrides": []}}
  ]})";

static void testEvents() {
  printf("-- reminders: the event's own, then the calendar's default\n");
  CalendarEvent events[CALENDAR_MAX_EVENTS];
  size_t n = calendarParseEvents(kEvents, events, CALENDAR_MAX_EVENTS, 10, -3);
  checkInt("cancelled events are dropped", n, 3);
  checkText("first summary", events[0].summary, "Dentista");
  checkInt("first start", events[0].startEpoch, kDentistStart);
  checkInt("first end", events[0].endEpoch, kDentistEnd);
  checkInt("the nearest override wins", events[0].leadMin, 30);
  checkText("the location travels with the event", events[0].location,
            "Av. Belgrano 245");
  checkInt("it is not all day", events[0].allDay, false);
  checkText("all-day summary", events[1].summary, "Cumple de Ana");
  checkInt("all-day start is local midnight", events[1].startEpoch, kHolidayStart);
  checkInt("useDefault takes the calendar's default", events[1].leadMin, 10);
  checkInt("all-day is marked", events[1].allDay, true);
  checkText("no reminder summary", events[2].summary, "Sin aviso");
  checkInt("an explicit empty reminder list never reacts", events[2].leadMin, -1);

  printf("-- a calendar with no default and no override gets the fallback\n");
  n = calendarParseEvents(kEvents, events, CALENDAR_MAX_EVENTS, 7, -3);
  checkInt("the selected calendar's default is what the caller passes",
           events[1].leadMin, 7);
  n = calendarParseEvents(kEvents, events, CALENDAR_MAX_EVENTS, -1, -3);
  checkInt("no default anywhere keeps the event silent", events[1].leadMin, -1);
  (void)n;

  printf("-- a very long reminder is capped so the face is not alert for days\n");
  const char *far = R"({"items":[{"summary":"Viaje",
    "start":{"dateTime":"2026-12-24T10:00:00-03:00"},
    "end":{"dateTime":"2026-12-24T11:00:00-03:00"},
    "reminders":{"useDefault":false,
      "overrides":[{"method":"popup","minutes":40320}]}}]})";
  n = calendarParseEvents(far, events, CALENDAR_MAX_EVENTS, 10, -3);
  checkInt("one event", n, 1);
  checkInt("four weeks become the cap", events[0].leadMin, CALENDAR_MAX_LEAD_MIN);
}

static CalendarEvent makeEvent(const char *summary, int64_t start, int64_t end,
                               int leadMin, bool allDay = false) {
  CalendarEvent e;
  memset(&e, 0, sizeof(e));
  snprintf(e.summary, sizeof(e.summary), "%s", summary);
  e.startEpoch = start;
  e.endEpoch = end;
  e.leadMin = leadMin;
  e.allDay = allDay;
  return e;
}

static void testReaction() {
  printf("-- picking the next event and its phase\n");
  CalendarEvent events[3];
  events[0] = makeEvent("Sin aviso", kDentistStart + 600, kDentistStart + 1200, -1);
  events[1] = makeEvent("Dentista", kDentistStart, kDentistEnd, 30);
  snprintf(events[1].location, sizeof(events[1].location), "Av. Belgrano 245");
  events[2] = makeEvent("Otro", kDentistStart + 7200, kDentistStart + 10800, 60);

  CalendarReaction r;
  // 40 minutes before the dentist: the 30-minute reminder has not started yet.
  calendarPickReaction(events, 3, kDentistStart - 40 * 60, 5, &r);
  checkInt("too early for a 30-minute reminder", r.active, false);
  checkText("phase none", calendarPhaseName(r.phase), "none");

  // 20 minutes before: inside the window, 1/3 of it consumed.
  calendarPickReaction(events, 3, kDentistStart - 20 * 60, 5, &r);
  checkInt("inside the lead window", r.active, true);
  checkText("the dentist wins", r.summary, "Dentista");
  checkInt("  the start epoch comes along", r.startEpoch, kDentistStart);
  checkText("  and the location", r.location, "Av. Belgrano 245");
  checkInt("  ms to start", r.msToStart, 20 * 60 * 1000);
  checkInt("  intensity is a third", r.intensity, 33);
  checkText("  early phase", calendarPhaseName(r.phase), "early");

  // 8 minutes before: 22/30 consumed, past the half.
  calendarPickReaction(events, 3, kDentistStart - 8 * 60, 5, &r);
  checkInt("near intensity", r.intensity, 73);
  checkText("near phase", calendarPhaseName(r.phase), "near");

  // 2 minutes before: the last stretch.
  calendarPickReaction(events, 3, kDentistStart - 2 * 60, 5, &r);
  checkInt("starting intensity", r.intensity, 93);
  checkText("starting phase", calendarPhaseName(r.phase), "starting");

  // The event has started, within grace.
  calendarPickReaction(events, 3, kDentistStart + 60, 5, &r);
  checkInt("ongoing", r.active, true);
  checkInt("  negative ms to start", r.msToStart, -60 * 1000);
  checkInt("  full intensity", r.intensity, 100);
  checkText("  ongoing phase", calendarPhaseName(r.phase), "ongoing");

  // Past the grace window the face goes back to normal.
  calendarPickReaction(events, 3, kDentistStart + 6 * 60, 5, &r);
  checkInt("the grace window ends", r.active, false);

  // The event that starts in two hours but has a 60-minute reminder is still
  // outside its window, so the earlier final phase wins.
  calendarPickReaction(events, 3, kDentistStart + 60 * 30, 5, &r);
  checkInt("the next event is not close yet", r.active, false);
}

static void testAllDayAndPriority() {
  printf("-- all-day events are the fallback, not the headline\n");
  CalendarEvent events[2];
  events[0] = makeEvent("Dentista", kDentistStart, kDentistEnd, 30);
  events[1] = makeEvent("Feriado", kHolidayStart, kHolidayStart + 86400, 10, true);

  CalendarReaction r;
  // 20 minutes before the dentist; the all-day event is also inside its window
  // because its reminder starts the previous day at 23:50.
  calendarPickReaction(events, 2, kDentistStart - 20 * 60, 5, &r);
  checkText("the timed event is preferred", r.summary, "Dentista");
  checkInt("  it has its own reminder", r.leadMin, 30);

  // With nothing timed close, the all-day event reacts.
  CalendarEvent allDay[1];
  allDay[0] = events[1];
  calendarPickReaction(allDay, 1, kHolidayStart - 5 * 60, 5, &r);
  checkInt("all-day reacts on its own", r.active, true);
  checkText("  with its summary", r.summary, "Feriado");
  checkInt("  and its reminder", r.leadMin, 10);

  printf("-- a reminder at the exact start still has a face\n");
  CalendarEvent zero[1];
  zero[0] = makeEvent("Ahora", kDentistStart, kDentistEnd, 0);
  calendarPickReaction(zero, 1, kDentistStart - 30, 5, &r);
  checkInt("a zero-minute reminder is inside its window only at the start",
           r.active, false);
  calendarPickReaction(zero, 1, kDentistStart, 5, &r);
  checkInt("at the start it is active", r.active, true);
  checkInt("  and full", r.intensity, 100);
}

// The encoder refusing a buffer that does not fit is only half the contract: the
// builders have to propagate that refusal instead of sending a request with a
// truncated id. `url_codec_test` pins the encoder; this pins the call sites.
static void testFormCapacityAborts() {
  printf("-- a form whose encoder cannot fit is refused, not truncated\n");
  char out[1024];
  char big[CALENDAR_ID_MAX * 3 + 120];
  memset(big, 'a', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  checkInt("oversized calendar id aborts the device-code form",
           calendarDeviceCodeForm(big, out, sizeof(out)), false);
  checkInt("oversized calendar id aborts the events path",
           calendarEventsPath(big, 1700000000, 1700086400, out, sizeof(out)),
           false);
  char tiny[8];
  checkInt("a small output buffer still aborts",
           calendarDeviceCodeForm("id", tiny, sizeof(tiny)), false);
}

static void testForms() {
  printf("-- the OAuth and API requests, built without a network\n");
  char buf[1024];
  checkInt("device code request",
           calendarDeviceCodeForm("1234-abc.apps.googleusercontent.com", buf,
                                  sizeof(buf)),
           true);
  checkText("  the form", buf,
            "client_id=1234-abc.apps.googleusercontent.com&scope=https%3A%2F%2Fwww"
            ".googleapis.com%2Fauth%2Fcalendar.readonly");

  checkInt("device token exchange",
           calendarTokenFormDevice("id", "secret", "dev/1+code", buf, sizeof(buf)),
           true);
  checkText("  the form", buf,
            "client_id=id&client_secret=secret&device_code=dev%2F1%2Bcode&grant_"
            "type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code");

  checkInt("refresh exchange",
           calendarTokenFormRefresh("id", "secret", "1//token", buf, sizeof(buf)),
           true);
  checkText("  the form", buf,
            "client_id=id&client_secret=secret&refresh_token=1%2F%2Ftoken&grant_"
            "type=refresh_token");

  printf("-- the events query\n");
  char path[512];
  checkInt("a path with an encoded calendar id",
           calendarEventsPath("abc@group.calendar.google.com", 1795200000,
                              1795372800, path, sizeof(path)),
           true);
  if (!strstr(path, "/calendar/v3/calendars/abc%40group.calendar.google.com/events?")) {
    printf("FAIL events path calendar segment: %s\n", path);
    g_failures++;
  }
  const char *wanted[] = {"singleEvents=true", "orderBy=startTime",
                          "maxResults=12",
                          "fields=items(id,status,summary,location,start,end,reminders)",
                          "timeMin=", "timeMax="};
  for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
    if (!strstr(path, wanted[i])) {
      printf("FAIL events path is missing %s: %s\n", wanted[i], path);
      g_failures++;
    } else {
      printf("ok   events path contains %s\n", wanted[i]);
    }
  }

  printf("-- the event's own clock, in the owner's offset\n");
  checkInt("15:00 at -03", calendarFormatClock(kDentistStart, -3, buf, sizeof(buf)),
           true);
  checkText("  the value", buf, "15:00");
  calendarFormatClock(kDentistStart, 0, buf, sizeof(buf));
  checkText("the same instant in UTC", buf, "18:00");

  printf("-- the title wraps to two lines\n");
  char l1[32], l2[32];
  checkInt("one line", calendarWrapTwoLines("Dentista", 10, l1, sizeof(l1), l2,
                                            sizeof(l2)), 1);
  checkText("  the line", l1, "Dentista");
  checkText("  no second line", l2, "");
  checkInt("two lines", calendarWrapTwoLines("Turno con la Dra. Paz", 10, l1,
                                             sizeof(l1), l2, sizeof(l2)), 2);
  checkText("  the first", l1, "Turno con");
  checkText("  the second", l2, "la Dra. Pa");
  checkInt("a word wider than the line is cut", calendarWrapTwoLines(
               "Supercalifragilistico", 10, l1, sizeof(l1), l2, sizeof(l2)), 2);
  checkText("  the cut", l1, "Supercalif");
  checkText("  the rest on the second line", l2, "ragilistic");
  checkInt("empty text", calendarWrapTwoLines("", 10, l1, sizeof(l1), l2,
                                              sizeof(l2)), 0);

  printf("-- an RFC 3339 builder for the query window\n");
  checkInt("the dentist's instant",
           calendarRfc3339FromEpoch(kDentistStart, buf, sizeof(buf)), true);
  checkText("  in UTC", buf, "2026-11-21T18:00:00Z");
  int64_t roundTrip = 0;
  calendarParseRfc3339(buf, &roundTrip);
  checkInt("  and back", roundTrip, kDentistStart);
}

static void testOAuthResponses() {
  printf("-- Google's OAuth answers, classified without a network\n");
  JsonDocument doc;
  CalendarDeviceCode code;

  deserializeJson(doc, R"({"device_code":"dev-1","user_code":"ABCD-EFGH",
    "verification_url":"https://www.google.com/device","expires_in":1800,
    "interval":5})");
  calendarParseDeviceCodeResponse(doc.as<JsonVariantConst>(), &code);
  checkText("device code", code.deviceCode, "dev-1");
  checkText("user code", code.userCode, "ABCD-EFGH");
  checkText("verification url", code.verificationUrl,
            "https://www.google.com/device");
  checkInt("interval", code.intervalSec, 5);
  checkInt("expiry", code.expiresInSec, 1800);

  doc.clear();
  deserializeJson(doc, R"({"device_code":"dev","user_code":"X",
    "interval":0,"expires_in":10})");
  calendarParseDeviceCodeResponse(doc.as<JsonVariantConst>(), &code);
  checkInt("a zero interval becomes one second", code.intervalSec, 1);
  checkInt("a short expiry is clamped up", code.expiresInSec, 60);
  checkText("a missing url falls back to google.com/device",
            code.verificationUrl, "https://www.google.com/device");

  doc.clear();
  deserializeJson(doc, R"({"user_code":"X","interval":300,"expires_in":99999})");
  calendarParseDeviceCodeResponse(doc.as<JsonVariantConst>(), &code);
  checkText("no device code means no flow", code.deviceCode, "");
  checkInt("a long interval is clamped", code.intervalSec, 60);
  checkInt("a long expiry is clamped", code.expiresInSec, 3600);

  CalendarOAuthResponse r;
  doc.clear();
  deserializeJson(doc, R"({"access_token":"ya29.token","expires_in":3599,
    "refresh_token":"1//refresh"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 200, &r);
  checkInt("success is authorized", r.outcome == CALENDAR_OAUTH_AUTHORIZED, true);
  checkText("  access token", r.accessToken, "ya29.token");
  checkText("  refresh token", r.refreshToken, "1//refresh");
  checkInt("  lifetime", r.expiresInSec, 3599);
  checkText("  no error sentence", r.error, "");

  doc.clear();
  deserializeJson(doc, R"({"access_token":"ya29.token"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 200, &r);
  checkInt("a refresh exchange may omit the refresh token",
           r.outcome == CALENDAR_OAUTH_AUTHORIZED, true);
  checkInt("  the default lifetime applies", r.expiresInSec, 3600);
  checkText("  refresh stays empty", r.refreshToken, "");

  doc.clear();
  deserializeJson(doc, R"({"expires_in":3599})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 200, &r);
  checkInt("a 200 without an access token is an error",
           r.outcome == CALENDAR_OAUTH_ERROR, true);
  checkText("  and says so", r.error, "Google no devolvio el access token");

  doc.clear();
  deserializeJson(doc, R"({"error":"authorization_pending"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 400, &r);
  checkInt("pending keeps polling", r.outcome == CALENDAR_OAUTH_PENDING, true);
  checkText("  with no error for the panel", r.error, "");

  doc.clear();
  deserializeJson(doc, R"({"error":"slow_down"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 400, &r);
  checkInt("slow_down keeps polling slower",
           r.outcome == CALENDAR_OAUTH_SLOW_DOWN, true);

  doc.clear();
  deserializeJson(doc, R"({"error":"access_denied"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 403, &r);
  checkInt("a denial is terminal", r.outcome == CALENDAR_OAUTH_DENIED, true);
  checkText("  the sentence", r.error, "Cancelaste el permiso en Google");

  doc.clear();
  deserializeJson(doc, R"({"error":"expired_token"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 400, &r);
  checkInt("an expired code is terminal", r.outcome == CALENDAR_OAUTH_EXPIRED, true);
  checkText("  the sentence", r.error, "El codigo vencio, pedi uno nuevo");

  doc.clear();
  deserializeJson(doc, R"({"error":"invalid_grant"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 400, &r);
  checkInt("a dead refresh token is its own outcome",
           r.outcome == CALENDAR_OAUTH_INVALID_GRANT, true);
  checkText("  the sentence", r.error,
            "El permiso vencio o lo revocaste; conecta Google de nuevo");

  doc.clear();
  deserializeJson(doc, R"({"error":"invalid_client",
    "error_description":"The OAuth client was not found."})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 401, &r);
  checkInt("an unknown error stays an error",
           r.outcome == CALENDAR_OAUTH_ERROR, true);
  checkText("  Google's description is shown", r.error,
            "The OAuth client was not found.");

  doc.clear();
  deserializeJson(doc, R"({"error":"invalid_client"})");
  calendarParseTokenResponse(true, doc.as<JsonVariantConst>(), 401, &r);
  checkText("  without a description, the fallback", r.error,
            "Google rechazo la autorizacion");

  calendarParseTokenResponse(false, doc.as<JsonVariantConst>(), 502, &r);
  checkInt("a non-JSON body is unreadable",
           r.outcome == CALENDAR_OAUTH_UNREADABLE, true);
  checkText("  with an actionable sentence", r.error,
            "Google rechazo la autorizacion");
}

int main() {
  testRfc3339();
  testCalendarList();
  testEvents();
  testReaction();
  testAllDayAndPriority();
  testFormCapacityAborts();
  testForms();
  testOAuthResponses();
  printf("\n%s\n", g_failures ? "FAILED" : "all calendar checks passed");
  return g_failures ? 1 : 0;
}

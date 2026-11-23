#include "calendar_events.h"

#include "calendar_time.h"

namespace {

// Before 2020 the board has not seen NTP, so `time()` is the RTC's default. A day
// computed from that would be a real-looking lie; zeros tell the app to use `tz`.
constexpr int64_t kPlausibleClock = 1600000000LL;

}  // namespace

void calendarEventsJson(const CalendarEvent *events, size_t count,
                        int tzOffsetHours, int64_t nowEpoch, JsonDocument &doc) {
  int64_t dayStart = 0;
  int64_t dayEnd = 0;
  if (nowEpoch >= kPlausibleClock) {
    calendarLocalMidnight(nowEpoch, tzOffsetHours, 0, &dayStart);
    calendarLocalMidnight(nowEpoch, tzOffsetHours, 1, &dayEnd);
  }
  doc["ok"] = true;
  doc["tz"] = tzOffsetHours;
  doc["dayStart"] = dayStart;
  doc["dayEnd"] = dayEnd;
  doc["count"] = count;
  JsonArray list = doc["events"].to<JsonArray>();
  for (size_t i = 0; events && i < count; i++) {
    const CalendarEvent &event = events[i];
    JsonObject item = list.add<JsonObject>();
    // The arrays are never null, so an event without a place sends "" and the
    // app never has to tell "no location" from a missing field.
    item["summary"] = event.summary;
    item["location"] = event.location;
    item["startEpoch"] = event.startEpoch;
    item["endEpoch"] = event.endEpoch;
    item["leadMin"] = event.leadMin;
    item["allDay"] = event.allDay;
  }
}

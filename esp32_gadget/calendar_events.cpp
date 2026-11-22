#include "calendar_events.h"

void calendarEventsJson(const CalendarEvent *events, size_t count,
                        int tzOffsetHours, JsonDocument &doc) {
  doc["ok"] = true;
  doc["tz"] = tzOffsetHours;
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

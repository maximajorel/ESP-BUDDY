#include "calendar_month.h"

void calendarMonthJson(const CalendarMonthEvent *events, size_t count,
                       const CalendarMonthInfo &info, JsonDocument &doc) {
  doc["ok"] = true;
  doc["tz"] = info.tzOffsetHours;
  doc["year"] = info.year;
  doc["month"] = info.month;
  doc["monthStart"] = info.monthStart;
  doc["monthEnd"] = info.monthEnd;
  doc["gridStart"] = info.gridStart;
  doc["gridEnd"] = info.gridEnd;
  doc["count"] = count;
  doc["truncated"] = info.truncated;
  JsonArray list = doc["events"].to<JsonArray>();
  for (size_t i = 0; events && i < count; i++) {
    const CalendarMonthEvent &event = events[i];
    JsonObject item = list.add<JsonObject>();
    // The buffers are never null, so an event without a place sends "" and the
    // app never has to tell "no location" from a missing field.
    item["id"] = event.id;
    item["etag"] = event.etag;
    item["summary"] = event.summary;
    item["location"] = event.location;
    item["startEpoch"] = event.startEpoch;
    item["endEpoch"] = event.endEpoch;
    item["allDay"] = event.allDay;
  }
}

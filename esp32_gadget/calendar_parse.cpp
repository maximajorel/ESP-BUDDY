#include "calendar_parse.h"

#include <ArduinoJson.h>

#include "calendar_time.h"

namespace {

// Copies a JSON string into a fixed buffer. The cut backs off to the start of a
// UTF-8 sequence so a truncated summary never renders as a broken glyph on the
// glass.
void copyField(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  dst[0] = '\0';
  if (!src) return;
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// The earliest reminder among the array's entries, or -1 when there is none.
// Google sends one entry per method (popup, email); the owner cares about the
// first thing that fires, not which channel it uses.
int earliestMinutes(JsonArrayConst reminders) {
  int best = -1;
  for (JsonVariantConst entry : reminders) {
    JsonVariantConst minutes = entry["minutes"];
    if (!minutes.is<int>()) continue;
    int value = minutes.as<int>();
    if (value < 0) continue;
    if (best < 0 || value < best) best = value;
  }
  return best;
}

// The event's reminder, resolved to one number. `reminders` missing entirely is
// treated as "use the defaults": that is what Google did before the field
// existed, and a silent event is worse than a slightly early one.
int leadForEvent(JsonObjectConst reminders, int defaultLeadMin) {
  if (reminders.isNull()) return defaultLeadMin;
  const bool useDefault = reminders["useDefault"] | false;
  if (useDefault) return defaultLeadMin;
  return earliestMinutes(reminders["overrides"].as<JsonArrayConst>());
}

}  // namespace

size_t calendarParseCalendarList(const char *json, CalendarInfo *out, size_t cap) {
  if (!json || !out || cap == 0) return 0;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return 0;
  JsonArrayConst items = doc["items"].as<JsonArrayConst>();
  if (items.isNull()) return 0;

  size_t n = 0;
  for (JsonVariantConst item : items) {
    if (n >= cap) break;
    JsonObjectConst entry = item.as<JsonObjectConst>();
    if (entry.isNull()) continue;
    const char *id = entry["id"] | "";
    if (!*id) continue;
    CalendarInfo &info = out[n];
    memset(&info, 0, sizeof(info));
    copyField(info.id, sizeof(info.id), id);
    copyField(info.name, sizeof(info.name), entry["summary"] | id);
    info.primary = entry["primary"] | false;
    info.defaultLeadMin =
        earliestMinutes(entry["defaultReminders"].as<JsonArrayConst>());
    n++;
  }
  return n;
}

size_t calendarParseEvents(const char *json, CalendarEvent *out, size_t cap,
                           int defaultLeadMin, int tzOffsetHours) {
  if (!json || !out || cap == 0) return 0;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return 0;
  JsonArrayConst items = doc["items"].as<JsonArrayConst>();
  if (items.isNull()) return 0;

  size_t n = 0;
  for (JsonVariantConst item : items) {
    if (n >= cap) break;
    JsonObjectConst entry = item.as<JsonObjectConst>();
    if (entry.isNull()) continue;

    // A cancelled instance is still in the answer; the owner does not want a
    // face for something that no longer happens.
    const char *status = entry["status"] | "";
    if (strcmp(status, "cancelled") == 0) continue;

    JsonObjectConst start = entry["start"].as<JsonObjectConst>();
    JsonObjectConst end = entry["end"].as<JsonObjectConst>();
    if (start.isNull()) continue;

    int64_t startEpoch = 0;
    int64_t endEpoch = 0;
    bool allDay = false;
    const char *dateTime = start["dateTime"] | "";
    if (*dateTime) {
      if (!calendarParseRfc3339(dateTime, &startEpoch)) continue;
      const char *endDateTime = end["dateTime"] | "";
      if (!*endDateTime || !calendarParseRfc3339(endDateTime, &endEpoch)) {
        endEpoch = startEpoch + 3600;
      }
    } else {
      const char *date = start["date"] | "";
      if (!*date) continue;
      if (!calendarParseDate(date, tzOffsetHours, &startEpoch)) continue;
      allDay = true;
      const char *endDate = end["date"] | "";
      if (*endDate && calendarParseDate(endDate, tzOffsetHours, &endEpoch)) {
        // Google's all-day end is exclusive; the face only needs the last
        // instant the event is current.
        endEpoch -= 1;
      } else {
        endEpoch = startEpoch + 86400 - 1;
      }
    }
    if (endEpoch < startEpoch) endEpoch = startEpoch;

    CalendarEvent &event = out[n];
    memset(&event, 0, sizeof(event));
    copyField(event.summary, sizeof(event.summary), entry["summary"] | "");
    event.startEpoch = startEpoch;
    event.endEpoch = endEpoch;
    int lead = leadForEvent(entry["reminders"].as<JsonObjectConst>(),
                            defaultLeadMin);
    if (lead > CALENDAR_MAX_LEAD_MIN) lead = CALENDAR_MAX_LEAD_MIN;
    event.leadMin = lead;
    event.allDay = allDay;
    n++;
  }
  return n;
}

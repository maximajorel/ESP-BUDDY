// The local calendar's data, shared by the pure rules and the service.
//
// No `Arduino.h` here on purpose: `calendar_time.h`, `calendar_plan.h` and the
// parse half compile on the host, and `tools/calendar_core_test.cpp` is the
// proof that the arithmetic behind the face is right before a board sees it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define CALENDAR_ID_MAX 128
// Protocol limits, shared by the pure OAuth response parser and the store so the
// two cannot disagree about how much a token can be.
#define CALENDAR_TOKEN_MAX 511
#define CALENDAR_DEVICE_CODE_MAX 256
#define CALENDAR_USER_CODE_MAX 16
#define CALENDAR_VERIFY_URL_MAX 96
#define CALENDAR_NAME_MAX 64
#define CALENDAR_SUMMARY_MAX 96
#define CALENDAR_LOCATION_MAX 96

// One page of Google's list is plenty for a personal account, and a fixed array
// keeps the selector out of the heap.
#define CALENDAR_MAX_LIST 24
// The events window is the owner's day plus the 26 h of reaction ahead, so the
// buffer holds a whole busy day and still has tomorrow's meetings for the face.
// 24 events cost ~5 KB of internal RAM, twice (the live copy and the parse
// scratch); the board reports 22% of DRAM used with this in place.
#define CALENDAR_MAX_EVENTS 24

// El presupuesto del cuerpo de `events.list`, derivado del buffer y no de un
// número suelto: 1 KB por evento alcanza para un título y un lugar largos (el
// cuerpo trae el texto entero aunque el equipo recorte a 96), start/end con zona
// y dos recordatorios, y 4 KB cubren el sobre de la respuesta. Un cuerpo más
// grande que el presupuesto no se parsea a medias: se descarta entero, así que
// quedarse corto deja al dueño sin agenda. `tools/calendar_core_test.cpp`
// construye el peor caso y falla si deja de entrar.
#define CALENDAR_EVENT_BODY_BYTES 1024
#define CALENDAR_EVENTS_BODY_BYTES \
  (4096 + CALENDAR_MAX_EVENTS * CALENDAR_EVENT_BODY_BYTES)

// --- the month grid ---------------------------------------------------------
//
// A month is a different animal from the day list, and it does not reuse it.
// The day list feeds the face on every frame and lives in internal RAM, so it
// stays exactly as small as it was. The month is only drawn when the popover is
// open, only lives for the length of one request, and lives in PSRAM. It is also
// the one that the tray edits, which is why it is the one that carries Google's
// `id` and `etag`: without them nothing can be updated or deleted.
// Google's own id is ~26-102 chars, but a recurring instance is
// `<id>_<YYYYMMDD>T<HHMMSS>Z`: up to ~130 for a long base id. 160 covers that
// without ever cutting one, and cutting an id is not cosmetic — a truncated id
// is an event the owner can see and cannot edit, and the failure only shows up
// as "ese evento ya no existe" after he typed the change.
#define CALENDAR_EVENT_ID_MAX 160
#define CALENDAR_EVENT_ETAG_MAX 64
// Google ids are ~26-102 chars and an etag is a quoted tick (`"3201234567890"`).
// A week of a busy calendar is ~20 events; 150 covers a month with room to spare
// and costs ~70 KB of PSRAM in the worst case. The number is not round on
// purpose: it is the largest cap whose worst-case *response* still fits
// `CALENDAR_MONTH_RESPONSE_BUDGET` with margin, and
// `tools/calendar_month_test.cpp` fails if that stops being true.
#define CALENDAR_MONTH_MAX_EVENTS 150
#define CALENDAR_MONTH_EVENT_BODY_BYTES 1024
#define CALENDAR_MONTH_BODY_BYTES \
  (4096 + CALENDAR_MONTH_MAX_EVENTS * CALENDAR_MONTH_EVENT_BODY_BYTES)
// El techo de la respuesta que el equipo arma en RAM interna: 200 filas con el
// id y el titulo enteros son el peor caso, y ese numero esta medido por
// `tools/calendar_month_test.cpp` sobre el payload serializado. No es un limite
// que se aplique en runtime (serializar a `String` no se puede cortar a mitad),
// es el presupuesto contra el que se elige `CALENDAR_MONTH_MAX_EVENTS`: si el peor
// caso no entra, se baja el tope de eventos, no se reza.
#define CALENDAR_MONTH_RESPONSE_BUDGET (96 * 1024)

// Google accepts reminders up to four weeks ahead. A face that stays alert for
// a month is not a warning any more, so the reaction caps the lead at one day.
#define CALENDAR_MAX_LEAD_MIN 1440

struct CalendarInfo {
  char id[CALENDAR_ID_MAX];
  char name[CALENDAR_NAME_MAX];
  bool primary;
  // The calendar's own default reminder, in minutes. -1 means "this calendar
  // declares no default", which lets the panel's fallback decide.
  int defaultLeadMin;
};

struct CalendarEvent {
  char summary[CALENDAR_SUMMARY_MAX];
  char location[CALENDAR_LOCATION_MAX];
  int64_t startEpoch;  // UTC seconds
  int64_t endEpoch;    // UTC seconds; for an all-day event, the next midnight
  // The reminder that opens the window, already resolved. -1 means this event
  // must never react: the owner turned its reminders off.
  int leadMin;
  bool allDay;
};

// One row of the month grid. Strings are fixed buffers for the same reason as
// `CalendarEvent`: the parse happens once, on the network task, and nothing is
// allowed to hand a pointer to a live JSON document across a task boundary.
struct CalendarMonthEvent {
  char id[CALENDAR_EVENT_ID_MAX];
  char etag[CALENDAR_EVENT_ETAG_MAX];
  char summary[CALENDAR_SUMMARY_MAX];
  char location[CALENDAR_LOCATION_MAX];
  int64_t startEpoch;  // UTC seconds
  int64_t endEpoch;    // UTC seconds; for an all-day event, the last second
  bool allDay;
};

enum CalendarPhase {
  CALENDAR_PHASE_NONE = 0,
  CALENDAR_PHASE_EARLY,     // inside the window, more than half of it left
  CALENDAR_PHASE_NEAR,      // half to the last stretch
  CALENDAR_PHASE_STARTING,  // the final 15% of the lead window
  CALENDAR_PHASE_ONGOING,   // the event already started, within the grace
};

struct CalendarReaction {
  bool active;
  CalendarPhase phase;
  uint8_t intensity;  // 0..100, how much of the lead window is consumed
  int32_t msToStart;  // negative once the event started
  int leadMin;
  int graceMin;
  int64_t startEpoch;  // for the clock in the split frame
  char summary[CALENDAR_SUMMARY_MAX];
  char location[CALENDAR_LOCATION_MAX];
};

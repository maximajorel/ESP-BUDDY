#include "calendar.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

#include "calendar_forms.h"
#include "calendar_http.h"
#include "calendar_parse.h"
#include "calendar_schedule.h"
#include "calendar_plan.h"
#include "calendar_store.h"
#include "calendar_time.h"
#include "settings.h"

namespace {

constexpr uint32_t kTaskStack = 12288;
constexpr size_t kListBodyBytes = 8192;
// Derivado de CALENDAR_MAX_EVENTS, no un número suelto: un cuerpo más grande que
// el presupuesto se descarta entero (`CALENDAR_HTTP_BODY_TOO_LARGE`), así que
// quedarse corto no da menos filas, deja al dueño sin agenda. El peor caso está
// medido en `tools/calendar_core_test.cpp`.
constexpr size_t kEventsBodyBytes = CALENDAR_EVENTS_BODY_BYTES;
// The events window is the owner's whole day (the popover shows it in full,
// including what already happened) plus more than the 24-hour reaction cap ahead,
// so a reminder never opens on an event the device did not fetch. The lower
// bound is `calendarDayQueryStart`: local midnight, never less than an hour back.
constexpr int64_t kFutureSeconds = 26 * 3600;

SemaphoreHandle_t s_lock = nullptr;
TaskHandle_t s_task = nullptr;

CalendarInfo s_calendars[CALENDAR_MAX_LIST];
size_t s_calendarCount = 0;
CalendarEvent s_events[CALENDAR_MAX_EVENTS];
size_t s_eventCount = 0;
char s_lastError[96] = "";
uint32_t s_lastSyncMs = 0;
int64_t s_lastSyncEpoch = 0;
bool s_syncing = false;
volatile bool s_refreshRequested = false;
uint32_t s_lastAttemptMs = 0;
uint32_t s_attempts = 0;
// Intentos fallidos seguidos: sólo lo lee `calendarTick` para elegir el ritmo de
// reintento, y lo satura para que no siga creciendo años después.
uint32_t s_failures = 0;

// Parsing scratch: only the service task writes here, so it needs no lock. The
// shared copies are swapped under the lock after the parse succeeds.
CalendarInfo s_scratchCalendars[CALENDAR_MAX_LIST];
CalendarEvent s_scratchEvents[CALENDAR_MAX_EVENTS];

void lock() {
  if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

void unlock() {
  if (s_lock) xSemaphoreGive(s_lock);
}

void setError(const char *text) {
  lock();
  snprintf(s_lastError, sizeof(s_lastError), "%s", text ? text : "");
  s_syncing = false;
  unlock();
}

int selectedIndex() {
  const char *selected = calendarStoreSelectedId();
  if (!*selected) return -1;
  for (size_t i = 0; i < s_calendarCount; i++) {
    if (strcmp(s_calendars[i].id, selected) == 0) return (int)i;
  }
  return -1;
}

// Reads the calendar list so the selected calendar's own default reminders are
// known, and so the panel's selector shows real names. A failure here is not
// fatal: the events are still worth fetching with the panel's fallback lead.
bool fetchCalendars(const char *token, char *err, size_t errCap) {
  CalendarHttpResult res;
  if (!calendarHttpGet(CALENDAR_API_HOST, CALENDAR_LIST_PATH, token,
                       kListBodyBytes, &res)) {
    snprintf(err, errCap, "No pude pedir la lista de calendarios");
    return false;
  }
  if (res.status != 200) {
    snprintf(err, errCap, "Google devolvio %d al listar calendarios", res.status);
    return false;
  }
  const size_t count =
      calendarParseCalendarList(res.body.c_str(), s_scratchCalendars,
                                CALENDAR_MAX_LIST);
  if (count == 0) {
    snprintf(err, errCap, "La lista de calendarios vino vacia o ilegible");
    return false;
  }
  lock();
  memcpy(s_calendars, s_scratchCalendars, sizeof(s_calendars));
  s_calendarCount = count;
  unlock();
  return true;
}

// Devuelve true sólo si la agenda se refrescó: el que reintenta necesita saber
// si el fallo fue un tropiezo del arranque o algo que va a seguir así.
bool fetchNow() {
  lock();
  s_syncing = true;
  unlock();

  char err[96] = "";
  if (!calendarAuthEnsureAccessToken(false, err, sizeof(err))) {
    setError(err[0] ? err : "No pude renovar el permiso de Google");
    return false;
  }
  const char *token = calendarAuthAccessToken();

  // The list is auxiliary: a failing list must not hide the events.
  char listError[96] = "";
  const bool listed = fetchCalendars(token, listError, sizeof(listError));

  const char *selected = calendarStoreSelectedId();
  if (!*selected) selected = "primary";
  int defaultLead = calendarStoreFallbackLeadMin();
  const int index = selectedIndex();
  if (index >= 0 && s_calendars[index].defaultLeadMin >= 0) {
    defaultLead = s_calendars[index].defaultLeadMin;
  }

  const time_t now = time(nullptr);
  int64_t from = 0;
  if (!calendarDayQueryStart((int64_t)now, settingsGet().tzOffsetHours, &from)) {
    setError("No pude calcular el comienzo del dia");
    return false;
  }
  const int64_t to = (int64_t)now + kFutureSeconds;
  char path[512];
  if (!calendarEventsPath(selected, from, to, path, sizeof(path))) {
    setError("El nombre del calendario no entra en el pedido");
    return false;
  }

  CalendarHttpResult res;
  if (!calendarHttpGet(CALENDAR_API_HOST, path, token, kEventsBodyBytes, &res)) {
    setError("No pude pedir los eventos a Google");
    return false;
  }
  if (res.status != 200) {
    snprintf(err, sizeof(err), "Google devolvio %d al pedir eventos", res.status);
    setError(err);
    return false;
  }

  const size_t count =
      calendarParseEvents(res.body.c_str(), s_scratchEvents,
                          CALENDAR_MAX_EVENTS, defaultLead,
                          settingsGet().tzOffsetHours);
  lock();
  memcpy(s_events, s_scratchEvents, sizeof(s_events));
  s_eventCount = count;
  s_lastSyncMs = millis();
  s_lastSyncEpoch = (int64_t)now;
  s_lastError[0] = '\0';
  if (!listed && listError[0]) {
    // Events arrived; the selector will keep its last known list. The panel still
    // deserves to know why the names did not refresh.
    snprintf(s_lastError, sizeof(s_lastError), "%s", listError);
  }
  s_syncing = false;
  unlock();
  return true;
}

void taskLoop(void *) {
  for (;;) {
    calendarTick();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool clockReady() { return time(nullptr) > 1600000000; }

// Set by the serial console only. Volatile because the web task reads it.
volatile bool s_debugFake = false;
volatile int32_t s_debugMinutes = 0;
volatile int32_t s_debugLeadMin = 30;

constexpr int64_t kDebugBase = 2000000000;

}  // namespace

void calendarBegin() {
  if (s_lock) return;
  s_lock = xSemaphoreCreateMutex();
  calendarStoreBegin();
  calendarAuthBegin();
  calendarHttpBegin();
  xTaskCreatePinnedToCore(taskLoop, "calendar", kTaskStack, nullptr, 1, &s_task,
                          1);
}

void calendarTick() {
  CalendarFlow flow;
  calendarAuthFlowCopy(&flow);
  if (flow.pending) {
    bool authorized = false;
    char err[96] = "";
    calendarAuthPollFlow(&authorized, err, sizeof(err));
    if (authorized) s_refreshRequested = true;
    if (err[0]) setError(err);
  }

  if (!calendarStoreConfigured() || !calendarStoreAuthorized()) return;
  // The reaction window is arithmetic on real time, so without NTP there is
  // nothing honest to ask Google for. The panel already says the clock is not
  // synced in the network card.
  if (!clockReady()) return;
  // Pero el reloj no alcanza: en un reset tibio el RTC conserva la hora y el
  // WiFi todavía se está asociando, así que la primera petición salía sin
  // enlace ("connection refused", transport=-1) y el dueño veía la agenda vacía
  // hasta el reintento. Preguntar a Google exige enlace, no sólo hora.
  if (WiFi.status() != WL_CONNECTED) return;

  const uint32_t now = millis();
  // La política vive en `calendar_schedule.h` y se prueba en host: sin un sync
  // bueno se reintenta a los quince segundos, porque el primer intercambio
  // después de arrancar puede volver ilegible (el WiFi todavía se está
  // asentando) y dos minutos de agenda vacía no son un costo aceptable. Con un
  // sync bueno, quince minutos alcanzan para un calendario personal.
  const uint32_t interval = calendarSyncDelayMs(s_lastSyncMs != 0, s_failures);
  const bool due = s_attempts == 0 || s_refreshRequested ||
                   (uint32_t)(now - s_lastAttemptMs) >= interval;
  if (!due) return;
  s_refreshRequested = false;
  s_lastAttemptMs = now;
  s_attempts++;
  if (fetchNow()) {
    s_failures = 0;
  } else if (s_failures < 60000) {
    s_failures++;
  }
}

CalendarStatusInfo calendarStatusInfo() {
  CalendarStatusInfo info;
  memset(&info, 0, sizeof(info));
  info.configured = calendarStoreConfigured();
  info.authorized = calendarStoreAuthorized();

  CalendarFlow flow;
  calendarAuthFlowCopy(&flow);
  info.pending = flow.pending;
  if (flow.pending) {
    snprintf(info.userCode, sizeof(info.userCode), "%s", flow.userCode);
    snprintf(info.verificationUrl, sizeof(info.verificationUrl), "%s",
             flow.verificationUrl);
    const int32_t left = (int32_t)(flow.expiresAtMs - millis());
    info.pendingSecondsLeft = left > 0 ? left / 1000 : 0;
  }

  lock();
  info.kind = !info.configured     ? CALENDAR_UNCONFIGURED
              : info.pending       ? CALENDAR_AUTH_PENDING
              : info.authorized    ? CALENDAR_READY
                                   : CALENDAR_NEEDS_AUTH;
  snprintf(info.selectedId, sizeof(info.selectedId), "%s",
           calendarStoreSelectedId());
  snprintf(info.selectedName, sizeof(info.selectedName), "%s",
           calendarStoreSelectedName());
  info.fallbackLeadMin = calendarStoreFallbackLeadMin();
  info.graceMin = calendarStoreGraceMin();
  // Sin credenciales no hay error que mostrar: el ultimo fallo pertenece a una
  // configuracion que ya no existe y solo confunde el paso a paso.
  snprintf(info.lastError, sizeof(info.lastError), "%s",
           info.configured ? s_lastError : "");
  info.lastSyncMs = s_lastSyncMs;
  info.syncing = s_syncing;
  info.calendarCount = s_calendarCount;
  info.eventCount = s_eventCount;

  // The next event for the panel's summary, even when its reminder is off or it
  // is outside the reaction window: the owner asked for the event, not only the
  // ones that move the face.
  const int64_t nowEpoch = (int64_t)time(nullptr);
  const CalendarEvent *next = nullptr;
  for (size_t i = 0; i < s_eventCount; i++) {
    const CalendarEvent &e = s_events[i];
    if (e.startEpoch < nowEpoch - 3600) continue;
    if (!next || e.startEpoch < next->startEpoch) next = &e;
  }
  if (next) {
    info.hasNext = true;
    snprintf(info.nextSummary, sizeof(info.nextSummary), "%s", next->summary);
    const int64_t deltaMs = (next->startEpoch - nowEpoch) * 1000;
    info.nextMsToStart = deltaMs > 2147483647LL
                             ? 2147483647
                             : (deltaMs < -2147483647LL ? -2147483647
                                                        : (int32_t)deltaMs);
    info.nextLeadMin = next->leadMin;
  }
  unlock();
  return info;
}

bool calendarCalendarAt(size_t index, CalendarInfo *out) {
  if (!out) return false;
  lock();
  const bool ok = index < s_calendarCount;
  if (ok) *out = s_calendars[index];
  unlock();
  return ok;
}

size_t calendarEventsCopy(CalendarEvent *out, size_t cap) {
  if (!out || cap == 0) return 0;
  lock();
  const size_t count = s_eventCount < cap ? s_eventCount : cap;
  if (count > 0) memcpy(out, s_events, count * sizeof(CalendarEvent));
  unlock();
  return count;
}

void calendarSelectedName(char *out, size_t cap) {
  if (!out || cap == 0) return;
  lock();
  snprintf(out, cap, "%s", calendarStoreSelectedName());
  unlock();
  if (!out[0]) snprintf(out, cap, "PRINCIPAL");
}

CalendarReaction calendarReactionNow() {
  CalendarReaction r;
  memset(&r, 0, sizeof(r));
  r.phase = CALENDAR_PHASE_NONE;
  if (s_debugFake) {
    // A synthetic event at a fixed epoch, so the output does not depend on NTP.
    CalendarEvent event;
    memset(&event, 0, sizeof(event));
    snprintf(event.summary, sizeof(event.summary), "Evento de prueba");
    event.startEpoch = kDebugBase + (int64_t)s_debugMinutes * 60;
    event.endEpoch = event.startEpoch + 3600;
    event.leadMin = (int)s_debugLeadMin;
    calendarPickReaction(&event, 1, kDebugBase, calendarStoreGraceMin(), &r);
    return r;
  }
  if (!clockReady()) return r;
  const int64_t nowEpoch = (int64_t)time(nullptr);
  lock();
  calendarPickReaction(s_events, s_eventCount, nowEpoch,
                       calendarStoreGraceMin(), &r);
  unlock();
  return r;
}

bool calendarRequestRefresh() {
  if (!calendarStoreAuthorized()) return false;
  s_refreshRequested = true;
  return true;
}

bool calendarStartFlow(char *err, size_t errCap) {
  if (err && errCap) err[0] = '\0';
  if (!calendarAuthStartFlow(err, errCap)) {
    setError(err && err[0] ? err : "No pude empezar la conexion con Google");
    return false;
  }
  lock();
  s_lastError[0] = '\0';
  unlock();
  return true;
}

void calendarCancelFlow() { calendarAuthCancelFlow(); }

void calendarDebugFakeEvent(int minutesToStart, int leadMin) {
  if (leadMin < 0) leadMin = 0;
  if (leadMin > CALENDAR_MAX_LEAD_MIN) leadMin = CALENDAR_MAX_LEAD_MIN;
  if (minutesToStart < -1440) minutesToStart = -1440;
  if (minutesToStart > 1440) minutesToStart = 1440;
  s_debugMinutes = minutesToStart;
  s_debugLeadMin = leadMin;
  s_debugFake = true;
}

void calendarDebugFakeOff() { s_debugFake = false; }

bool calendarSelect(const char *id, const char *name) {
  if (!calendarStoreSetSelected(id, name)) return false;
  s_refreshRequested = true;
  return true;
}

#include "calendar.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <time.h>

#include "calendar_forms.h"
#include "calendar_http.h"
#include "calendar_parse.h"
#include "calendar_schedule.h"
#include "calendar_plan.h"
#include "calendar_store.h"
#include "calendar_time.h"
#include "calendar_write.h"
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

// The JSON tree of a whole month is hundreds of kilobytes, which is three times
// what the board has free internally. ArduinoJson takes an allocator, so the tree
// of the month goes to PSRAM and the day list keeps using the internal heap it
// already fits in.
class PsramAllocator : public ArduinoJson::Allocator {
 public:
  void *allocate(size_t size) override {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  void deallocate(void *ptr) override { heap_caps_free(ptr); }
  void *reallocate(void *ptr, size_t size) override {
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

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

bool calendarFetchMonth(int year, int month, CalendarMonthEvent *out, size_t cap,
                        size_t *count, CalendarMonthInfo *info, char *err,
                        size_t errCap) {
  if (err && errCap) err[0] = '\0';
  if (!out || !info || cap == 0) return false;
  if (count) *count = 0;
  memset(info, 0, sizeof(*info));

  const int tz = settingsGet().tzOffsetHours;
  CalendarMonthWindow window;
  if (!calendarMonthWindow(year, month, tz, &window)) {
    if (err && errCap) snprintf(err, errCap, "El mes pedido no existe");
    return false;
  }
  if (!calendarStoreConfigured()) {
    if (err && errCap) snprintf(err, errCap, "Falta configurar Google en el panel");
    return false;
  }
  if (!calendarStoreAuthorized()) {
    if (err && errCap) snprintf(err, errCap, "Falta conectar la cuenta de Google");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (err && errCap) snprintf(err, errCap, "El equipo no tiene red");
    return false;
  }

  char authError[96] = "";
  if (!calendarAuthEnsureAccessToken(false, authError, sizeof(authError))) {
    if (err && errCap) {
      snprintf(err, errCap, "%s",
               authError[0] ? authError : "No pude renovar el permiso de Google");
    }
    return false;
  }

  const char *selected = calendarStoreSelectedId();
  if (!*selected) selected = "primary";
  char path[512];
  if (!calendarMonthPath(selected, window.gridStart, window.gridEnd, path,
                         sizeof(path))) {
    if (err && errCap) {
      snprintf(err, errCap, "El nombre del calendario no entra en el pedido");
    }
    return false;
  }

  // El primer intercambio TLS despues de arrancar falla de vez en cuando en esta
  // placa (el WiFi todavia se esta asentando) y el mes no tiene un poll detras que
  // lo reintente como lo tiene el dia: se vio en la placa, el primer pedido de mes
  // despues del reset contestaba "no respondio" y el siguiente ya andaba. Un
  // segundo intento, solo ante un error de transporte, convierte ese tropiezo en
  // una espera en vez de en un error que el dueño tiene que volver a tocar.
  CalendarHttpBigBody body;
  bool fetched = calendarHttpGetBig(CALENDAR_API_HOST, path,
                                    calendarAuthAccessToken(),
                                    CALENDAR_MONTH_BODY_BYTES, &body);
  if (fetched && body.transportError != 0 && body.status == 0) {
    Serial.printf("[calendar] mes: reintento tras transport=%d\n",
                  body.transportError);
    // Medido en la placa: con el enlace debil (-85 dBm) el primer intento
    // despues de un reset se rechaza y el segundo, medio segundo despues,
    // contesta. Reintentar en el mismo instante repite el mismo error.
    vTaskDelay(pdMS_TO_TICKS(500));
    calendarHttpFreeBigBody(&body);
    fetched = calendarHttpGetBig(CALENDAR_API_HOST, path,
                                 calendarAuthAccessToken(),
                                 CALENDAR_MONTH_BODY_BYTES, &body);
  }
  if (!fetched) {
    if (err && errCap) snprintf(err, errCap, "No pude pedir el mes a Google");
    return false;
  }
  // One owner for the PSRAM buffer, whatever the answer was.
  struct Release {
    CalendarHttpBigBody *body;
    ~Release() { calendarHttpFreeBigBody(body); }
  } release{&body};

  if (body.transportError == CALENDAR_HTTP_BODY_TOO_LARGE) {
    if (err && errCap) {
      snprintf(err, errCap, "El mes no entra en la memoria del equipo");
    }
    return false;
  }
  if (body.transportError != 0) {
    // El codigo de transporte va al puerto serie, no a la cara del dueño: en el
    // popover "-1" no le dice nada y "proba de nuevo" sí es la accion que sirve.
    Serial.printf("[calendar] mes: transport=%d\n", body.transportError);
    if (err && errCap) {
      snprintf(err, errCap,
               "Google no respondio: proba de nuevo en unos segundos");
    }
    return false;
  }
  if (body.status == 403) {
    // The grant was made with `calendar.readonly`: this is the honest sentence
    // for that, and the panel needs to say it before the owner blames the tray.
    if (err && errCap) {
      snprintf(err, errCap,
               "Google no autorizo leer este calendario: volve a conectar la "
               "cuenta en el panel");
    }
    return false;
  }
  if (body.status == 401) {
    if (err && errCap) {
      snprintf(err, errCap,
               "Google rechazo el permiso guardado: volve a conectar la cuenta");
    }
    return false;
  }
  if (body.status != 200) {
    if (err && errCap) {
      snprintf(err, errCap, "Google devolvio %d al pedir el mes", body.status);
    }
    return false;
  }

  PsramAllocator allocator;
  const size_t parsed =
      calendarParseMonthEvents(body.data, out, cap, tz, &allocator);

  info->year = year;
  info->month = month;
  info->tzOffsetHours = tz;
  info->monthStart = window.monthStart;
  info->monthEnd = window.monthEnd;
  info->gridStart = window.gridStart;
  info->gridEnd = window.gridEnd;
  // Reaching the cap is the only signal Google gives us without a second page:
  // saying so is better than drawing a month that looks complete.
  info->truncated = parsed >= cap;
  if (count) *count = parsed;
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

// --- escritura ---------------------------------------------------------------
//
// One place decides what a Google status code means to the owner. The tray shows
// `message` and never has to translate a number, and "403" cannot mean one thing
// when creating and another when deleting.

namespace {

CalendarWriteOutcome writeFail(int status, const char *message) {
  CalendarWriteOutcome outcome;
  outcome.ok = false;
  outcome.status = status;
  snprintf(outcome.message, sizeof(outcome.message), "%s",
           message ? message : "");
  return outcome;
}

CalendarWriteOutcome writeOk(int status) {
  CalendarWriteOutcome outcome;
  outcome.ok = true;
  outcome.status = status;
  outcome.message[0] = '\0';
  return outcome;
}

// Everything a write needs before it is worth asking Google. Returns false and
// fills `out` with the reason when the board cannot write at all.
bool writeReady(CalendarWriteOutcome *out) {
  if (!calendarStoreConfigured()) {
    *out = writeFail(0, "Falta configurar Google en el panel");
    return false;
  }
  if (!calendarStoreAuthorized()) {
    *out = writeFail(0, "Falta conectar la cuenta de Google");
    return false;
  }
  // The grant from a firmware that only asked `calendar.readonly` still refreshes
  // a token and still reads; every write answers 403. Saying it here is what saves
  // the owner from typing an event and losing it.
  if (!calendarStoreWriteScope()) {
    *out = writeFail(
        403,
        "El permiso guardado es de solo lectura: reconectá la cuenta en el panel "
        "para poder editar");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    *out = writeFail(0, "El equipo no tiene red");
    return false;
  }
  char error[96] = "";
  if (!calendarAuthEnsureAccessToken(false, error, sizeof(error))) {
    *out = writeFail(0, error[0] ? error : "No pude renovar el permiso de Google");
    return false;
  }
  return true;
}

CalendarWriteOutcome mapWriteResult(const CalendarHttpResult &res) {
  if (res.transportError != 0) {
    return writeFail(0, "No pude hablar con Google");
  }
  switch (res.status) {
    case 200:
    case 201:
    case 204:
      return writeOk(res.status);
    case 401:
      return writeFail(
          401,
          "Google rechazo el permiso guardado: volve a conectar la cuenta en el "
          "panel");
    case 403:
      // Whatever we believed about the grant, Google says this one cannot write.
      // Recording it keeps the next attempt from repeating the same lost edit.
      calendarStoreSetWriteScope(false);
      return writeFail(
          403,
          "Google no permite escribir: reconectá la cuenta en el panel para dar "
          "el permiso de edicion");
    case 404:
      return writeFail(404, "Ese evento ya no existe en Google");
    case 409:
      return writeFail(409, "Ese evento ya existe o cambio: recarga el mes");
    case 410:
      return writeFail(410, "Ese evento se borro en otro lado");
    case 412:
      return writeFail(
          412,
          "Ese evento cambio en otro lado: recarga el mes y volve a intentar");
    case 429:
      return writeFail(429,
                       "Google pidio esperar un momento: proba de nuevo en un "
                       "minuto");
    default:
      return writeFail(res.status, "Google no acepto el cambio");
  }
}

const char *selectedCalendar() {
  const char *selected = calendarStoreSelectedId();
  return *selected ? selected : "primary";
}

}  // namespace

CalendarWriteOutcome calendarCreateEvent(const char *summary,
                                         const char *location,
                                         int64_t startEpoch, int64_t endEpoch,
                                         bool allDay) {
  CalendarWriteOutcome outcome;
  if (!writeReady(&outcome)) return outcome;
  if (endEpoch <= startEpoch) {
    return writeFail(400, "El evento termina antes de empezar");
  }

  JsonDocument doc;
  if (!calendarEventBodyJson(summary, location, startEpoch, endEpoch, allDay,
                             settingsGet().tzOffsetHours, doc)) {
    return writeFail(400, "Las fechas del evento no son validas");
  }
  String body;
  serializeJson(doc, body);

  char path[512];
  if (!calendarCreatePath(selectedCalendar(), path, sizeof(path))) {
    return writeFail(400, "El nombre del calendario no entra en el pedido");
  }
  CalendarHttpResult res;
  if (!calendarHttpJson("POST", CALENDAR_API_HOST, path,
                        calendarAuthAccessToken(), nullptr, body, 4096, &res)) {
    return writeFail(0, "No pude pedir el alta a Google");
  }
  outcome = mapWriteResult(res);
  if (outcome.ok) s_refreshRequested = true;
  return outcome;
}

CalendarWriteOutcome calendarUpdateEvent(const char *id, const char *etag,
                                         const char *summary,
                                         const char *location,
                                         int64_t startEpoch, int64_t endEpoch,
                                         bool allDay) {
  CalendarWriteOutcome outcome;
  if (!id || !*id) return writeFail(400, "Falta el evento");
  // Sin `If-Match` Google escribe igual y pisa el cambio que el dueño hizo en el
  // telefono mientras el popover estaba abierto: se niega antes de salir, con el
  // código que significa exactamente esto (428 Precondition Required).
  if (!calendarEtagUsable(etag)) {
    return writeFail(428,
                     "Falta la version del evento: recarga el mes y volve a "
                     "intentar");
  }
  if (!writeReady(&outcome)) return outcome;
  if (endEpoch <= startEpoch) {
    return writeFail(400, "El evento termina antes de empezar");
  }

  JsonDocument doc;
  if (!calendarEventBodyJson(summary, location, startEpoch, endEpoch, allDay,
                             settingsGet().tzOffsetHours, doc)) {
    return writeFail(400, "Las fechas del evento no son validas");
  }
  String body;
  serializeJson(doc, body);

  char path[512];
  if (!calendarEventPath(selectedCalendar(), id, path, sizeof(path))) {
    return writeFail(400, "El nombre del calendario no entra en el pedido");
  }
  CalendarHttpResult res;
  if (!calendarHttpJson("PATCH", CALENDAR_API_HOST, path,
                        calendarAuthAccessToken(), etag, body, 4096, &res)) {
    return writeFail(0, "No pude pedir el cambio a Google");
  }
  outcome = mapWriteResult(res);
  if (outcome.ok) s_refreshRequested = true;
  return outcome;
}

CalendarWriteOutcome calendarDeleteEvent(const char *id, const char *etag) {
  CalendarWriteOutcome outcome;
  if (!id || !*id) return writeFail(400, "Falta el evento");
  // Mismo motivo que en el cambio: un borrado sin version se lleva lo que otro
  // cambio sin decir nada.
  if (!calendarEtagUsable(etag)) {
    return writeFail(428,
                     "Falta la version del evento: recarga el mes y volve a "
                     "intentar");
  }
  if (!writeReady(&outcome)) return outcome;

  char path[512];
  if (!calendarEventPath(selectedCalendar(), id, path, sizeof(path))) {
    return writeFail(400, "El nombre del calendario no entra en el pedido");
  }
  CalendarHttpResult res;
  if (!calendarHttpJson("DELETE", CALENDAR_API_HOST, path,
                        calendarAuthAccessToken(), etag, "", 4096, &res)) {
    return writeFail(0, "No pude pedir el borrado a Google");
  }
  outcome = mapWriteResult(res);
  if (outcome.ok) s_refreshRequested = true;
  return outcome;
}

#pragma once
#include <Arduino.h>

// El calendario lo resuelve el bridge: acá sólo se lee su estado y se le manda
// la configuración. El ESP no habla con Google ni guarda credenciales; el panel
// es la cara cómoda de algo que ya vive del otro lado.
//
// Los dos tokens importan y son distintos: leer el estado usa el token del buddy
// (el mismo del poll de la cara), escribir usa el de administración. Si el equipo
// no tiene el de administración, el panel muestra todo y no deja guardar.

struct BuddyCalendarStatus {
  int status;  // HTTP del bridge; 0 = no hubo respuesta
  bool configured;      // las tres credenciales están puestas
  bool active;          // hay un evento corriendo ahora mismo
  bool hasNext;         // nextSummary/nextInMin valen
  bool hasAskToken;     // este equipo puede guardar (token de administración)
  bool hasClientId;
  bool hasClientSecret;
  bool hasRefreshToken;
  char calendarId[48];
  char nextSummary[96];
  char redirect[128];  // redirect_uri que hay que registrar en Google
  int nextInMin;
  int leadMin;
  int graceMin;
};

// Lo que el panel puede editar. Un campo vacío significa "no lo toques": así el
// formulario puede mostrar sólo el calendario y no obligar a reescribir secretos.
struct BuddyCalendarEdit {
  const char *clientId;
  const char *clientSecret;
  const char *refreshToken;
  const char *calendarId;
  int leadMin;
  int graceMin;
};

// Pide GET /calendar al bridge y llena `out`. Devuelve false cuando no hubo
// respuesta o el cuerpo no se pudo leer; `out->status` dice el porqué.
bool buddyCalendarStatus(BuddyCalendarStatus *out);

// POST /calendar con el token de administración. `err` recibe una frase para el
// panel, no un código.
bool buddyCalendarSave(const BuddyCalendarEdit &edit, char *err, size_t errCap);

// POST /calendar con {"clear": true}: saca las credenciales del bridge.
bool buddyCalendarForget(char *err, size_t errCap);

// POST /calendar/oauth/start: el bridge devuelve la URL de Google a la que hay
// que mandar al dueño. `urlOut` queda vacío si el bridge no soporta el flujo.
bool buddyCalendarOAuthStart(const char *clientId, const char *clientSecret,
                             char *urlOut, size_t urlCap, char *err,
                             size_t errCap);

# Vista de mes del calendario, brillo/LED sin sesión y título del tray

Cuarto pedido del dueño sobre la app de bandeja de `odd/tasks/tray-buddy.md`.
Abre el mes entero en el popover con navegación, lleva la edición de eventos hasta
Google a través del equipo, y hace públicas **sólo** las escrituras de brillo y LED.

## Specs

S1. Vista de todo el mes con navegación — verbatim: "Tener una vista de todo el
mes y navegar entre meses, conservando el lenguaje de diseño". El popover dibuja
una grilla de mes (semana que empieza el lunes, 5 o 6 filas) con los botones
anterior / siguiente / hoy, y los eventos del mes visible en cada celda. El
lenguaje de diseño es el de `design/system.md` y `design/tray-buddy/`: papel y
tinta, tipografía monoespaciada para las horas, sin colores nuevos. La grilla se
pide al equipo, que es el único que conoce el offset horario del dueño.

S2. Ver y editar los eventos — verbatim: "ver los eventos y editarlos (si hay
limitaciones de lo que puede hacer la API me avisas...)". Hoy la API del equipo
sólo lee: el scope guardado es `calendar.readonly`, el modelo `CalendarEvent` no
guarda el id del evento, y no existe ningún endpoint de escritura. Por eso el
pedido se cumple en dos partes. (a) **Ver**: el equipo gana un endpoint de mes
(`GET /api/calendar/month?y=&m=`) que pide a Google la ventana de la grilla
—hasta 42 días— y devuelve los eventos con `id`, `etag`, título, lugar, horas y
la marca de todo el día. Sin parámetros, `GET /api/calendar/events` sigue
devolviendo exactamente el día de hoy, byte por byte. (b) **Editar**: el scope
pasa a `https://www.googleapis.com/auth/calendar`, el modelo gana `id` y `etag`,
el equipo gana `POST /api/calendar/events` (alta), `POST /api/calendar/events/update`
y `POST /api/calendar/events/delete`, y el dueño re-consiente el permiso en el
equipo con su cuenta. Un 412 (etag viejo), un 410 (borrado en otro lado) o un 403
(scope insuficiente) se le informan al dueño en español: nunca un éxito callado.

S3. Brillo del LCD y LED RGB sin autenticación — verbatim: "Controlar el brillo
del LCD y del Led RGB sin autenticación". El firmware publica dos escrituras
angostas: `POST /api/lcd` (sólo `brightness`) y `POST /api/led` (sólo `r`, `g`,
`b`, `level`, `effect`). Entran en `adminIsPublicWrite` con comparación exacta de
método y ruta, igual que `/api/focus`, `/api/screen` y `/api/face`. `POST
/api/settings` **sigue** pidiendo sesión, porque también escribe `tz`, `buddyUrl`,
`buddyToken` y `buddyAskToken`. El popover gana los controles de brillo y LED que
hoy no tiene.

S4. Título del tray — verbatim: "En el titulo del tray donde dice \"BUDDY\" vamos
a sacar el texto que dice \"Bandeja\"". La cabecera dice `BUDDY` y nada más: se
quita el `· bandeja`, no el resto de la barra.

## Tasks

| ID | Qué | Specs | Ruta | Commit |
| --- | --- | --- | --- | --- |
| T1 | Firmware: `GET /api/calendar/month`, cuerpo en PSRAM, `id`/`etag`, JSON propio, tests de host | S2a | firmware | `feat(calendario): el equipo sirve un mes entero` |
| T2 | Tray: grilla de mes con navegación, con el lenguaje del diseño | S1 | tray | `feat(tray): la grilla del mes, con navegación` |
| T3 | Firmware: `POST /api/lcd` y `POST /api/led` públicos, exactos; tests de `admin_auth` | S3 | firmware | `feat(api): brillo y LED sin sesión, y sólo eso` |
| T4 | Tray: controles de brillo y LED | S3 | tray | `feat(tray): brillo del LCD y LED desde el popover` |
| T5 | Tray: la cabecera dice BUDDY | S4 | tray | `fix(tray): el título no dice "bandeja"` |
| T6 | Firmware: scope de escritura, `id`/`etag` en el modelo, endpoints de alta/edición/baja, errores de Google | S2b | firmware | `feat(calendario): escribir eventos en Google` |
| T7 | Tray: editor de eventos (alta, edición, baja) con los errores en español | S2b | tray | `feat(tray): editar eventos desde el mes` |
| T8 | Docs: README del tray, README del equipo y este documento | S1-S4 | docs | `docs(calendario): el mes y la edición en el papel` |

## Log

L1 (pedido original, verbatim):

> Necesito que en el buddy tray pueda
>
> 1.- Tener una vista de todo el mes y navegar entre meses, conservando el lenguaje de diseño, ver los eventos y editarlos (si hay limitaciones de lo que puede hacer la API me avisas...)
> 2.- Controlar el brillo del LCD y del Led RGB sin autenticación
> 3.- En el titulo del tray donde dice "BUDDY" vamos a sacar el texto que dice "Bandeja"
> 4-

L2 (respuestas del dueño a la consulta previa): no hay punto 4, la lista termina
en el 3; el calendario va con **mes + edición completa** (scope de escritura y
re-consentimiento incluidos); el brillo y el LED van por **endpoint público
angosto**, no publicando `/api/settings`.

L3 (evidencia, límites encontrados antes de escribir): `calendar_events.cpp`
calcula `dayStart`/`dayEnd` como hoy y mañana, y `calendar_model.h` fija
`CALENDAR_MAX_EVENTS 24`; `calendar_forms.h` pide
`https://www.googleapis.com/auth/calendar.readonly` y ya trae `items(id,...)` en
`fields`; `CalendarEvent` no guarda ese id; `admin_auth.cpp` deja públicas sólo
`/api/focus`, `/api/screen` y `/api/face`; `net.cpp` protege todo `/api/calendar`.

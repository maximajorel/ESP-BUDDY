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
| T1 | Firmware: `GET /api/calendar/month`, cuerpo en PSRAM, `id`/`etag`, JSON propio, tests de host | S2a | firmware | `31f8ab5` `feat(calendario): el equipo sirve un mes entero` |
| T2 | Tray: grilla de mes con navegación, con el lenguaje del diseño | S1 | tray | `2da71ef` `feat(tray): la grilla del mes, con navegación` |
| T3 | Firmware: `POST /api/lcd` y `POST /api/led` públicos, exactos; tests de `admin_auth` | S3 | firmware | `0f162f2` `feat(api): brillo y LED sin sesión, y sólo eso` |
| T4 | Tray: controles de brillo y LED | S3 | tray | `84d3145` `feat(tray): brillo del LCD y LED desde el popover` |
| T5 | Tray: la cabecera dice BUDDY | S4 | tray | `0fb3abd` `fix(tray): el título no dice "bandeja"` |
| T6 | Firmware: scope de escritura, `id`/`etag` en el modelo, endpoints de alta/edición/baja, errores de Google | S2b | firmware | `feat(calendario): escribir eventos en Google` |
| T7 | Tray: editor de eventos (alta, edición, baja) con los errores en español | S2b | tray | `feat(tray): editar eventos desde el mes` |
| T8 | Docs: README del tray, README del equipo y este documento | S1-S4 | docs | `docs(calendario): el mes y la edición en el papel` |

### Cortes de revisión

La rama `feat/calendario-mes` nace de `feat/tray-buddy`, que no está mergeada: el
slice contra `origin/main` arrastra las ~8.000 líneas de esa rama anterior y no
sirve como medida de esta feature. Los cortes que sí importan son los commits de
arriba, cada uno con su verificación. T1 es el más grande (786 líneas de
firmware + tests) y no se puede partir sin dejar el endpoint en un estado que no
compila; T6 es el segundo por el mismo motivo.

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

L4 (evidencia, toolchain): el firmware compila desde WSL con
`/mnt/c/Program Files/Arduino CLI/arduino-cli.exe` y rutas Windows; el tray con
`/mnt/c/Users/Máximo Majorel/.cargo/bin/cargo.exe` (no hay `cargo` en WSL). Las
rutas Windows con `á` rompen `make verify-assets`, así que ese target va con rutas
relativas. Línea base antes de tocar nada: `make test` (42 checks) y
`cargo test` (41).

L5 (decisión de diseño del mes): el mes no se guarda en el equipo. La cara vive de
una ventana fija de 24 eventos en RAM interna; el mes se pide a Google en el
momento, se dibuja y se tira. El cuerpo (hasta 200 eventos) y el árbol JSON van a
PSRAM con `heap_caps_malloc(MALLOC_CAP_SPIRAM)`, que es lo que ya usan el
framebuffer y el arte de Música. `id` es obligatorio en el parse del mes: un
evento que nadie puede direccionar no se muestra.

L6 (decisión de seguridad): en vez de publicar `/api/settings` se agregaron dos
endpoints angostos, `/api/lcd` y `/api/led`, con comparación exacta de método y
ruta. El test de `admin_auth` fija además que `/api/settings` (con y sin barra)
siga protegido.

L7 (decisión sobre el permiso): el scope pasa a `.../auth/calendar`. Un permiso
viejo (de sólo lectura) sigue leyendo y da 403 al escribir, así que el equipo
recuerda si el permiso guardado puede escribir (`wrScope` en NVS) y el tray dice
"reconectá la cuenta en el panel" antes de que el dueño pierda el evento. El 403
de Google limpia esa marca para no repetir el intento perdido.

L8 (pendiente anotado, no hecho): `docs/ARCHITECTURE.md` tiene cambios sin commitear
de otra tanda (clima) y su mapa de endpoints no menciona los seis nuevos. Se dejó
sin tocar a propósito para no mezclar dos features en un commit; la tabla de
endpoints nuevos quedó documentada en `esp32_gadget/README.md`.

L9 (corrección por verificación independiente): la verificación de `gentle-ai-verify`
encontró un bloqueante real en la escritura: un `etag` vacío se mandaba sin
`If-Match`, así que Google escribía igual y el cambio hecho en el teléfono se
perdía. El manejo del 412 existía pero era inalcanzable por ese camino. Arreglado
en `cd6daec`: `calendarEtagUsable` (pura, exige la forma con comillas) y un 428
`Precondition Required` con la frase "recarga el mes y volve a intentar"; el mismo
commit fija los verbos no públicos de `/api/lcd` y `/api/led`, que la verificación
también marcó como caso faltante. PASS en el resto: la superficie pública sigue
siendo exacta, los dos handlers nuevos no escriben nada más que brillo y LED, y el
permiso de sólo lectura se niega antes de tocar la red.

L10 (lo que falta para decir que la edición funciona): la prueba de punta a punta
contra Google no se hizo y no se puede hacer desde acá. Requiere (a) flashear la
placa en COM5 —está conectada y enumerada, pero no hay autorización de sesión para
escribirle— y (b) reconectar la cuenta en el panel del equipo, porque el permiso
guardado es de sólo lectura. Sin las dos, lo verificado es el contrato: los bytes
que salen, la tabla de permisos y que el firmware compila.

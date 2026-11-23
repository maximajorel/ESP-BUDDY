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

L11 (cierre de las puertas por comando): `make test` 44 checks, `make
verify-assets` PASSED y `make build` MAKE_EXIT=0 (51% de flash) con
`ARDUINO_CLI` apuntando al ejecutable de Windows —ojo: `WEB_UI`/`WEB_ASSETS` hay
que pasarlos explícitos, porque el Makefile los deriva de `SKETCH` y una ruta
Windows con `á` rompe el verificador—. `cargo test` 51 y `admin_auth_test` 164.

L12 (no verificado, y por qué): la interfaz del tray no se vio renderizada. El
servidor de Chrome del entorno no responde (`Network.enable timed out`) y no hay
binario de Chrome/Chromium en WSL, así que el popover quedó verificado por lectura
y por `node --check`, no por pantalla. Lo que un render probaría —grilla de 42
celdas en 380 px, editor abierto, sliders— sigue sin evidencia visual.

L13 (la interfaz sí se vio, y encontró un defecto): el popover se renderizó de
verdad sin la placa: una copia de `tray_buddy/ui/` en un directorio temporal, un
`stub.js` que reemplaza `window.__TAURI__.core.invoke` por respuestas fijas del
equipo (un mes de noviembre 2026 con tres eventos, `display.brightness=55`,
`rgb.level=200,effect=1`) y que se maneja solo con clics y esperas, y Chrome
headless del lado de Windows con `--dump-dom` (para leer el reporte) y
`--screenshot` (para mirarlo). El servidor va en 0.0.0.0 y se llega por la IP de
WSL; `--window-size` tiene un mínimo de ventana en Windows, así que la captura
buena es a 560 px de ancho.

Resultado: **42 comprobaciones, 0 fallas**, y una captura del popover con la
grilla de noviembre, los días del mes vecino atenuados, el 23 marcado como hoy,
las marcas de evento en 21 y 28, el día elegido listado abajo, los dos sliders con
"SIN SESIÓN" y el título `BUDDY`.

Encontró un defecto real que los tests no podían ver: al tocar un chip de efecto,
`renderLight()` volvía a leer `status` y pisaba el valor recién elegido, así que
el equipo recibía el efecto anterior **y** el nivel anterior (el del último
sondeo). Arreglado en el commit `fix(tray): el chip de efecto manda lo que el
          dueño eligió` separando pintar de adoptar: `renderLight()` sólo
pinta `light`, `adoptLight()` es lo único que lee `status`, y no adopta mientras
hay una escritura en vuelo (`lightPending`). El arnés se reescribió con dos
comprobaciones nuevas (el chip manda su efecto; no pisa el nivel recién elegido) y
volvió verde.

El arnés quedó fuera del repo a propósito: es una copia temporal con un stub, no
un artefacto del producto, y meterlo obligaría a mantener un Chrome y un script
por plataforma. Queda como receta acá, y como candidato a herramienta si la
interfaz del tray crece.

L14 (el presupuesto del mes, medido): el objetivo pide "buffer sizing in the
calendar event builder" y el mes no lo tenía. Agregado a `calendar_month_test`:
arma el peor cuerpo que Google podría mandar (150 eventos con id, título y lugar
largos, start/end con zona) y el peor payload que el equipo podría devolver.

Números medidos: cuerpo del peor caso **123.311 B** contra un presupuesto de
**157.696 B**; payload del peor caso **71.686 B** contra un techo de **98.304 B**.

La medición forzó dos correcciones que no eran cosméticas:
- `CALENDAR_MONTH_MAX_EVENTS` **200 → 150**. Con 200 el payload del peor caso daba
  89.136 B: 7 KB de margen contra un techo que se arma en `String` sobre RAM
  interna. El número no es redondo a propósito: 150 es el tope más grande cuyo
  peor caso todavía tiene margen, y el test falla si eso deja de ser cierto.
- `CALENDAR_EVENT_ID_MAX` **128 → 160**, y el parse ahora **descarta** un id que
  no entra en vez de recortarlo. Google arma los ids de instancia recurrente como
  `<id>_<YYYYMMDD>T<HHMMSS>Z`, así que un id base largo pasa los 128; recortado
  direcciona otro evento (o ninguno) y el dueño se enteraría recién al guardar,
  con un "ese evento ya no existe" que no explica nada. El costo es que un evento
  con un id imposible no se muestra; con 160 ese caso es prácticamente inexistente.

L15 (el sondeo del mes no era posible, y eso importaba): la verificación que queda
pendiente necesita la placa, y la única vía sin sesión del panel es la consola
serie. Ahí `api GET /api/calendar/month?y=2026&m=11` contestaba **404**: el
dispatcher compara la ruta exacta y la consola pega la query en la ruta, mientras
que el servidor HTTP la corta en `uri()`. O sea que el endpoint del mes sólo se
podía probar abriendo una sesión —justo lo que no se puede hacer en el sondeo—.

Arreglado en el dispatcher: separa la query de la ruta antes de comparar
(`net_query.h`, puro y con `tools/net_query_test.cpp`) y `apiCalendarMonth` la lee
de ahí cuando existe, cayendo a `s_server.arg` en HTTP. Efecto lateral bueno:
cualquier endpoint con parámetros queda sondeable por consola, y la comparación de
rutas sigue siendo exacta (una query nunca es parte de una ruta).

Queda `tools/probes/calendario_mes.txt`: arranque, los tres meses (bordes
incluidos), el mes sin parámetros (tiene que ser 400, no un mes inventado), los dos
`POST` públicos de brillo y LED, y la lectura de vuelta en `/api/status`. Es la
secuencia que convierte la verificación bloqueada en un comando de una línea:

    python tools\gadget_probe.py @tools\probes\calendario_mes.txt

L16 (verificación en la placa, con el dueño habilitando el flasheo): `make flash
PORT=COM5` → `Hash of data verified.`, 51% de flash, RAM estático igual.

Sondeo por consola (`tools/probes/calendario_mes.txt` y `calendario_escritura.txt`):

| Qué | Resultado medido |
| --- | --- |
| `GET /api/calendar/month?y=2026&m=11` | **200**, 44 eventos reales, `gridStart` 1792983600 (lunes) y `gridEnd` 1796612400 (42 días) |
| `?y=2026&m=12` | **200**, 57 eventos, instancias recurrentes con id de ~45 chars y `etag` |
| `?y=2030&m=1` | **200**, 4 eventos: el rango llega más allá de hoy |
| `?y=2026` y sin parámetros | **400** "Falta el mes": no inventa un mes |
| `?y=2026&m=13` | **400** por rango validado |
| `POST /api/lcd {"brightness":70}` | **200**, y `/api/status` pasa de `brightness: 15` a `70`; después 55 |
| `POST /api/led {…,"level":128,"effect":1}` | **200**, y `/api/status` pasa de `level: 51` a `128` |
| `POST /api/led {"effect":9}` | **400** "Efecto de LED invalido" |
| `POST /api/calendar/events` (permiso viejo) | **403** "El permiso guardado es de solo lectura: reconectá la cuenta en el panel para poder editar" y **no creó nada** |
| `POST /api/calendar/events/delete` sin etag | **428** "Falta la version del evento: recarga el mes y volve a intentar" |
| `GET /api/calendar/events` (el día) | **200**, `dayStart`/`dayEnd` y los 5 eventos de siempre: el payload viejo sigue intacto en la placa |

`psramFree` se mantuvo en ~8.14 MB a través de tres pedidos de mes (8140564 →
8145088 → 8147704): el allocator de PSRAM libera todo. El heap interno bajó a
59 KB durante la respuesta de 57 eventos y volvió a 102 KB.

Dos defectos que sólo se ven con la placa, corregidos:
- El primer pedido de mes después de un reset contestaba `502 {"error":"Google no
  respondio (-1)"}`: un código de transporte en la cara del dueño y ningún
  reintento. Ahora reintenta una vez con 500 ms de espera (el fallo medido es el
  enlace débil, −85 dBm, asentándose) y el mensaje es "Google no respondio: proba
  de nuevo en unos segundos", con el código sólo en el puerto serie.
- `gadget_probe.py` cortaba el `drain` a los 8 s y un pedido de mes con reintento
  puede tardar dos timeouts de TLS: el comando siguiente se escribía con la placa
  ocupada y su respuesta se perdía. Techo a 20 s (vuelve igual en cuanto hay
  silencio).

Lo único que queda de S2b es el alta → lectura → edición → baja reales, y no
depende de mí: el permiso guardado es el viejo y hay que **reconectar la cuenta en
el panel** una vez. La secuencia ya está escrita (`calendario_escritura.txt`) y
falla hoy en el primer paso con la frase correcta, que es justo lo que se quería.

L17 (última pieza del instrumento): la consola leía líneas de 128 bytes y el
cuerpo de una **edición** no entra: `{"id":"…","etag":"\"…\"","summary":"…",
"startEpoch":…,"endEpoch":…}` pasa los 130 con el prefijo del comando. Con 128 el
`api POST` se truncaba y el equipo contestaba "Los datos enviados no son validos":
correcto, pero dejaba la edición sin poder probarse sin abrir sesión del panel
—justo lo que el sondeo no puede hacer—. `s_line` pasa a 256 (128 bytes de RAM
estática) y el sondeo de escritura quedó completo, con el `id` y el `etag` del
paso de lectura para completar los dos últimos.

Estado medido hoy, con el firmware ya en la placa: el alta sigue contestando
**403 "El permiso guardado es de solo lectura: reconectá la cuenta en el panel"**,
o sea que la reconexión de Google todavía no ocurrió. Es el único paso que queda y
necesita al dueño con el navegador.

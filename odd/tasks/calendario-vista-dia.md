# Calendario del buddy tray: la vista del día, completa y honesta

La tarjeta de calendario del popover ya pide los eventos (`GET /api/calendar/events`,
feature `calendario-agenda`), pero la lista que dibuja **no es "hoy"**: el equipo
consulta a Google una ventana de −1 h a +26 h, así que los eventos de mañana se
muestran como si fueran de hoy, sin etiqueta, y los de esta mañana que ya
terminaron no llegan a existir porque la ventana arranca recién una hora atrás.

## Specs

S1. Pedido del dueño, verbatim:

"Quiero continuar con la app de buddy tray, sobre todo me quiero enfocar que
funcione bien el tema del calendario. Podemos arrancar de ultima si es mas
sencillo para la vista del día, ver los eventos que tengo hoy.

La integracion con Google Calendar ya esta realizada en la WebUI, por lo que
podemos usar la información de allí."

S2. Alcance elegido por el dueño en el cuestionario de esta feature, verbatim:
"El día completo + mañana etiquetada" — "Hoy entero (lo ya pasado atenuado), y
abajo una sección 'Mañana' con los eventos del resto de la ventana de 26 h, bien
rotulada en vez de mezclada." Queda **fuera**: el mes (`Ver el mes` sigue
deshabilitado) y crear eventos.

S3. El equipo pide los eventos desde la **medianoche local de hoy** (no desde
"hace una hora") hasta `now + 26 h`, y retiene hasta **24** eventos en RAM para
que un día cargado no se corte. La reacción de la cara no cambia: un evento
terminado nunca la mueve (`calendarInWindow` exige `now <= min(end, start+grace)`
y `calendarStatusInfo` sigue ignorando lo que empezó hace más de una hora).

S4. `GET /api/calendar/events` suma `dayStart` y `dayEnd`: epoch UTC de la
medianoche local de hoy y de mañana, calculados por el equipo con su propio
offset. La app **no** deriva la medianoche de su reloj. Compatibilidad: contra un
firmware viejo (sin los campos) la app los deriva de `tz`, y si el equipo
directamente no publica el endpoint, la tarjeta igual muestra la próxima cita y
pide actualizar el firmware en el renglón de la agenda.

S5. La tarjeta dibuja dos secciones rotuladas: **HOY** con todos los eventos de
hoy (los ya terminados atenuados, un evento de todo el día rotulado "todo el
día") y **MAÑANA** con el resto de la ventana. Ningún evento de otro día puede
aparecer dentro de "Hoy", y el orden es siempre el que manda el equipo.

S6. Con más eventos de los que entran, el cuerpo del popover **scrollea**; nada
se sale del rectángulo de la ventana (el sistema lo recortaría). Sin sesión, sin
autorización o sin eventos, la tarjeta dice lo mismo que hoy: no se inventa nada.

## Tasks

| ID | Spec | Ruta | Commit |
|----|------|------|--------|
| D1 | S3 | worker (firmware: ventana desde la medianoche local, 24 eventos, con test de host) | `430b51e` |
| D2 | S4 | worker (firmware: `dayStart`/`dayEnd` en el JSON, con test de host) | `37d1dc3` |
| D3 | S4 | worker (app: parseo y compatibilidad, con test de Rust) | `8e6ee63` |
| D4 | S5, S6 | worker (app: secciones Hoy/Mañana, atenuado y scroll) | `8e6ee63` |
| D5 | S5, S6 | verificación: tests de host, compilación de placa, `cargo test`, capturas CDP y app real | este documento |

## Log

L1. Pedido del dueño (verbatim en S1) y decisión de alcance en S2.

L2. Relevamiento previo (leído, no supuesto): `calendar.cpp` usa
`kPastSeconds = 3600` y `kFutureSeconds = 26*3600`, con `CALENDAR_MAX_EVENTS 12`;
`renderAgenda` (`tray_buddy/ui/app.js`) dibuja la lista entera sin filtrar ni
etiquetar días, y el `tz` que llega se usa sólo para la hora. El `daytag` existe
en el CSS pero sólo lo usa la tarjeta de "próxima cita".

L3. D1 cerrado. Test primero: `calendar_core_test` falló en rojo por
`calendarLocalMidnight` inexistente y por `CALENDAR_MAX_EVENTS` 12≠24, y quedó
verde con el helper en `calendar_time.h` (que normaliza el resto de C para las
fechas antes de 1970) y `make build` cambiado a 24 slots. El piso de la ventana
es una **hora atrás** cuando la medianoche está más cerca, porque Google filtra
por hora de inicio y un evento que empezó ayer y sigue corriendo tiene que estar
en la respuesta: `calendarDayQueryStart`, con sus dos casos en el test
(`09:48 → 00:00 local`; `00:01 → ahora−1 h`). Evidencia: `python3
tools/run_tests.py` → **42 passed**; `arduino-cli compile` con el FQBN del
`Makefile` → exit 0, 1.611.218 bytes (51% de flash) y **79.316 bytes de globales
(24% de RAM)**, 4 puntos más que antes por los 12 eventos extra en las dos copias
(viva y de parseo).

L4. D2 cerrado. El firmware publica `dayStart`/`dayEnd` desde el mismo helper, y
manda **ceros** cuando el reloj no vio NTP todavía: una medianoche de 1970 sería
una mentira con forma de dato. `calendar_events_test` fija los tres casos (hoy en
−03, +05 y UTC), el caso del reloj sin sincronizar y que el resto del contrato no
cambió.

L5. D3 cerrado. `cargo test` → **26 passed** (25 de antes + el caso del equipo
viejo sin `dayStart`/`dayEnd`). El campo viaja en camelCase como el resto del
contrato.

L6. D4 cerrado y verificado en tres capas.
- **Arnés de navegador** (stub de `window.__TAURI__`, fixtures de un día real,
  reloj fijo en 2026-11-23 10:20 −03) sobre el `ui/index.html` real, con Chrome
  headless. Escenarios `day`, `tomorrow`, `old` (firmware sin `dayStart`),
  `empty`, `unauthorized`, `session` y `busy` (24 eventos): el HTML resultante
  muestra `daytag: Hoy` con el evento de todo el día, el de 08:00 con
  `agenda-row is-past`, el de 15:00 normal y `daytag: Mañana` con el de mañana;
  `#old` da **exactamente** el mismo resultado que `#day`, que es la
  compatibilidad de S4; `calMeta` dice `4 hoy`, `sin Google`, `sin eventos hoy`
  o `privado` según el caso.
- **Desborde medido, no estimado**: a 520x820 y a 388x660, con 5 y con 24 filas,
  `outsideCount: 0` (ningún descendiente del cuerpo se sale del rectángulo) y
  `body.scrollHeight` 1727 > `clientHeight` 611 en el caso lleno, con
  `overflow-y: auto`: el cuerpo scrollea, nada se recorta.
- **App real**: WebView2 de la app compilada, con `Page.captureScreenshot` por
  CDP (`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port`) y los
  mismos datos inyectados en las funciones reales del popover: renderiza HOY con
  "todo el día", el evento pasado atenuado y el rótulo MAÑANA. Captura en
  `%TEMP%\agenda-app-real.png`.

L7. Defecto encontrado al verificar contra la placa real (no en el arnés): con un
equipo que no tiene el endpoint `GET /api/calendar/events` (firmware viejo, que
es justo lo que hay flasheado hoy), `loadCalendar` descartaba la respuesta buena
de `/api/calendar` porque la segunda llamada fallaba, y la tarjeta entera se caía
a "sin datos". Se separaron los dos pedidos: `renderCalendar` con lo que llegó y
`renderAgenda` con su propio motivo, y un 404 se traduce a "Actualizá el
firmware del equipo para ver la agenda del día." en vez de mostrar el texto crudo
del firmware. Verificado en la app real contra la placa: la tarjeta muestra la
próxima cita que informa el equipo y la agenda pide actualizar el firmware.

L8. Estado de la placa al cerrar esta entrega (leído, con la sesión del panel que
la app ya tenía): `GET /api/calendar` → `kind:"ready"`, `authorized:true`,
`selectedName:"Tareas Personales"`, `calendarCount:9`, `eventCount:0`,
`hasNext:false`; `GET /api/calendar/events` → `{"ok":false,"error":"No existe ese
recurso"}`. Es decir: la integración con Google está viva y lo único que falta
para ver la agenda es flashear el firmware de L3/L4. **Pendiente con el dueño:
autorización explícita para flashear** (regla del skill `esp32-gadget-flash`),
puerto COM por leer de `board list`, y después la prueba de la agenda con sus
eventos reales.

L9. El mock de la API (`tools/mock_gadget_api.py`) ganó `GET
/api/calendar/events` con datos relativos al día del equipo, para poder ejercitar
la agenda sin la placa (`f899b80`). Ese archivo ya traía, sin commitear, el WIP
del clima de otra feature: el commit se armó **sobre HEAD y sólo con los cambios
de la agenda** (`git hash-object -w` + `update-index --cacheinfo`), y el WIP
ajeno sigue sin commitear como estaba. Verificado corriendo ese blob exacto en
un servidor aparte: sin sesión → 401; con sesión y la cuenta aprobada →
`count: 5` con `dayStart`/`dayEnd` coherentes y el `eventCount` de
`/api/calendar` saliendo del mismo día. El "aprobar a los 4 s" del mock pasó a
`calendar_tick()`, compartido por los dos endpoints: antes sólo lo hacía
`/api/calendar` y la agenda contestaba "sin eventos" con el estado ya en `ready`.

L10. `tools/probes/calendario_agenda.txt` (`2aa2e9c`): la secuencia de consola
que verifica el endpoint después de flashear, con la fila correspondiente en
`tools/probes/README.md`. `api` llama a `netHandleApi` en proceso y no pasa por
`adminRequiresSession`, así que el JSON se puede leer sin abrir sesión del panel.
Lectura de puerto para el próximo paso: `arduino-cli board list` → **COM5**
(`ESP32 Family Device`); COM3/COM4 sin placa identificada.

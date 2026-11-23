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

S8. El primer sync después de arrancar se reintenta a los **15 s**, no a los
dos minutos: el equipo tarda en tener la agenda y el popover se abre en ese
mismo momento. Agotados tres intentos rápidos sin un sync bueno se vuelve al
ritmo lento de dos minutos, para que una credencial mal escrita no golpee a
Google; con un sync bueno, quince minutos.

S7. Las secciones **y la tarjeta de próxima cita** se rotulan por el día que son,
no por su posición: a las 23:50
la ventana de 26 h entra al día siguiente al de mañana y esa sección no puede
decir "Mañana". Hoy → "Hoy", el día siguiente → "Mañana", cualquier otro día →
su día de la semana en minúscula (el CSS lo pone en mayúsculas). La tarjeta usa
el mismo rótulo para el evento de todo el día que anuncia, así la lista y la
tarjeta no pueden decir días distintos del mismo evento. Un evento que
arrancó antes de la medianoche local y sigue abierto es de hoy; uno que terminó
antes de esa medianoche no se lista.

## Tasks

| ID | Spec | Ruta | Commit |
|----|------|------|--------|
| D1 | S3 | worker (firmware: ventana desde la medianoche local, 24 eventos, con test de host) | `430b51e` |
| D2 | S4 | worker (firmware: `dayStart`/`dayEnd` en el JSON, con test de host) | `37d1dc3` |
| D3 | S4 | worker (app: parseo y compatibilidad, con test de Rust) | `8e6ee63` |
| D4 | S5, S6 | worker (app: secciones Hoy/Mañana, atenuado y scroll) | `8e6ee63` |
| D6 | S7 | worker (app: secciones rotuladas por día + `tools/preview.js` en el repo) | `b0dae4a` |
| D7 | S7 | worker (app: la tarjeta de próxima cita usa el mismo rótulo) | `86357f5` |
| D8 | S8 | worker (firmware: el ritmo de reintento del sync, con test de host) | `fed833d` |
| D5 | S5, S6, S7 | verificación: tests de host, compilación de placa, `cargo test`, capturas CDP y app real | este documento |

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

L10bis. Auditoría del estado commiteado, con la suite sobre un `git archive HEAD`
fuera del árbol de trabajo: **41 passed** (los tres tests de calendario
adentro; el único que falta respecto del árbol es `weather_core_test`, que
pertenece al WIP del clima sin commitear). El firmware **no** compila desde HEAD
solo, y **no es por esta feature**: `esp32_gadget/net.cpp` (ya commiteado) incluye
`weather.h` y compañía, y todo el módulo `weather*` está sin trackear
(`?? esp32_gadget/weather.*`). Es decir, una copia limpia de la rama no compila
hasta que ese WIP se commitee; el árbol de trabajo sí compila (51% flash, 24%
RAM) y es lo que se flashea. Queda anotado, no se toca: no es de esta feature y
commitear trabajo ajeno no es una decisión del agente.

L10ter. Defecto encontrado al auditar la suba a 24 eventos: el presupuesto del
cuerpo seguía en 16384 bytes y `readBody` **descarta el cuerpo entero** cuando se
pasa, así que un día cargado con títulos largos dejaba al dueño sin agenda (no
con menos filas). El peor caso que manda Google con los campos pedidos —título y
lugar de 200 caracteres, `start`/`end` con zona y dos recordatorios por evento—
se midió en el test: **18.731 bytes**, más de lo que entraba. El presupuesto ahora
se deriva del buffer en `calendar_model.h` (`4096 + 24 * 1024 = 28.672`) y
`tools/calendar_core_test.cpp` construye ese peor caso, lo imprime, lo parsea
entero y falla si deja de entrar (`76aef9f`). Compila para la placa: 51% de flash
y 24% de RAM. **Sin medir en el vidrio**: el pico de heap con un cuerpo de 28 KB
(la `String` del transporte más el documento de ArduinoJson) queda fuera de esta
entrega porque exige flashear; si al empujar el equipo aparece presión de heap,
el orden de ataque está en `calendar_http.cpp` (`readBody` copia el cuerpo dos
veces).

L14. **Primera verificación en la placa después de flashear** (COM5, `Hash of data
verified`): `api GET /api/calendar/events` → `200 {"ok":true,"tz":-3,
"dayStart":1795402800,"dayEnd":1795489200,"count":0,"events":[]}`, con las dos
medianoches exactas (23 y 24 de noviembre a las 00:00 −03). El endpoint nuevo
funciona en el equipo.

L17. Verificado en el equipo con el evento **real del dueño**: creó "🧪 Evento de
prueba ESP32" para hoy y la sonda devolvió
`{"ok":true,"tz":-3,"dayStart":1795402800,"dayEnd":1795489200,"count":1,
"events":[{"summary":"🧪 Evento de prueba ESP32","startEpoch":1795453200,
"endEpoch":1795456800,"leadMin":30,"allDay":false}]}` — 14:00 a 15:00 local,
dentro de la ventana del día, y `/api/calendar` con `hasNext:true` y
`nextSummary` con ese mismo título. La cadena Google → equipo → JSON de la agenda
funciona con datos reales. El popover quedó abierto (PID de la corrida del
dueño); el último tramo —verlo en pantalla— pide la sesión del panel, que sólo el
dueño puede abrir.

L18. La sonda de consola moría con los títulos que tienen emoji: `print` contra
la consola cp1252 de Windows lanzaba `UnicodeEncodeError` justo después de traer
la data. Arreglado con `reconfigure(encoding="utf-8", errors="replace")`
(`0d92f04`). No era un defecto del firmware.

L19. Sobre el primer sync fallido: **es intermitente**, no sistemático. Tres
arranques seguidos sincronizaron bien, con el primer sync bueno a los ~25 s
(esperando NTP: `clockReady()` bloquea antes) y sin línea de error. Cuando falla,
es de transporte (`status 0` o cuerpo vacío) y el reintento de 15 s lo levanta.
Para el próximo caso quedó diagnóstico permanente en el puerto serie (`97f968d`):
`calendar_http.cpp` imprime método, host/path, código, error de transporte, TLS y
bytes de la petición que falló (cubre token, lista y eventos), y
`calendar_auth.cpp` agrega los primeros 80 bytes del cuerpo del token. Causa raíz
**abierta**, con la evidencia lista; no se toca más sin un caso que la muestre.

L16. D8 verificado en el equipo, ya flasheado: a los 45 s de arranque
`lastSyncMs: 27671` (el sync bueno llegó a los **27,7 s**; con la política vieja
había llegado a los 129 s), `lastError:""`, `calendarCount:9` y la lista real de
calendarios (Tareas Personales —el elegido, primary—, Trabajo, Facultad,
Formula 1, PROGRAMACION 3, INGLES 2, Bases de Datos II, Festivos en Argentina,
Calendario Conjunto). `eventCount: 0` y `count: 0` en la agenda: el calendario
elegido no tiene eventos entre la medianoche local del 23 y el 24 a las 12:47 —
dato real, no un defecto. Si el dueño quiere ver otra cosa ahí, cambia el
calendario en el panel (los otros ocho están disponibles).

L15. Pero la primera lectura mostró `lastError:"Google rechazo la autorizacion"`,
`lastSyncMs:0`, `calendarCount:0`: el **primer intento de sync después de
arrancar falla** y el reintento automático a los 120 s lo arregla (verificado:
a los 129 s → `lastError:""`, `calendarCount:9`, `lastSyncMs:129069`). La causa
del primer fallo no está confirmada: el mensaje es el de una respuesta de token
**ilegible** (`calendarParseTokenResponse` con cuerpo no-JSON), que apunta al
primer intercambio TLS con el WiFi asentándose, no a un permiso revocado —
Google contestó bien al reintento. Síntoma mitigado, no causa raíz: la política
de reintento se extrajo a `esp32_gadget/calendar_schedule.h` con sus casos en
`tools/calendar_core_test.cpp` (S8, D8) y ahora el equipo recupera la agenda a
los 15 s en vez de a los 120 s.

L13. La misma clase de defecto seguía viva en la tarjeta: con la próxima cita
siendo un evento de todo el día de pasado mañana (escenario `after-tomorrow`,
reloj a las 23:50), la tarjeta decía "mañana" mientras la lista decía "miércoles"
—el mismo dato con dos respuestas—. `dayLabel()` quedó como el único lugar que
decide el rótulo (hoy / mañana / día de la semana) y lo usan las dos superficies;
un evento de todo el día que ya está corriendo sigue siendo "hoy" por
solapamiento (`86357f5`). Verificados los diez escenarios del arnés: `after-tomorrow`
→ "miércoles", y sin cambios en day, tomorrow, old, empty, unauthorized, session,
busy, multiday y late-night.

L12. Defecto encontrado al revisar la ventana de 26 h: **a la noche la sección
"Mañana" miente**. A las 23:50, `now + 26h` cae en el día siguiente al de mañana,
así que un evento de pasado mañana se listaba bajo "Mañana" — el mismo error que
esta feature vino a arreglar, un día más lejos. Reproducido primero en el arnés
(escenario `late-night`: "Trámite pasado mañana" bajo "Mañana") y después
corregido (S7, `b0dae4a`): los eventos se agrupan por día local, el grupo de hoy
es "Hoy", el siguiente "Mañana" y el resto lleva su día de la semana, calculado
del número de día local sin `Date` ni zona. Un evento que terminó antes de la
medianoche local ya no abre un grupo "ayer": se descarta. Verificados los nueve
escenarios por DOM (day, tomorrow, old, empty, unauthorized, session, busy,
multiday, late-night → "miércoles"). El arnés quedó en el repo como
`tray_buddy/tools/preview.js`, así la próxima vez se reproduce con un comando.

L11. Borde verificado en el arnés (escenario `multiday`, que no estaba en L6):
un evento de todo el día que arrancó ayer entra en HOY como "todo el día"; uno
de 22:00 de ayer a 02:00 de hoy entra en HOY, atenuado, con su hora real; uno de
23:00 de hoy a 02:00 de mañana entra en HOY y es la próxima cita; y el de mañana
a las 09:00 queda en MAÑANA. `calMeta` cuenta los tres que tocan hoy. La regla es
solapamiento con el día local, no igualdad de fecha.

L10. `tools/probes/calendario_agenda.txt` (`2aa2e9c`): la secuencia de consola
que verifica el endpoint después de flashear, con la fila correspondiente en
`tools/probes/README.md`. `api` llama a `netHandleApi` en proceso y no pasa por
`adminRequiresSession`, así que el JSON se puede leer sin abrir sesión del panel.
Lectura de puerto para el próximo paso: `arduino-cli board list` → **COM5**
(`ESP32 Family Device`); COM3/COM4 sin placa identificada.

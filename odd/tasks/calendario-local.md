# Google Calendar en el firmware: credenciales, selector y ojos que reaccionan

El calendario hoy lo resuelve el bridge: el equipo le pregunta a otro servidor y
ni siquiera guarda credenciales de Google (`buddy_calendar.*`). Esta etapa lo
mueve adentro del firmware: la placa habla con Google, guarda sus credenciales,
deja elegir el calendario desde el panel y hace reaccionar los ojos según el
recordatorio del evento.

## Specs

S1. Dependencia — verbatim: "Necesito finalizar la integracion del google
calendar con el ESP, me gustaria que sea dependiente solo del firmware, no
necesariamente del otro proyecto que es el bridge que corre en el otro
servidor". El calendario funciona sin bridge configurado: la placa hace el flujo
OAuth, guarda el refresh token y consulta la API de Google por su cuenta. El
bridge sigue existiendo para la cabeza de Sofía y el resto de Hermes, pero no
participa del calendario. La tarjeta de calendario del panel deja de proxear
`/calendar` al bridge.

S2. Credenciales en el panel — verbatim: "Quiero que en la web UI pueda
configurar las credenciales de mi google calendar". El panel (autenticado)
permite pegar Client ID y Client secret, guardarlos en NVS, conectarlos con
Google y olvidarlos. Los secretos nunca se muestran de vuelta ni viajan en la
URL: el campo vacío significa "dejar lo que está".

S3. Conexión fácil — la autorización es el *device flow* de Google, que no pide
redirect URI: el panel muestra un código corto y el enlace
`https://www.google.com/device`; el dueño lo escribe en su cuenta y aprueba, y la
placa obtiene el refresh token sola. Si Google todavía no entregó el token, el
panel dice "esperando" y sigue consultando sin que el dueño haga nada más.

S4. Varios calendarios, elección simple — verbatim: "En mi cuenta de google tengo
varios calendarios, necesito que sea facil seleccionarlo para cualquier persona
sin conocimientos tecnicos". Después de autorizar, el panel lista los
calendarios de la cuenta con su nombre (el principal primero) en un `<select>` y
guarda el elegido. No se pide pegar IDs a mano. Si el calendario elegido
desaparece, el estado lo dice y el selector sigue ofreciendo la lista real.

S5. Instrucciones claras — verbatim: "e instrucciones claras para la
configuracion de la API key". La tarjeta del panel y `INSTALL.md` explican paso a
paso, con los nombres exactos que muestra Google Cloud (habilitar la Google
Calendar API, pantalla de consentimiento, publicar la app, crear el cliente
OAuth de tipo *TVs and Limited Input devices*, pegar ID y secreto), qué hacer si
la app queda en *Testing* (el permiso vence a los 7 días) y cómo revocar. Nada
de "conseguí un refresh token".

S6. Los ojos reaccionan — verbatim: "que los ojos reaccionen si tengo un evento
cerca, acorde al recordatorio que tengo en ese evento en mi calendario". El
aviso previo sale del recordatorio del evento (`reminders.overrides[].minutes`);
si el evento usa los recordatorios por defecto del calendario, se usan esos; si
no hay ninguno, el respaldo configurable del panel. La reacción vive en la
ventana `[inicio - aviso, inicio + gracia]`, escala en el tiempo (ojos que se
abren y se inclinan más a medida que se acerca), muestra el título del evento en
el HUD y pinta la luz violeta. Un evento ya empezado tiene su propia cara de
"ahora". Los estados que piden atención (`thinking`, `tool`, `waiting`, `error`)
no se pisan: el calendario sólo toma la cara cuando no hay algo más urgente.

S7. Sin tocar lo que anda — el calendario no rompe el bridge, Spotify, el sueño
local, el juego ni las pruebas de pantalla. La consulta a Google corre en su
propia tarea, con su propio cliente TLS y su propio mutex, y nunca comparte
socket con el long-poll de Sofía ni con Spotify. El panel sigue pidiendo sesión
para leer o escribir el calendario.

S8. Se puede probar sin la placa — la lógica pura (RFC 3339, recordatorios,
elección del próximo evento, fases de la reacción, parseo de las respuestas de
Google) vive en `.h`/`.cpp` sin `Arduino.h` y se fija en
`tools/calendar_core_test.cpp`. La compilación completa con el FQBN de
`INSTALL.md` §3 tiene que seguir pasando.

## Tasks

| Task | Specs | Ruta | Commit |
| --- | --- | --- | --- |
| T1: documento y trackeo de la etapa | — | inline | `1004573` |
| T2: modelo y lógica pura con pruebas (RFC 3339, parseo, recordatorio, ventana, fases) | S6, S8 | inline | `1004573` |
| T3: credenciales en NVS con validación y claves propias | S1, S2 | inline | `5109fce` |
| T4: transporte HTTPS propio y device flow con refresco de token | S3, S8 | inline | `5109fce` |
| T5: tarea de consulta, snapshot y comando de consola | S1, S4 | inline | `50ffb51` |
| T6: reacción de la cara (`BUDDY_UPCOMING`) y luz violeta | S6, S7 | inline | `50ffb51` |
| T7: endpoints del panel y protección de sesión | S2, S7 | inline | `50ffb51` |
| T8: tarjeta del panel con conectar, selector de calendario e instrucciones | S2, S3, S4, S5 | inline | `e01d201` |
| T9: `INSTALL.md`, `README.md` y `ARCHITECTURE.md` | S5 | inline | docs |
| T10: sacar el proxy de calendario del bridge | S1 | inline | `50ffb51` |
| T11: pruebas de host, compilación del firmware y panel empaquetado | S8 | inline | `50ffb51`, `e01d201` |
| T12: verificación en la placa | S6 | hardware | `803586c`, `3a7b968` |

## Log

L1 — pedido del dueño, verbatim:

> Necesito finalizar la integracion del google calendar con el ESP, me gustaria
> que sea dependiente solo del firmware, no necesariamente del otro proyecto que
> es el bridge que corre en el otro servidor:
>
> Quiero que en la web UI pueda configurar las credenciales de mi google
> calendar y que los ojos reaccionen si tengo un evento cerca, acorde al
> recordatorio que tengo en ese evento en mi calendario.
>
> En mi cuenta de google tengo varios calendarios, necesito que sea facil
> seleccionarlo para cualquier persona sin conocimientos tecnicos, e
> instrucciones claras para la configuracion de la API key.

L2 — estado inicial verificado (21-nov): `buddy_calendar.{h,cpp}` sólo proxea
`GET /calendar` y `POST /calendar*` al bridge con sus dos tokens; el panel
(`web_ui/admin.html:393`) muestra instrucciones del bridge; `BUDDY_UPCOMING`
existe en el enum y en el wire pero `buddyPose()` no tiene su `case` (cae en la
cara de idle) y `drawHud()` no dibuja el evento. La rama base `feat/concentracion`
tiene 33 pruebas de host en verde.

L3 — implementación (21-nov), con las pruebas y la compilación como evidencia:

- **Puro y probado en host**: `calendar_model.h`, `calendar_time.h`,
  `calendar_plan.h`, `calendar_forms.h`, `calendar_parse.*` y
  `tools/calendar_core_test.cpp` (79 chequeos: RFC 3339 con offset, fecha local
  de todo el día, recordatorio del evento vs. default del calendario vs. sin
  recordatorio, tope de 24 h, elección del evento, fases e intensidad, formas
  OAuth y query de eventos). `python3 tools/run_tests.py` → **34 passed**.
- **Credenciales y red**: `calendar_store.*` (namespace `calendar`, claves
  `cid`/`csec`/`rt`/`sel`/`selName`/`fbLead`/`grace`), `calendar_http.*` (cliente
  TLS propio, mutex propio, cuerpo limitado) y `calendar_auth.*` (device flow,
  refresco, errores en frases para el panel; el access token vive en RAM).
- **Servicio**: `calendar.*` tiene su tarea (12 KB, core 1), consulta la lista y
  el próximo día de eventos cada 15 min (cada 2 min hasta el primer éxito), y
  publica un snapshot bajo mutex. `calendarReactionNow()` no toca la red.
- **Cara**: `buddyGetView()` aplica la reacción local sobre el snapshot (estado
  `UPCOMING`, etiqueta con el título, fase `EVENT` y violeta) sólo si el bridge
  no está en `thinking`/`tool`/`waiting`/`error`; `buddyUpcomingPose()` escala
  con el tiempo y `drawUpcomingTitle()` escribe el título arriba. `buddy_led`
  late violeta aunque no haya bridge. Pruebas nuevas en `buddy_anim_test`.
- **Panel**: tarjeta nueva con los pasos de Google Cloud (API, consentimiento
  publicado, cliente *TVs and Limited Input devices*), formulario de credenciales
  (vacío = no tocar), código grande del device flow con enlace y cuenta
  regresiva, selector `<select>` con los calendarios reales y botones de
  actualizar/olvidar. `GET /api/calendar*` ahora pide sesión.
- **Rutas**: `/api/calendar` (estado + reacción), `/api/calendar/calendars`,
  `/api/calendar/config`, `/api/calendar/connect`, `/api/calendar/cancel`,
  `/api/calendar/select`, `/api/calendar/refresh`, `/api/calendar/forget`.
  `buddy_calendar.*` (proxy al bridge) se eliminó.
- **Consola**: `calendar` y `calendar refresh`.
- **Compilación**: `arduino-cli` con el FQBN de `INSTALL.md` §3 → **OK**,
  1 554 278 B de flash (49%) y 70 468 B de RAM (21%), 257 212 B libres.
- **Panel ejercitado contra `tools/mock_gadget_api.py`** por HTTP: sin sesión
  `GET /api/calendar` → 401; login → `unconfigured`; configurar → `needs_auth`;
  conectar → `auth_pending` con `ABCD-EFGH`; aprobación simulada → `ready` con 3
  calendarios; elegir Trabajo → `selectedName=Trabajo`, `reaction.phase=early`;
  olvidar → `unconfigured`. Los IDs que usa `app.js` existen todos en
  `admin.html` y `node --check` pasa. El navegador headless del MCP no arrancó
  esta sesión (timeout del protocolo): no hay captura visual.
- **Pendiente honesto**: verificación en la placa (T12) y la conexión real con
  la cuenta de Google del dueño. Ambas necesitan autorización/flash explícita.
- Nota: el hook de commit cuenta líneas desde `origin/main` en una rama
  encadenada y reporta ~22k; las rebanadas reales son 1 061 / 705 / 1 006 / docs.

L4 — verificación en la placa (21-nov, autorizada por el dueño), COM5:

- `arduino-cli upload` → `Hash of data verified`. Firmware de la última prueba:
  1 555 104 B escritos; compilación 1 554 962 B (49%) y 70 468 B de RAM (21%).
- Sonda `tools/probes/calendario_local.txt` con `calendar fake`, que inyecta un
  evento sintético por consola (no persiste, mismo modelo que `buddyface`):
  - 20 min antes, aviso 30 → `reaccion: early (activa) intensidad=33 ms=1200000`.
  - 2 min antes → `starting`, `intensidad=93`, `ms=120000`.
  - evento empezado (-1 min) → `ongoing`, `intensidad=100`, `ms=-60000`.
  - `calendar fake off` → `reaccion: none` y el bridge vuelve a mandar
    (`state: idle`, `label: aca estoy`, LED cyan).
  - `/api/status` durante la reacción: `state:"upcoming"`,
    `label:"Evento de prueba"`, `phase:"event"`, `led:[170,80,255]`, y el `dump 4`
    muestra el título arriba y los ojos más abiertos. Todo esto con el bridge
    conectado y **sin credenciales de Google**: el calendario no depende de él.
- Google real (sin cuenta): `GET /api/calendar` sin sesión → `401`. Con un client
  falso guardado desde el panel, `POST /api/calendar/connect` → `502` con el
  error real de Google `"The OAuth client was not found."`: el TLS del equipo
  llega a `oauth2.googleapis.com` y el pedido del device flow tiene la forma que
  Google espera. Después `POST /api/calendar/forget` deja el equipo limpio.
- Defecto encontrado y corregido en esa misma verificación: ArduinoJson guarda
  punteros a los `char[]`, y `apiCalendarStatus()` serializaba después de que el
  struct local saliera de alcance, así que el panel recibía bytes basura en
  `userCode`, `verificationUrl`, `selectedId`, `selectedName`, `lastError` y
  `nextSummary`. Se pasó el struct por referencia hasta `serializeJson` y se
  copiaron los `id/name` de la lista (`803586c`), se reflasheó y la respuesta
  quedó limpia (cadenas vacías, `kind: needs_auth`, `lastError` correcto).
- **Lo único que falta** es la conexión con la cuenta real del dueño: requiere su
  Client ID/secret y aprobar el código en Google. Eso no lo puede hacer el
  agente; el panel ya muestra el paso a paso.

L5 — cierre de verificación (21-nov):

- La regla de prioridad salió del `screen_buddy.cpp` a
  `buddyStateYieldsToCalendar()` (puro, `buddy_state.h`) y se fijó en
  `tools/buddy_state_test.cpp`: idle/done/asleep/offline/unconfigured/upcoming
  ceden la cara; thinking/tool/waiting/error no.
- Con la URL del bridge apuntando a `http://127.0.0.1:1`, `calendar fake 20 30`
  siguió publicando `state:"upcoming"`, `phase:"event"`, `led:[170,80,255]` y el
  LED violeta (`rgb 136,64,204`): la reacción no consulta al bridge. La URL se
  restauró a `https://buddy.maximajorel.com.ar` en la misma sonda.
- Reflasheo final de la rama y verificación corta: `calendar fake 20 30` →
  `early 33`, `fake off` → `none`. La placa quedó sin credenciales de prueba
  (`forget`).
- Se aclaró en el panel y en `INSTALL.md` que este flujo usa credenciales OAuth
  y no una "API key", que es la confusión que el pedido original dejaba abierta.
- Compilación y flasheo finales de la rama: 1 555 750 B (49%) de flash y 70 468 B
  (21%) de RAM, `Hash of data verified`. El panel servido por la placa se
  verificó por HTTP: contiene la tarjeta nueva, los pasos del cliente *TVs and
  Limited Input devices* y la aclaración de OAuth; `GET /api/calendar` responde
  `unconfigured` con cadenas vacías y sin sesión responde `401`.
- El navegador headless del MCP sigue sin arrancar (`Network.enable` /
  protocolo agotado), así que la tarjeta no tiene captura visual; sí se
  ejercitaron por HTTP el flujo completo contra el mock y los endpoints reales
  de la placa.
- Sigue pendiente lo único que no puede hacer el agente: autorizar la cuenta real
  del dueño con su Client ID/secret.

L6 — parser OAuth puro + evidencia real + incidente de configuración (21-nov):

- El manejo de las respuestas OAuth salió de `calendar_auth.cpp` a
  `calendar_auth_parse.h/.cpp` (puro, ArduinoJson): éxito con o sin refresh
  rotado, `authorization_pending`, `slow_down`, `access_denied`,
  `expired_token`, `invalid_grant`, error desconocido con descripción y cuerpo
  no-JSON. `tools/calendar_core_test.cpp` lo fija (28 chequeos nuevos) y
  `calendar_auth.cpp` quedó solo con transporte y almacenamiento. Suite: 34.
- **La conexión real del dueño funcionó**: al consultar la placa después de este
  flasheo, `GET /api/calendar` respondió `kind:"ready"` con
  `selectedId:"maximomajorel@gmail.com"`, o sea el refresh token estaba guardado,
  el device flow había terminado y el calendario principal estaba elegido. Es la
  verificación de punta a punta que faltaba.
- **Incidente**: en la misma tanda de verificación, el agente mandó
  `POST /api/calendar/config` con un Client ID/secret falsos para probar el error
  de Google. Ese endpoint, correctamente, invalida el grant al cambiar las
  credenciales; el `forget` posterior terminó de borrar el calendario elegido.
  La configuración real del dueño se perdió y hay que rehacerla desde el panel
  con el Client ID/secret del cliente OAuth que ya existe en Google Cloud.
  Lección: no usar el formulario de credenciales de un equipo ya configurado como
  conejillo de indias; la prueba de error de Google se hace con una placa sin
  configuración o después de un `forget` explícito y avisado.

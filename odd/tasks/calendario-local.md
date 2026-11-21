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
| T1: documento y trackeo de la etapa | — | inline | — |
| T2: modelo y lógica pura con pruebas (RFC 3339, parseo, recordatorio, ventana, fases) | S6, S8 | inline | — |
| T3: credenciales en NVS con validación y claves propias | S1, S2 | inline | — |
| T4: transporte HTTPS propio y device flow con refresco de token | S3, S8 | inline | — |
| T5: tarea de consulta, snapshot y comando de consola | S1, S4 | inline | — |
| T6: reacción de la cara (`BUDDY_UPCOMING`) y luz violeta | S6, S7 | inline | — |
| T7: endpoints del panel y protección de sesión | S2, S7 | inline | — |
| T8: tarjeta del panel con conectar, selector de calendario e instrucciones | S2, S3, S4, S5 | inline | — |
| T9: `INSTALL.md`, `README.md` y `ARCHITECTURE.md` | S5 | inline | — |
| T10: sacar el proxy de calendario del bridge | S1 | inline | — |
| T11: pruebas de host, compilación del firmware y panel empaquetado | S8 | inline | — |
| T12: verificación en la placa | S6 | hardware | — |

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

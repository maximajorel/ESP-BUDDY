# Buddy de bandeja: app de escritorio para manejar el equipo

Tercer pedido del dueño, sobre la dirección 01 ya cerrada en `design/tray-buddy/`.
Una app de escritorio que vive en la bandeja de Windows, se instala como programa,
abre un popover anclado al icono y maneja el equipo por la LAN: pantalla del LCD,
pomodoro, ta-te-ti, calendario, brillo/LED/colores y acceso al `/admin`.
Tauri v2 (webview del sistema + Rust) porque el diseño ya es HTML/CSS y porque el
equipo no manda CORS: el HTTP sale del proceso nativo.

## Specs

S1. Stack Rust liviano — verbatim: "Podemos usar el stack de RUST si es rapido...
y liviano. Si no electron tambien podria ser algo facil de entender." Se elige
**Tauri v2** (binario ~5-10 MB, sin Chromium propio: WebView2 en Windows,
WKWebView en macOS, WebKitGTK en Linux). Rust hace bandeja, posicionamiento,
transporte HTTP, autostart, atajo global y notificaciones. El frontend es
HTML/CSS/TypeScript sin framework y sin lógica de red: sólo `invoke` y pintura.
Electron queda descartado por peso (+150 MB y +150 MB de RAM); WPF/WinUI queda
descartado porque no llega a macOS ni a Linux.

S2. Instalador con nuestro lenguaje de diseño — verbatim: "Tener un instalador
respetando el lenguaje de diseño que tenemos, se instala en el sistema como
programa." El instalador es NSIS por usuario (`installMode: "currentUser"`, sin
pedir administrador), con las imágenes de cabecera y lateral en papel y tinta
según `design/system.md`, el icono de la bandeja (cuadrado de tinta con dos ojos)
y acceso en el menú de inicio. Instalar deja el programa disponible como
cualquier otro, y desinstalar no deja la clave de red guardada.

S3. Primer arranque guarda la conexión por IPv4 — verbatim: "En la primera
apertura debe guardar la info para conectarse al ESP, por Ipv4 por ahora sobre la
red local." El asistente pide la IPv4 (y puerto opcional, por defecto 80), valida
contra `GET /api/status` antes de aceptar, y guarda el destino en el store local
del usuario. Sin una conexión válida el popover no ofrece acciones del equipo y
explica qué falla. La IPv4 se puede cambiar después sin reinstalar.

S4. Pomodoro — verbatim: "Timer para el pomodoro." Chips 10 / 25 / 45 /
personalizado como en `design/tray-buddy/01-popover-bandeja.html`, contador en
mono tabular, y el **equipo es la única fuente de verdad**: arrancar manda
`POST /api/focus {"seconds": n}` y el número que se dibuja sale de lo que informa
`GET /api/status`, no de un contador propio. Parar manda `POST /api/focus
{"seconds": 0}`, que cancela. +5 MIN suma sobre el restante informado. Al vencer,
el equipo celebra solo (Concentración ya lo hace); la app sólo muestra el estado.

S5. Los mismos juegos que la web — verbatim: "Mismos juegos que tenemos en la
web." **Alcance confirmado en L4: sólo el ta-te-ti.** Hoy el único juego de la
web pública es el **ta-te-ti** de
`esp32_gadget/web_ui/landing.js`: tres dificultades (`easy`, `normal`, `hard` con
las probabilidades de error 0.6 / 0.18 / 0), marcador persistido y la cara del
equipo reaccionando por `POST /api/face` con los moods del firmware (`focus`,
`thinking`, `alert`, `happy`, `anger`, `sleepy`, `sad`). Se porta 1:1 con las
mismas reglas y el mismo marcador; el transporte lo pone Rust. No se inventan
juegos nuevos: si mañana la web suma otro, se porta igual.

S6. Calendario del panel, sólo lectura en v1 — verbatim: "Ver el calendario que
fue configurado en la web ui. Si se puede crear eventos desde ahi genial, si no
lo dejamos como spec." La app lee `GET /api/calendar` y `GET
/api/calendar/calendars`, ambas protegidas por sesión, y dibuja la próxima cita y
la lista de hoy como en el artboard 01. **Crear eventos queda fuera de v1**: el
firmware hoy sólo lee de Google Calendar, no tiene endpoint de escritura, y darlo
de alta implica OAuth con scope de escritura y un endpoint nuevo. Queda anotado
como S6.1 para otra feature, no se improvisa acá.

S6.1. (pendiente, fuera de v1) Crear eventos desde el programa. Requiere scope de
escritura en el OAuth del equipo más un endpoint `POST /api/calendar/events`.

S7. Acceso directo al `/admin` — verbatim: "Acceso directo para abrir el
/admin." El botón del pie abre `http://<ip>/admin` en el navegador del sistema.
No se embebe el panel en el webview: la sesión del panel es una cookie de ese
equipo y duplicarla en la app sería tener dos sesiones que se expulsan entre sí.

S8. Brillo, LED y colores sin login — verbatim: "Configuracion de brillo de
pantalla, led y colores del buddy desde el programa sin necesidad de
autenticación." Estado real del firmware: `POST /api/screen`, `POST /api/focus` y
`POST /api/face` **ya** son públicas (`adminIsPublicWrite`), pero `POST
/api/settings` — que es donde viven `brightness`, `uiColor`, `buddyEyeColor` y
`rgb {r,g,b,level,effect}` — exige sesión. Tres caminos posibles:
**Decidido A** (L4): el asistente pide una vez las credenciales del panel y
`POST /api/session` devuelve la cookie `sid`, que la app guarda en el store
local; cada request a `/api/settings` la manda, y ante un 401 la app repite el
login una vez con las credenciales guardadas. Descartadas: (B) token de
emparejamiento nuevo en el firmware — whitelist más angosta que la sesión, pero
es código de autenticación nuevo en el equipo; (C) abrir `/api/settings` sin
sesión, que sería un agujero: cualquier equipo en el AP podría reescribir la
configuración. Con A el firmware no cambia en absoluto.

S9. Elegir la pantalla del equipo — verbatim: "Seleccionar pantalla visible del
ESP." Siete tiles como el artboard, con los nombres que devuelve `GET
/api/screens/names` y el orden de `GET /api/screens/order`; la que está en el
vidrio se marca leyendo `GET /api/status`. Elegir manda `POST /api/screen
{"index": n}` (ya pública).

S10. La app no es dueña de nada del equipo — verbatim: "Ver el calendario que fue
configurado en la web ui." La app no guarda estado propio que compita con el
equipo: no implementa OAuth, no duplica tokens de Spotify ni de Calendar y no
lleva un temporizador paralelo. Todo lo que muestra lo lee del equipo, y sin
conexión lo dice en vez de inventar.

S11. Windows primero, macOS y Linux del mismo código — verbatim: "Quiero arrancar
con compatibilidad en windows pero me gustaria que sea compatible para MAC o
Linux." **Decidido en L4: mismo código multiplataforma ahora, validación real sólo
en Windows en esta feature.** Un solo código y un solo `Cargo.toml`. Únicos puntos por plataforma: el
anclaje del popover al icono (en macOS/Windows sale del rect de la bandeja; en
Linux se ancla al cursor porque AppIndicator no expone rect) y la transparencia
del popover (sin compositor en Linux no hay esquinas redondeadas).

S12. Sin CORS — el equipo no manda `Access-Control-Allow-Origin`, así que ningún
`fetch` desde el webview puede hablarle: todo el HTTP sale del proceso Rust con
`reqwest`. El frontend nunca ve la IPv4 como URL de red, sólo como dato.

S13. USB queda como requisito **futuro**, no de esta feature — verbatim: "en una
de esas tal vez con USB? Lo anotemos como requisito futuro". Hoy el transporte es
sólo HTTP sobre la LAN: el firmware expone el CDC serie para la consola, pero no
hay protocolo de comandos por USB. Si se implementa, entra como transporte
alternativo detrás de la misma capa de comandos (T2), sin que el frontend se
entere. No se escribe código de USB en esta feature.

S14. El icono de la bandeja es el cuadrado **amarillo** con ojos negros —
verbatim: "el icono me gustaria que sea ese icono amarillo ocn los ojos negros".
El papel pasa de `#ffffff` a `--sun` (`#ffd731`, `design/system.md` §1), que es el
mismo amarillo que ya usa `.brand .logo` en el popover y el logo del panel web.
Los ojos siguen en `--ink`. La geometría (26 unidades, borde de 1, ojos 6x7 al
38%) no cambia: sólo el color del papel, y se regenera con
`tools/make_icons.py`, que es la única fuente de los iconos.

## Slices de entrega

La rama `feat/tray-buddy` acumula más de 400 líneas autoradas desde `origin/main`,
así que la entrega se corta por unidad revisable y **no** por la rama entera.
Cada punto es un candidato de review independiente y en ese orden:

| Slice | Contenido | Commit |
|-------|-----------|--------|
| S1 | esqueleto Tauri: ventana, bandeja, popover estático (T1) | `e9cdae6` |
| S2 | icono amarillo y remuestreo arreglado (T11) | `50bbe44` |
| S3 | transporte HTTP + store + comandos (T2, base de T4/T5/T7/T9) | `2b133e2` |
| S4 | asistente de conexión y popover conectado (T2, T4, T5, T7, T9) | `b65c2ce` |
| S5 | instalador NSIS con las imágenes del diseño (T10) | `83fef0b` |
| S6 | asistente que reemplaza el cuerpo y desbordes de la ventana | `f3dc558` |

Un PR por slice cuando el dueño lo pida; no se abren solos. Los pendientes
declarados (T6 ta-te-ti, T8 brillo/LED/colores, S13 USB) no entran en ninguno.

## Tasks

| ID | Spec | Ruta | Commit |
|----|------|------|--------|
| T0 | — | **decidido B en L9**: herramientas oficiales de Microsoft. `tray_buddy/tools/setup-toolchain.ps1` como administrador (workload VCTools + `--includeRecommended`, con `--passive` para ver progreso) | `chore(tray): script de setup del toolchain de Windows` |
| T1 | S1, S11 | inline (parent): esqueleto `tray_buddy/` + ventana sin bordes + bandeja · commit `e9cdae6` (1190 líneas: proyecto nuevo, HTML/CSS/Rust/iconos; queda in_progress sin compilar) | `chore(tray): esqueleto de la app de bandeja con Tauri` |
| T2 | S3, S12 | inline (parent): transporte Rust + store + asistente de IPv4 con validación contra `/api/status` | `feat(tray): asistente de conexión por IPv4` |
| T3 | S1, S11 | inline (parent): icono de ojos, anclaje del popover con el `rect` del evento de bandeja, atajo global, autostart | `feat(tray): bandeja, anclaje y atajo` |
| T4 | S9 | inline (parent): selector de pantalla contra `/api/screens/*` y `/api/status` | `feat(tray): selector de pantalla` |
| T5 | S4 | inline (parent): pomodoro contra `/api/focus` | `feat(tray): pomodoro sobre Concentración` |
| T6 | S5 | inline (parent): ta-te-ti portado de `landing.js` | `feat(tray): ta-te-ti` |
| T7 | S6 | inline (parent): lectura de calendario del equipo | `feat(tray): calendario de sólo lectura` |
| T8 | S8 | inline (parent): credenciales una vez + sesión guardada, y `/api/settings` (brightness, uiColor, buddyEyeColor, rgb) | `feat(tray): brillo, LED y colores` |
| T9 | S7 | inline (parent): botón que abre `/admin` en el navegador | `feat(tray): acceso al panel` |
| T10 | S2 | inline (parent): instalador NSIS con papel y tinta | `build(tray): instalador NSIS` |
| T11 | S14 | inline (parent): `PAPER` a `--sun` en `tools/make_icons.py` y regenerar los iconos | `feat(tray): icono amarillo de la bandeja` |
| T12 | S13 | **no se implementa en esta feature**: sólo queda anotado como requisito futuro | — |

Riesgo declarado: con A la superficie nueva es del programa y no del firmware;
T8 guarda credenciales del panel en el store local del usuario (item 2: la clave
nunca se loguea y el archivo de estado no se commitea). T2 escribe un archivo de
configuración local con la IPv4 (item 6: lo cubre un test del parser y del
validador).

## Log

L1. Pedido original (mensaje 1, verbatim):
"Fijate que en engram o seguramente en el proyecto esta la idea de hacer el
tray-buddy. Quiero arrancar con compatibilidad en windows pero me gustaria que sea
compatible para MAC o Linux. Que stack me recomendas?"

L2. Pedido que abre esta feature (mensaje 2, verbatim):
"Okas perfecto! Iniciemos un plan para hacer el programita.
Podemos usar el stack de RUST si es rapido... y liviano. Si no electron tambien
podria ser algo facil de entender.
Yo creo que para cumplir el objetivo tiene que:

01.- Tener un instalador respetando el lenguaje de diseño que tenemos, se instala
en el sistema como programa
02.- En la primera apertura debe guardar la info para conectarse al ESP, por Ipv4
por ahora sobre la red local
03.- El programa debe tener

- Timer para el pomodoro
- Mismos juegos que tenemos en la web
- Ver el calendario que fue configurado en la web ui. Si se puede crear eventos
desde ahi genial, si no lo dejamos como spec
- Acceso directo para abrir el /admin
- Configuracion de brillo de pantalla, led y colores del buddy desde el programa
sin necesidad de autenticación
- Seleccionar pantalla visible del ESP"

L3. Evidencia del relevamiento (lectura del repo, sesión de plan):
- `design/tray-buddy/01-popover-bandeja.html` es la dirección elegida; 02 y 03
  están descartadas en `design.json`.
- El único juego de la web es el ta-te-ti de `web_ui/landing.js` (SLIP 0.6/0.18/0,
  marcador en `localStorage["gadget.tateti"]`, moods por `POST /api/face`).
- `adminIsPublicWrite` ya deja públicas `POST /api/focus`, `/api/screen` y
  `/api/face`; `POST /api/settings` y todo `/api/calendar*` exigen sesión
  (`adminRequiresSession` en `admin_auth.cpp`).
- No hay ningún header `Access-Control-Allow-Origin` en `esp32_gadget/`.
- El calendario del firmware sólo lee de Google Calendar: no existe endpoint de
  escritura de eventos.

L4. Decisiones del dueño (mensaje 3, respuestas al cuestionario):
1. Auth S8 → "A · Credenciales una vez, sesión guardada (recomendado)".
2. Juegos → "Sí, sólo el ta-te-ti".
3. Mac y Linux → "Ahora: mismo código, sin validar".

L5. Evidencia de entorno (probe de la sesión de implementación):
- WSL: sin `cargo`/`rustc`, sin `webkit2gtk-4.1`, y `sudo` bloqueado
  ("no new privileges"), así que la variante Linux no se puede compilar ni correr
  acá.
- Windows (interop OK desde WSL): Windows 11 26200, WebView2 Runtime presente
  (`C:\Program Files (x86)\Microsoft\EdgeWebView\Application\157.0.4341.60`),
  node/npm/git presentes, **sin Rust y sin Visual Studio / MSVC**
  (`vswhere` ausente, `link.exe` ausente).
- Conclusión: T0 (instalar Rust + MSVC Build Tools) es prerequisito de cualquier
  verificación; se compila desde WSL invocando los binarios de Windows por
  interop.

L6. APIs de Tauri 2.12.7 verificadas contra el código fuente del crate (no de
memoria), porque sin compilador cada suposición cuesta un ciclo entero:
- `TrayIconEvent::Click { rect, position, button, button_state }` trae el
  rectángulo del icono medido en el momento del clic, y `TrayIcon::rect()` lo
  consulta después. Eso hace innecesario `tauri-plugin-positioner` y cubre S11
  sin plugin de terceros.
- `Monitor::work_area() -> &PhysicalRect<i32, u32>` con campos `position` y
  `size`: el clamp contra la barra de tareas sale sin dependencias.
- `tauri::Rect` es `{ position: Position, size: Size }`, enums Physical/Logical
  según plataforma: hay que convertir a mano con el `scale_factor` de la ventana.
- `MenuItem::with_id(manager, id, text, enabled, accelerator)` y
  `WindowEvent::CloseRequested { api }` confirmados.
- `npm install` del CLI de Tauri ya corrió del lado Windows vía interop: el CLI
  está listo, lo único que falta es Rust + MSVC.

L7. Decisión del dueño sobre el toolchain (pregunta "¿no hay otro camino para
bajarme el instalador?"): **D · compilar en GitHub Actions**. Descartadas las
otras tres, con sus números:
- A (MinGW portable, `rustup target add x86_64-pc-windows-gnu` + WinLibs
  16.2.0, zip de 276 MB): el más barato en MB y sin admin, pero Tauri sobre GNU
  no está soportado oficialmente; `webview2-com-sys 0.39.1` declara el target
  `x86_64-pc-windows-gnu` y el crate `windows` baja `windows_x86_64_gnu`, así
  que la cadena podría cerrar, pero no hay garantía.
- B (VS Build Tools sólo con `VC.Tools.x86.x64` + SDK, ~2,2 GB, con admin):
  camino soportado. Queda como plan B si CI no alcanza.
- C (Electron, 0 MB de toolchain): la app pasaría de ~8 MB a ~150 MB y se
  descartaría el esqueleto Rust.
- D (elegida): 0 MB locales, el runner `windows-latest` ya trae MSVC y el SDK;
  se baja el instalador como artifact. Costo asumido: no hay `tauri dev` local,
  la iteración es por push, y la verificación en pantalla la hace el dueño
  corriendo el artifact.

Nota de por qué se llegó hasta acá: el instalador de VS falló dos veces con
código 87 por el mismo motivo, ajeno a winget y a los argumentos — PowerShell
conserva el salto de línea dentro de las comillas, así que un argumento pegado
en dos líneas llega partido a `setup.exe` (el log del bootstrapper muestra
`--inc` + salto + `ludeRecommended`). `tools/setup-toolchain.ps1` quedó en el
repo por si el dueño alguna vez quiere el toolchain local: pasa los argumentos
como arreglo y verifica `cargo` y `link.exe` al final.

L8. El dueño canceló D de inmediato: "NOOO CANCELA D". Se borró
`.github/workflows/tray-build.yml` y T0 vuelve a ser toolchain **local**,
pendiente de elegir entre A (MinGW portable, sin admin) y B (VS Build Tools
sólo con VC.Tools + SDK, con admin). D queda descartada como camino, no como
alternativa futura: el motivo del rechazo no se registró, pero el costo
declarado en L7 (iterar por push, verificación manual del artifact) es la
hipótesis.

L9. El dueño rechazó MinGW: "Nono pero las herramientas oficiales quiero
instalar". Se quitó el target GNU (`rustup target remove
x86_64-pc-windows-gnu`) y queda **B**, el camino soportado: Build Tools de
Microsoft con el workload VCTools. El script `tools/setup-toolchain.ps1` pasa
los argumentos como arreglo (inmune al salto de línea que produjo los dos
errores 87) y ahora usa `--passive` para que el dueño vea la barra de progreso
en vez de 20 minutos de silencio. A y D quedan descartadas por decisión del
dueño, no por inviabilidad técnica.

L10. Tercer pedido del dueño sobre la app, después de ver el popover compilado
(verbatim):
"Ok bueno vi la estetica y me gusto si, el icono me gustaria que sea ese icono
amarillo ocn los ojos negros

Necesitamos entonces

1.- Instalador
2.- Wizard configurador con el programa
3.- Integracion del sistema al esp con la red local... (en una de esas tal vez
con USB? Lo anotemos como requisito futuro)

Implementemos todo lo que podamos y de ahi ejecutar tests, luego de eso ya pruebo
yp"

Se agregan S13 (USB futuro, no se implementa) y S14 (icono amarillo), y T11/T12.
T2, T4, T5, T9 y T10 se reabren: son exactamente los puntos 1 y 2 y la
integración del punto 3. Bloqueante encontrado y resuelto en esta sesión:
`tauri.conf.json` tenía `macOSPrivateApi: true` sin la feature
`macos-private-api` en `Cargo.toml`, así que **ninguna** compilación había pasado
del build script; el dueño decidió sacar la clave ("saquemos eso del macros, asi
arrancamos de ultima primero con windows"), y `cargo build` quedó verde
(`tray-buddy.exe`, 14 MB, arranca y crea la bandeja).

L11. Evidencia de verificación de esta sesión (no son opiniones, son corridas):
- `cargo test`: **20 pruebas, 0 fallos** (store, parser de dirección, parseo de
  `/api/status` con campos faltantes, cookie `sid`, y tres de coherencia del
  instalador). El transporte se prueba contra un servidor HTTP falso en
  `127.0.0.1`, así que no hace falta el equipo para correrlas.
- `npm run build`: instalador generado, `Buddy_0.1.0_x64-setup.exe`, 1.373.688
  bytes, exit code 0. El `installer.nsi` generado referencia `SIDEBARIMAGE` y
  `HEADERIMAGE` y makensis corre después, así que las imágenes viajan dentro.
- **Corrección de un error de esta misma sesión**: se creyó que en un build de
  desarrollo Tauri lee el frontend del disco. Es falso: `generate_context!`
  embebe `ui/` al compilar (brotli), y `cargo build` no se enteraba de los
  cambios de `ui/` porque el build script no los vigilaba. Por eso el dueño
  seguía viendo el asistente transparente después del arreglo. Se agregaron los
  `cargo:rerun-if-changed` de `../ui/*` en `build.rs`. Y `cargo build` falla con
  "Acceso denegado" si la app está corriendo: hay que cerrarla antes.
- Verificación visual del popover y del asistente: Edge headless
  (`--headless=new --screenshot --force-device-scale-factor=1`) sobre
  `ui/index.html`. De ahí salió el bug del asistente "transparente": un hijo
  `position: absolute` de un contenedor `display: grid` se pintaba debajo de los
  otros ítems; se arregló con `z-index: 3` y se volvió a capturar.
- El icono se verificó leyendo los píxeles del PNG generado: papel
  `(255,215,49,255)` y ojos `(0,0,0,255)` a 128 y a 32 px.
- **Sin probar contra el equipo físico**: no hay ESP32 alcanzable desde esta
  sesión, así que el diálogo real con el firmware (pantalla, pomodoro,
  calendario) queda para la prueba del dueño.

L12. Primera prueba del dueño en la app real (captura de pantalla): "Mira asi
queda, se transparenta, hagamos un analisis muy minucioso de la UI". El
`z-index: 3` del asistente **no alcanzó**: la captura del WebView2 real seguía
mostrando los tiles encima del formulario. Decisión de diseño, no parche: el
asistente deja de ser una capa y pasa a **reemplazar** el contenido — al abrirse,
las tres secciones del cuerpo se ocultan con el atributo `hidden`, así no hay
superposición posible y la UI vieja tampoco queda clickeable detrás del
formulario. Se descarta seguir peleando con el orden de pintado de un hijo
absoluto de un contenedor grid.

Del mismo análisis de la captura salieron tres desbordes reales que el render
headless anterior había tapado como "artefacto":
- `.body` era grid con una columna `auto`: el reloj del pomodoro (40 px) tenía
  min-content mayor que los 358 px útiles, la columna crecía y **todo** el cuerpo
  se salía de la ventana de 388. El sistema recortaba el reloj ("25:" sin los
  segundos) y el último chip. Ahora la columna es `minmax(0, 1fr)` con
  `overflow-x: hidden` y los tiles son `min-width: 0`.
- La barra de título empujaba los botones de ventana fuera del recorte: el
  estado es ahora el único elemento sacrificable (se recorta con puntos
  suspensivos) y las etiquetas se acortaron (Offline / Conectando / En línea).
- "TRAGAMONEDAS" no entraba en un tile de 4 columnas: 8 px sin letter-spacing,
  gap de 6 y padding de 5, con `overflow-wrap: anywhere` como red para nombres
  inventados por el dueño. Antes se partía "TRAGAMONED/AS".
Verificación: capturas de Chrome headless con `.win-wrap` en flujo estático (para
ver el popover entero, 380x660) en los dos estados, y la app de escritorio
relanzada con el perfil de WebView2 borrado para descartar caché de assets.

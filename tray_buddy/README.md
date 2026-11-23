# Buddy en la bandeja

App de escritorio para manejar el equipo ESP32 por la red local sin abrir el
panel. Vive en la bandeja del sistema, y el clic izquierdo abre un popover
anclado al icono con tres cosas: la pantalla del LCD, el pomodoro y el
calendario. El botón **Panel** abre `/admin` en el navegador.

La dirección de diseño es `../design/tray-buddy/01-popover-bandeja.html` y los
tokens salen de `../design/system.md`: papel, tinta, bordes de 1px, stickers
planos, sin gradientes y sin `box-shadow`.

## Stack

Tauri v2. El webview es el del sistema (WebView2 en Windows, WKWebView en macOS,
WebKitGTK en Linux), así que el binario pesa unos pocos MB en vez de arrastrar
un Chromium propio. Rust hace la bandeja, la ventana sin bordes, el anclaje y —
esto es lo que decide la arquitectura— **todo el HTTP**.

El firmware no manda `Access-Control-Allow-Origin`, así que ningún `fetch` desde
el webview puede hablar con el equipo: la capa nativa es la única que puede, y el
frontend sólo llama `invoke`.

## Requisitos para compilar

| Pieza | Por qué |
|---|---|
| Rust estable (MSVC) | el binario |
| Visual Studio Build Tools con la carga *Desktop development with C++* | el linker y el Windows SDK que Rust usa en Windows |
| Node.js 18+ | sólo el CLI de Tauri (`@tauri-apps/cli`) |
| WebView2 Runtime | ya viene con Windows 11; el instalador lo verifica |

En Windows, una sola vez y desde una consola **como administrador**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup-toolchain.ps1
```

El script instala Rust (toolchain MSVC) y Build Tools con la carga de C++, y
al final verifica que existan `cargo` y `link.exe`. Existe porque pegar los
argumentos del instalador de VS a mano es frágil: PowerShell conserva el salto
de línea dentro de las comillas, y un argumento partido en dos líneas hace que
`setup.exe` responda 87 (parámetro inválido).

Después, en una terminal nueva, `cargo --version` tiene que responder.

Desde WSL también funciona, invocando los binarios de Windows por interop: no hay
nada de Rust instalado en Linux, y la variante Linux (WebKitGTK) no se compila
acá.

## Comandos

```bash
cd tray_buddy
npm install
npm run dev      # abre la app en modo desarrollo
npm run build    # genera el instalador NSIS en src-tauri/target/release/bundle/nsis/

cd src-tauri
cargo test       # 41 pruebas: store, parser de dirección, transporte HTTP, agenda y arranque
cargo build      # el binario de desarrollo, sin instalador
```

`cargo test` no necesita el equipo: levanta un servidor HTTP falso en
`127.0.0.1` y le habla con el cliente real, así el transporte (URL, método,
cuerpo, cookies y parseo) se prueba sin un ESP32 conectado.

El popover también se puede abrir directo en un navegador
(`ui/index.html`) para iterar el diseño sin compilar: `app.js` detecta que no
existe la API de Tauri y se queda con una cáscara inerte.

Para mirar la agenda con datos sin la placa ni la app compilada está
`tools/preview.js`: copia `ui/` a `.build-preview/` (ignorada por git), inyecta
un `window.__TAURI__` de mentira con el contrato real de `/api/status` y
`/api/calendar*`, congela el reloj y te imprime los dos comandos de Chrome
headless (DOM y captura). Los escenarios cubren el día normal, el equipo con
firmware viejo, sin autorización, sin sesión, un día de 24 eventos y los eventos
que cruzan la medianoche:

```bash
node tools/preview.js day          # tomorrow, old, empty, unauthorized, session,
node tools/preview.js late-night   # busy, multiday, late-night
```

## Instalación

El instalador es NSIS, **en español**, y sale de `npm run build`:
`src-tauri/target/release/bundle/nsis/Buddy_0.1.0_x64-setup.exe` (~1,4 MB). Es
**por usuario**: instala en `%LOCALAPPDATA%\Buddy` y escribe todo en `HKCU`. No
pide administrador salvo que falte WebView2, que se instala con el bootstrapper
oficial (esa vez sí lo pide). El autor que muestra Agregar o quitar programas es
`bundle.publisher` (`Máximo Majorel`).

Lo que hace el asistente, y lo que pregunta:

- **Menú Inicio**: el acceso directo se crea siempre, en `Buddy`.
- **Escritorio**: lo **pregunta** al final, con un tilde que se puede destildar
  (`MUI_FINISHPAGE_SHOWREADME` de la plantilla de Tauri). Si se destilda no queda
  acceso directo: en una actualización por el camino por defecto (desinstalar
  antes de instalar) el desinstalador viejo borra `$DESKTOP\Buddy.lnk`, y en una
  instalación nueva nunca se creó. Sólo sobrevive si se eligió "no desinstalar",
  que es la opción que deja la instalación anterior como estaba.
- **Iniciar con Windows**: lo **pregunta** `installer/hooks.nsh`
  (`NSIS_HOOK_POSTINSTALL`), en castellano y con un sí/no. Dice que sí: escribe la
  misma entrada que el menú de la bandeja. Dice que no: **borra** la que hubiera
  quedado de una instalación anterior, porque el camino "no desinstalar" no corre
  al desinstalador viejo y abstenerse de escribir no alcanza. En una instalación
  silenciosa (`/S`) no pregunta y no toca nada: encenderlo sin permiso es de las
  cosas que el dueño descubre tarde.
- **Ejecutar Buddy ahora**: el tilde de la página final.
- **Actualizar** vuelve a preguntar el arranque automático y la respuesta manda:
  la opción por defecto desinstala antes de instalar y ese desinstalador borra la
  entrada del registro (el camino `/UPDATE`, que la conserva, no se usa); y en el
  camino "no desinstalar" el hook borra la entrada si la respuesta es no. El
  desinstalador limpia también la marca del Administrador de tareas, salvo en una
  actualización `/UPDATE`, para no devolver prendido lo que se había apagado.

### Sin ventana de consola

El binario de release es de subsistema GUI (`#![cfg_attr(not(debug_assertions),
windows_subsystem = "windows")]`): no abre consola, arranca escondido y vive en
la bandeja. El que abre una ventana negra de terminal es
`src-tauri/target/debug/tray-buddy.exe`, el binario de desarrollo: no es un bug
de la app instalada. Si aparece una consola, se está corriendo el de debug.

### Una sola copia

Si Buddy ya está corriendo, una segunda instancia no abre una ventana nueva: avisa
y se va. El candado es una mutex con nombre (`Local\ar.esp32gadget.buddy.instance`,
en la sesión del usuario), que Windows libera cuando el proceso muere, así que no
queda ningún archivo rancio si la app se cuelga. El aviso es un evento con nombre:
la copia que llega tarde lo levanta y la primera **muestra su popover**, que es lo
que el dueño buscaba al tocar el acceso directo.

### Arrancar con Windows

En el menú de la bandeja (clic derecho) hay un ítem marcable, **Iniciar con
Windows**. Prende o apaga una entrada en
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, valor `Buddy`, con la
ruta del ejecutable **entre comillas** (sin comillas, una ruta con espacios se
corta en el primer espacio y el arranque falla en silencio). El tilde se lee del
registro cada vez que se abre el menú, así que si se apaga desde `msconfig` el
menú dice la verdad.

El nombre del valor coincide con `${PRODUCTNAME}` del instalador, que borra esa
misma entrada al desinstalar: es un contrato entre las dos partes y tiene test
(`autostart::tests::el_valor_del_registro_es_el_que_borra_el_desinstalador`).

### Desinstalar

Panel de control → Agregar o quitar programas → Buddy. Se van el programa, los
accesos directos y la entrada de arranque automático. La configuración
(`%APPDATA%\ar.esp32gadget.buddy\config.json`: IP del equipo y sesión del panel)
queda, salvo que se tilde la casilla de borrar datos de la app.

## Estructura

```
tray_buddy/
├── src-tauri/
│   ├── src/main.rs        bandeja, ventana, salir vs. esconder
│   ├── src/autostart.rs   arrancar con Windows: HKCU Run, valor "Buddy"
│   ├── src/store.rs       IPv4 puerto + token de sesión en el store local
│   ├── src/api.rs         el único HTTP: reqwest contra el equipo
│   ├── src/commands.rs    los comandos que el popover llama con invoke
│   ├── tauri.conf.json    ventana 388x660 transparente, bundle NSIS
│   ├── capabilities/      permisos: arrastrar, esconder, fijar arriba
│   ├── icons/             generados, no dibujados a mano
│   └── Cargo.toml
├── ui/                    HTML/CSS/JS sin framework y sin lógica de red
├── installer/             cabecera y lateral del NSIS, generados
└── tools/
    ├── make_icons.py           icono desde la geometría del diseño
    ├── make_installer_art.py   cabecera y lateral del instalador
    └── preview.js              la UI con datos falsos, sin Tauri ni la placa
```

Los iconos se regeneran con `python3 tools/make_icons.py`: el cuadrado de papel
de 26 unidades, el borde de tinta de 1 y los dos ojos de 6x7 al 38% son los
mismos números que `.tray-buddy` en el diseño, así que el icono de la bandeja y
el del instalador no se pueden desincronizar a mano.

## La ventana

El popover es una ventana **sin decoración y transparente** de 388x660. Los 8px
de la derecha y los 16px de abajo que sobran son la losa negra desplazada 7px y
el pico que apunta al icono: todo lo que se dibuja tiene que caer dentro de ese
rectángulo, porque el sistema recorta lo que se sale.

Con la agenda desplegada el control **Agenda / Achicar** de la tarjeta de
calendario la lleva a 520x820. Al agrandar, la esquina inferior derecha —donde
vive el pico— no se mueve: la ventana crece hacia arriba y hacia la izquierda.
El tamaño elegido se guarda en el store local y se recuerda entre arranques.

Cerrar y minimizar son lo mismo, esconder. Salir es una acción del menú de la
bandeja: una app de bandeja que muere al cerrar su ventana no es una app de
bandeja.

**Clic afuera esconde; el pin lo impide.** Sin fijar, el popover se comporta como
un popover: hacés clic en otra ventana y se esconde. Con el **pin** de la
cabecera queda a la vista —y arriba de las otras ventanas— aunque trabajes en
otra parte. El pin se guarda en el store, así que el popover nace como lo dejaste,
y quien decide es el proceso principal: el evento de pérdida de foco consulta la
preferencia antes de esconder. Como el clic en el icono de la bandeja también le
saca el foco a la ventana, un clic dentro de los 400 ms posteriores a ese blur se
toma como el mismo gesto (esconder) y no como "abrime": sin eso, esconder desde
la bandeja la volvía a mostrar.

## Qué hace hoy

- **Asistente de conexión (S3).** Primer arranque, cambio de dirección desde el
  pie del popover o desde el menú de la bandeja. Pide la IPv4 y un puerto
  opcional (80 por defecto), prueba `GET /api/status` y **sólo entonces** guarda
  en `%APPDATA%\ar.esp32gadget.buddy\config.json`. Una dirección que no
  contesta no se escribe.
- **Pantalla del LCD (S9).** Los tiles salen de `names` y `order` de
  `GET /api/status`, así que respetan el renombre y el orden que el dueño puso
  en el panel. Elegir manda `POST /api/screen {"index": n}` y después **lee** el
  estado otra vez en vez de suponer que salió bien.
- **Pomodoro (S4).** Chips 10/25/45/personalizado. Arrancar manda
  `POST /api/focus {"seconds": n}`; el reloj que se dibuja es el `remainingMs`
  que informa el equipo, no un contador propio. Parar manda `0`. **+5 MIN** suma
  sobre el restante informado.
- **Calendario (S6).** `GET /api/calendar` exige la sesión del
  panel, así que la tarjeta pide usuario y clave una vez, guarda **sólo el token
  `sid`** y la clave no toca el disco. Desde el firmware con sesión persistida,
  el equipo recuerda ese token entre reboots (con su caducidad de 12 h), así que
  el popover no vuelve a pedir la clave después de un reinicio. Arriba sigue la próxima cita; debajo va la
  **agenda del día** (`GET /api/calendar/events`), partida en dos secciones
  reales: **HOY**, con todos los eventos del día —los que ya terminaron,
  atenuados— y **MAÑANA** con el resto de la ventana que consultó el equipo, de
  modo que un evento de mañana nunca aparece como si fuera de hoy. La ventana del
  equipo llega 26 h adelante y a la noche entra el día siguiente al de mañana: ese
  —y cualquier otro— se rotula con su día de la semana en vez de mentir con
  "Mañana". Un evento que arrancó ayer y sigue abierto cae en HOY; uno que ya
  terminó antes de la medianoche local no se muestra. La hora local
  sale del `tz` del equipo sobre los epochs UTC; el día lo manda el firmware en
  `dayStart`/`dayEnd` (su medianoche local, no la de la PC) y, contra un equipo
  con firmware viejo, la app lo deriva del `tz`. Un evento de todo el día se
  rotula "todo el día" en vez de una hora. Sin sesión, sin autorización en
  Google o sin eventos, la tarjeta lo dice en vez de inventar filas; si el equipo
  todavía no publica el endpoint, pide actualizar el firmware en vez de mostrar
  el error crudo. La agenda crece con el cuerpo del popover, que scrollea: no se
  sale nunca del rectángulo de la ventana.
- **El mes entero, con navegación (S1 del pedido del mes).** El botón **Ver el
  mes** cambia la tarjeta del día por una grilla del mes con anterior, siguiente
  y **volver a hoy**, sin cambiar de ventana ni de lenguaje: papel, tinta, bordes
  de 1px y mono sólo para el número del día. Las semanas las decide el equipo
  (`GET /api/calendar/month?y=&m=`, que devuelve `gridStart`/`gridEnd` como
  medianoches locales suyas, lunes primero): la app no calcula un lunes ni una
  medianoche con el reloj de la PC. Los días del mes vecino se dibujan atenuados
  en vez de dejar un hueco; una celda lleva hasta tres marcas y un punto más, y
  debajo está el día elegido con sus eventos, que es donde el mes se vuelve
  legible en 380 px. Si el equipo llegó a su tope de filas, la tarjeta lo dice:
  un mes cortado no se dibuja como si estuviera completo.
- **Editar eventos (S2 del pedido del mes).** Tocar un evento del mes abre el
  editor con título, lugar, todo el día y las horas **en la hora del equipo**.
  Guardar manda `POST /api/calendar/events` (alta) o
  `POST /api/calendar/events/update` (cambio), este último con el `id` y el
  `etag` que trajo el mes: si el evento cambió en otro lado, Google contesta 412
  y la tarjeta lo dice en vez de pisar el cambio ajeno. Borrar pide dos toques y
  se puede hacer desde el mismo editor (`POST /api/calendar/events/delete`). Los
  errores llegan escritos desde el equipo y se muestran tal cual: "Ese evento
  cambió en otro lado", "Ese evento ya no existe en Google", "El permiso guardado
  es de sólo lectura: reconectá la cuenta en el panel". Esa última es la que
  aparece si el permiso de Google se dio con un firmware que sólo pedía lectura:
  se arregla reconectando la cuenta en el panel del equipo, sin reinstalar nada.
- **Brillo del LCD y LED (S3 del pedido del mes), sin sesión.** Dos sliders y un
  selector de color con chips de efecto (fijo, arcoíris, apagado). Mandan
  `POST /api/lcd {"brightness": n}` y
  `POST /api/led {r,g,b,level,effect}`, que el firmware publica por nombre exacto
  —no publica `/api/settings`, que además guarda la zona horaria, la URL del
  buddy y dos tokens—. Se manda al soltar y no al arrastrar: cada pedido es un
  POST por la LAN y un dedo que se mueve son decenas. El estado se lee de
  `GET /api/status` (`display.brightness`, `rgb.*`), no de una preferencia
  local: si el dueño cambia el brillo desde la pantalla CONFIG, el popover lo
  muestra en el siguiente sondeo, y un control que el dueño está agarrando no se
  pisa.
- **Popover que crece (S5).** El botón **Agenda / Achicar** de la tarjeta de
  calendario cambia la ventana entre 388x660 y 520x820 (`tauri::LogicalSize`)
  sin mover la esquina inferior derecha, que es donde está el pico de la
  bandeja. La preferencia se guarda como `expanded` en
  `%APPDATA%\ar.esp32gadget.buddy\config.json` y se aplica al abrir el popover y
  al arrancar la app.
- **Panel (S7).** Abre `http://<ip>/admin` en el navegador del sistema con
  `rundll32 url.dll,FileProtocolHandler`: sin shell de por medio, la URL no se
  interpreta como línea de comandos.
- **Icono amarillo (S14).** El cuadrado es `--sun` (#ffd731) con ojos de tinta,
  generado por `tools/make_icons.py`, que es la única fuente de los iconos de la
  bandeja y del instalador.
- **Instalador NSIS (S2)** por usuario, sin pedir administrador, con cabecera y
  lateral en papel y tinta generados por `tools/make_installer_art.py`.

## Lo que todavía no

- **Ta-te-ti (S5)** — el puerto del juego de `web_ui/landing.js` está pendiente:
  el transporte ya sabe mandar `POST /api/face`, falta la interfaz.
- **USB (S13)** queda anotado como requisito futuro, no implementado: hoy el
  único transporte es HTTP sobre la LAN.

El plan, las specs y las decisiones están en `../odd/tasks/tray-buddy.md`.

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
cargo test       # 25 pruebas: store, parser de dirección y transporte HTTP
cargo build      # el binario de desarrollo, sin instalador
```

`cargo test` no necesita el equipo: levanta un servidor HTTP falso en
`127.0.0.1` y le habla con el cliente real, así el transporte (URL, método,
cuerpo, cookies y parseo) se prueba sin un ESP32 conectado.

El popover también se puede abrir directo en un navegador
(`ui/index.html`) para iterar el diseño sin compilar: `app.js` detecta que no
existe la API de Tauri y se queda con una cáscara inerte.

## Estructura

```
tray_buddy/
├── src-tauri/
│   ├── src/main.rs        bandeja, ventana, salir vs. esconder
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
    └── make_installer_art.py   cabecera y lateral del instalador
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
- **Calendario (S6), sólo lectura.** `GET /api/calendar` exige la sesión del
  panel, así que la tarjeta pide usuario y clave una vez, guarda **sólo el token
  `sid`** y la clave no toca el disco. Arriba sigue la próxima cita; debajo va la
  **agenda del día** (`GET /api/calendar/events`), partida en dos secciones
  reales: **HOY**, con todos los eventos del día —los que ya terminaron,
  atenuados— y **MAÑANA** con el resto de la ventana que consultó el equipo, de
  modo que un evento de mañana nunca aparece como si fuera de hoy. La hora local
  sale del `tz` del equipo sobre los epochs UTC; el día lo manda el firmware en
  `dayStart`/`dayEnd` (su medianoche local, no la de la PC) y, contra un equipo
  con firmware viejo, la app lo deriva del `tz`. Un evento de todo el día se
  rotula "todo el día" en vez de una hora. Sin sesión, sin autorización en
  Google o sin eventos, la tarjeta lo dice en vez de inventar filas; si el equipo
  todavía no publica el endpoint, pide actualizar el firmware en vez de mostrar
  el error crudo. La agenda crece con el cuerpo del popover, que scrollea: no se
  sale nunca del rectángulo de la ventana.
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

- **Brillo, LED y colores (S8)** — `POST /api/settings` escribe configuración del
  equipo y por ahora el popover no lo toca.
- **Ta-te-ti (S5)** — el puerto del juego de `web_ui/landing.js` está pendiente:
  el transporte ya sabe mandar `POST /api/face`, falta la interfaz.
- **El mes completo del calendario** queda para después de esta feature: ver un
  mes exige cambiar la ventana de sync del equipo (hoy pide de -1 h a +26 h con
  `maxResults=12`) y retener muchos más eventos en un equipo con poca RAM. Hoy
  el popover muestra la próxima cita y la agenda del día.
- **USB (S13)** queda anotado como requisito futuro, no implementado: hoy el
  único transporte es HTTP sobre la LAN.

El plan, las specs y las decisiones están en `../odd/tasks/tray-buddy.md`.

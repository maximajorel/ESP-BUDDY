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
npm run dev     # abre la app en modo desarrollo
npm run build   # genera el instalador NSIS en src-tauri/target/release/bundle/nsis/
```

El popover también se puede abrir directo en un navegador
(`ui/index.html`) para iterar el diseño sin compilar: `app.js` detecta que no
existe la API de Tauri y se queda con una cáscara inerte.

## Estructura

```
tray_buddy/
├── src-tauri/
│   ├── src/main.rs        bandeja, ventana, salir vs. esconder
│   ├── tauri.conf.json    ventana 388x660 transparente, bundle NSIS
│   ├── capabilities/      permisos: arrastrar, esconder, fijar arriba
│   ├── icons/             generados, no dibujados a mano
│   └── Cargo.toml
├── ui/                    HTML/CSS/JS sin framework y sin lógica de red
└── tools/make_icons.py    icono desde la geometría del diseño
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

Cerrar y minimizar son lo mismo, esconder. Salir es una acción del menú de la
bandeja: una app de bandeja que muere al cerrar su ventana no es una app de
bandeja.

## Estado

En construcción. El plan, las specs y las decisiones están en
`../odd/tasks/tray-buddy.md`.

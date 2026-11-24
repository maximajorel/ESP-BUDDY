# AGENTS.md — cómo trabajar en este repositorio

Este archivo es el contrato para agentes de IA (y para humanos apurados) que
tocan el proyecto. Las reglas se escribieron para que un cambio se pueda
verificar sin la placa en la mano; lo que no se puede verificar, se dice.

El mapa completo de módulos, las capas y las claves de NVS viven en
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). **Leelo antes de proponer una
estructura nueva**: acá no se repite, y duplicarlo garantiza que se desincronice.

## Qué es esto

Firmware para la **Waveshare ESP32-S3-LCD-1.47B** (ESP32-S3R8, 8 MB PSRAM octal,
16 MB flash, ST7789 172×320 sin MISO) convertido en un equipo de siete pantallas
con un solo botón y un panel web servido por el propio equipo. Lo acompañan
`tray_buddy/` (app de bandeja, Tauri v2), el panel que corre en
`esp32_gadget/web_ui/` y los prototipos HTML de `design/` (referencia de diseño,
**no** son la UI que se sirve).

| Restricción | Consecuencia práctica |
|---|---|
| La pantalla **no tiene MISO** | No se puede leer el panel por software. La verificación honesta es `dump` (framebuffer en ASCII) por consola serie. |
| `PSRAM=opi` y `CDCOnBoot=cdc` | Con PSRAM mal configurada el equipo no arranca; con CDC mal configurado la consola queda muda. El FQBN se copia de `INSTALL.md` §3, nunca se improvisa. |
| 8 MB PSRAM / heap interno chico | Framebuffer completo en PSRAM y cuerpos JSON grandes también. Lo que se guarde en RAM se justifica con un número. |
| Ciclo único, sin RTOS propio | El `loop()` no puede bloquearse. Una espera larga va a una tarea dedicada con su propio stack y su propio mutex, como hacen `calendar.*` y `weather.*`. |
| Stack del lazo = 16 KB (`build_opt.h`) | El lazo dibuja el panel y hace el handshake TLS del bridge en esa misma tarea: con los 8 KB por defecto, el desborde se ve como `CORRUPT HEAP`. Ese archivo sólo acepta flags del compilador; un comentario ahí rompe el build. |

## Reglas duras

1. **`make test` verde antes y después.** Un test que deja de compilar parece un
   test que pasa; el runner cuenta la compilación como fallo a propósito.
2. **Regla nueva ⇒ archivo puro + test de host, escrito primero.** Si la lógica
   necesita `Arduino.h`, no es pura y hay que separarla. Mirá el rojo antes del
   verde.
3. **No se editan los archivos generados.** `web_assets.h`, `font5x7.h` y
   `admin_seed.h` se regeneran (`make pack`, `gen_font.py`, `gen_admin_defaults.py`).
   `make build` falla si `web_assets.h` quedó viejo: se repara con `make pack`,
   no con un editor.
4. **Flashear sólo con autorización explícita del dueño en la sesión actual.**
   Nunca después de un plan, de una corazonada, ni "para ver si anda". Un puerto
   por proceso: no subas firmware con el monitor, la sonda o la IDE abiertos.
5. **El puerto no se adivina.** Se lee de `board list` cada vez.
6. **La autenticación se decide en un solo lugar** (`adminIsPublicWrite` /
   `adminRequiresSession` en `admin_auth.cpp`) y se aplica en el transporte
   (`net.cpp`), nunca dentro de un handler. Toda ruta bajo `/admin` y todo verbo
   que escribe bajo `/api/` pide sesión, salvo las excepciones nominales y
   exactas de escritura pública. Una barra de más vuelve a pedir sesión.
7. **Los límites se validan en un solo lugar** (`config_value.h` para brillo).
   Un segundo rango escrito a mano en el transporte es cómo un pedido de 200 %
   terminó aceptándose como 100 %.
8. **Nada de números mágicos repetidos**: el umbral o color que aparece dos veces
   en un archivo se nombra en el `.h` del módulo.
9. **La cara de Sofía se toca con su skill.** Si el cambio toca
   `buddy_face.h`, `buddy_anim.h`, `buddy_imu.h`, `buddy_ball.h` o `drawFace`,
   leé `.agents/skills/sofia-buddy-face/SKILL.md` antes de elegir una forma. La
   regla que el dueño tuvo que pedir dos veces: **sin cejas**, y las expresiones
   salen sólo de los párpados y de los canales existentes.
10. **No hay `fetch` en el webview de `tray_buddy/`.** El firmware no manda
    `Access-Control-Allow-Origin`, así que todo el HTTP pasa por Rust (`api.rs`)
    y el frontend sólo llama `invoke`.
11. **Un archivo, una responsabilidad.** Si necesita dos títulos para explicarse,
    son dos archivos. Lo local va `static` y no aparece en el `.h`.
12. **Los errores son explícitos.** Una operación que puede fallar devuelve
    `bool` o un código; la API devuelve `ApiResult` con código y JSON, nunca un
    string suelto.

## Comandos

Para el firmware y sus tests alcanza con `g++` (C++17) y Python 3. La app de
bandeja necesita Node 18+ y Rust; su puesta a punto está en `tray_buddy/README.md`.

| Comando | Qué hace | Cuándo |
|---|---|---|
| `make test` | Compila y corre todos los `tools/*_test.cpp`, los checks Python y verifica `web_assets.h` contra `web_ui/` | Siempre, antes del firmware y antes de decir "listo" |
| `python3 tools/run_tests.py --filter buddy` | Sólo un test (acepta `--verbose`, `--fail-on-skip`) | Iterar |
| `make pack` | Regenera `web_assets.h` desde `web_ui/` | Después de tocar HTML/CSS/JS del panel |
| `make verify-assets` | Falla si el panel quedó viejo | Lo mismo, en modo lectura |
| `make build` | Verifica assets y compila con el FQBN real (`arduino-cli`); no corre la suite | Antes de flashear |
| `make flash PORT=COM5` | Compila y sube | Sólo con autorización, con el puerto leído de `board list` |
| `make seed` | Convierte `.env` en `admin_seed.h` (copiá `.env.example` a `.env`) | Primera vez, o al cambiar la credencial sembrada |
| `python3 tools/mock_gadget_api.py 8791` | Mock de la API para probar el panel sin placa | Panel, sin hardware |
| `python tools\gadget_probe.py @tools\probes\X.txt` | Consola serie desde Windows (fuerza reset al abrir) | Verificación en la placa |
| `cd tray_buddy && npm install && npm run dev` | Abre la app de bandeja en desarrollo | Tocar `tray_buddy/` |
| `cd tray_buddy && npm run build` | Instalador NSIS en `src-tauri/target/release/bundle/nsis/` | Probar el instalador |
| `cd tray_buddy/src-tauri && cargo test` | Transporte, store y agenda con un servidor HTTP falso | Tocar `tray_buddy/`; no necesita placa |
| `cd tray_buddy && node tools/preview.js day` | Popover con datos falsos en `.build-preview/` | Iterar UI sin compilar la app |

En WSL el único `arduino-cli` puede ser el de Windows: el `Makefile` recibe
`ARDUINO_CLI`, `SKETCH`, `WEB_UI`, `WEB_ASSETS` y `BUILD_DIR` como variables y
hay un ejemplo en su encabezado. En una consola nativa de Windows, `make test`
necesita `PYTHON=python`.

## Las cuatro capas

De arriba abajo y nunca al revés (`docs/ARCHITECTURE.md` § Regla de dependencias):

1. **Política pura** — `.h`/`.cpp` sin `Arduino.h`. Es la parte que se prueba en
   la compu; ahí va toda regla que se pueda aislar.
2. **Estado** — `settings.*` es el único dueño de NVS (con sus namespaces
   propios para calendario y clima).
3. **Servicios** — `net.*`, `spotify*.*`, `calendar*.*`, `weather*.*`, `imu.*`,
   `rgb.*`, `rotation.*`, `buddy_http.*`.
4. **Presentación** — `gfx.*`, `display.*`, `screens.*`, `screen_*`, `console.*`.

Una pantalla no habla HTTP: lee estado ya resuelto y dibuja. Un servicio no
dibuja. Una utilidad que necesitan dos features no vive adentro de una de ellas:
va a su propio archivo puro con su propia prueba (así existen `sha256.*` y
`url_codec.*`).

## Recetas

Están escritas paso a paso en `docs/ARCHITECTURE.md` § **Cómo extender**. Lo que
no está ahí y sí importa:

- **Pantalla nueva**: el id se agrega al final de `screens.h` (`SCREEN_COUNT`
  crece) para no mover los ids que el panel ya guarda.
- **Endpoint nuevo**: se registra en la tabla de rutas de `net.cpp`, se refleja
  en `tools/mock_gadget_api.py` con la misma regla de acceso (si no, la prueba
  sin placa miente) y se prueba sin red con `api GET /api/...` por consola.
- **Comando de consola nuevo**: el `help` es parte del contrato; una salida de
  varias líneas va entre `txReliable(true/false)`.
- **Test nuevo que necesita fuentes acompañantes**: se agrega a
  `COMPANION_SOURCES` en `tools/run_tests.py`, junto al test que lo necesita.

## Trampas del entorno

- **Abrir el puerto serie reinicia la placa.** Es normal; la primera línea de un
  sondeo debe esperar el banner (`boot 4`).
- **La CDC no se ve desde WSL.** La sonda se corre con el Python de Windows.
- **`.build-*` está gitignoreado a propósito**: cada sesión usa su directorio de
  build para no mezclar cachés.
- **ArduinoJson es opcional en host.** El runner la busca como lo haría el IDE o
  lee `ARDUINOJSON_DIR`; sin headers, `spotify_core_test` y los tests de
  calendario/clima se informan *skipped*, nunca *passed*. El CI fija v7.4.3 y
  corre con `--fail-on-skip` para que un skip no se lea como verde.
- **`design/` es referencia, no runtime.** Son prototipos HTML (los tokens están
  en `design/system.md`); `pack_web.py` empaqueta sólo `esp32_gadget/web_ui/` y
  la bandeja usa `tray_buddy/ui/`. Cambiar un prototipo no cambia ninguna UI.
- **La bandeja se compila con el toolchain de Windows.** Desde WSL se invocan
  los binarios de Windows por interop; no hay Rust/WebKitGTK de Linux en este
  repo. La puesta a punto está en `tray_buddy/README.md` y
  `tray_buddy/tools/setup-toolchain.ps1`.
- **El CI no compila el firmware** (necesita ~2 GB de core). `make build` es el
  gate local/manual.
- **`.env` y `admin_seed.h` no se versionan**: `.env.example` documenta las
  claves y el firmware guarda sólo sal + hash.
- **`_legacy/` es referencia**, no fuente. Ahí vive el demo de Waveshare.
- **Un firmware viejo se detecta, no se adivina**: cuando a `/api/status` le
  falta un campo que el popover necesita, la app de bandeja pide actualizar el
  firmware en vez de mostrar el error crudo.

## Antes de decir "listo"

1. `make test` verde, con la salida pegada (no "debería pasar").
2. Si tocaste `esp32_gadget/web_ui/`: `make pack` y el round trip verde.
3. Si tocaste `tray_buddy/`: `cd tray_buddy/src-tauri && cargo test`.
4. Si tocaste firmware: `make build` con el FQBN real, y el código de salida.
5. Si el cambio tiene efecto visible: evidencia de la placa (`dump`, `api GET
   /api/status`) o, si no hay placa disponible, decilo como pendiente. Nunca lo
   presentes como verificado.
6. Si la feature es grande: documento en `odd/tasks/<feature>.md` con `## Specs`
   (citas verbatim del pedido), `## Tasks` y `## Log`, y los commits en unidades
   revisables.
7. Reportá qué no se pudo verificar y por qué. Los números medidos valen más que
   los adjetivos.

## Convenciones de lenguaje

- **Identificadores, comentarios técnicos y nombres de test en inglés.**
- **Lo que ve el dueño en español**: pantalla, consola serie, panel web,
  mensajes del instalador.
- **La documentación del repositorio está en español** (`README.md`, `docs/`,
  `odd/tasks/`, este archivo). Un documento nuevo sigue esa convención.
- Los commits siguen `tipo(alcance): resumen` con el resumen en español; ver
  `CONTRIBUTING.md`.

## Skills del proyecto

Se leen antes de la tarea correspondiente, no después:

| Skill | Cuándo |
|---|---|
| [`.agents/skills/esp32-gadget-flash/SKILL.md`](.agents/skills/esp32-gadget-flash/SKILL.md) | Compilar, flashear o verificar en hardware |
| [`.agents/skills/sofia-buddy-face/SKILL.md`](.agents/skills/sofia-buddy-face/SKILL.md) | Cualquier cambio en la cara de Sofía |

## Qué no hacer

- No inventes el puerto, el FQBN, la versión del core ni una salida de test.
- No flashees sin autorización ni en medio de una tarea no relacionada.
- No edites archivos generados ni `.build-*`.
- No confundas las tres UIs: `design/` es prototipo, `esp32_gadget/web_ui/` es el
  panel que sirve el equipo y `tray_buddy/ui/` es el webview de la bandeja.
- No muevas ids de pantalla existentes ni agregues dependencias nuevas al
  firmware sin justificarlo (el core, NeoPixel, ArduinoJson y JPEGDEC son el
  techo actual).
- No agregues campos al blob `cfg` de NVS: un valor nuevo va a su propia clave.
- No dupliques en este archivo lo que ya explica `docs/ARCHITECTURE.md`; si algo
  cambió ahí, se actualiza allá.

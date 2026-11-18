# ESP32 Gadget

Firmware para la **Waveshare ESP32-S3-LCD-1.47B** que convierte la placa en un
equipo con siete pantallas y un panel de administración web.

| Pantalla | Qué es |
|---|---|
| **Reloj** | Hora digital (GMT-3, Tucumán) en dígitos de siete segmentos, negro y blanco. |
| **Tragamonedas** | Juego de casino con botón físico; al ganar, la luz RGB de atrás celebra. |
| **Meteoritos** | Juego ASCII de naves, meteoritos y cazas enemigos con barra de vida que disparan, con movimiento libre en dos ejes por sensor, dificultad creciente y récord. |
| **Red** | Nombre y contraseña del WiFi del equipo + dirección del panel web. |
| **Música** | Lo que estás escuchando en Spotify, con carátula, tema, artista y progreso. |
| **Sofía** | Buddy companion: una cara procedural que reacciona a lo que hace Hermes (pensando, trabajando, esperando, error, listo) a través de su bridge, en la LAN o por HTTPS con una URL pública. |
| **Config** | Brillo del LCD y del LED trasero con el botón, guardados en el equipo. |

Todo se maneja con **un solo botón**, el BOOT:

- **toque corto** → interactúa con la pantalla actual;
- **mantener 1 s** → abre el menú de pantallas con el nombre grande y centrado; un toque pasa a la
  siguiente y mantener 1 s entra. Los nombres y el orden se cambian desde **Aplicaciones** en el panel web y quedan
  guardados en el equipo.

## Empezá acá

👉 **[INSTALL.md](INSTALL.md)** — cómo instalar, configurar y usar, paso a paso.

Resumen: `arduino-cli upload` con el core ESP32 3.3.x y dos librerías
(*Adafruit NeoPixel* y *ArduinoJson*). La configuración de placa exacta está en
INSTALL.md §3; si te equivocás con la PSRAM, el equipo no arranca.

## Cómo está organizado

El firmware se separa en cuatro capas: política pura (testeable en host),
estado en NVS (`settings.*`), servicios (`net.*`, `spotify_*`, `imu.*`,
`rgb.*`, `rotation.*`) y presentación (`gfx.*`, `display.*`, `screens.*`,
`screen_*`, `console.*`).

- `esp32_gadget.ino` — arranque, botón y lazo principal.
- `console.*` — consola serie: comandos, volcados y diagnóstico.
- `screens.*` — registro, menú, apagado automático y chrome común.
- `screen_*.cpp` — una pantalla cada uno; no hablan HTTP ni escriben NVS.
- `*.h` puros — `rotation_policy`, `game_difficulty`, `game_hits`,
  `config_value`, `screen_name`, `screen_order`, `steer_solve`,
  `tap_window`, `text_fold`, `buddy_face`, `buddy_anim`, `buddy_sleep`,
  `buddy_event`, `ui_color`.

El mapa completo de módulos, la regla de dependencias, las claves de NVS y el
paso a paso para agregar una pantalla, un comando o un endpoint están en
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md).

Dos archivos son **generados**; si los editás a mano, se pierden:

| Generado | Con qué |
|---|---|
| `font5x7.h` | `python tools/gen_font.py esp32_gadget/font5x7.h` |
| `web_assets.h` | `python tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h` |
| `admin_seed.h` | `python tools/gen_admin_defaults.py` (lee el `.env` de la raíz) |

Olvidarse del segundo es el error más común: el equipo sigue sirviendo la versión
vieja del panel aunque hayas cambiado el HTML.

## Herramientas de desarrollo

En `../tools/`:

| Script | Para qué |
|---|---|
| `run_tests.py` | Compila y corre **todas** las pruebas de host y verifica el panel empaquetado. |
| `gadget_probe.py` | Maneja la consola serie desde Windows y guarda todo en un log. |
| `mock_gadget_api.py` | Mock de la API para probar el panel web en el navegador sin la placa. |
| `gen_font.py` | Genera la fuente desde glifos en ASCII art. |
| `pack_web.py` | Empaqueta el panel web dentro del firmware. |
| `gen_admin_defaults.py` | Convierte el `.env` en la sal y el hash de la credencial del panel. |

## Correr las pruebas

```bash
python tools/run_tests.py            # todas las pruebas de host + el panel
python tools/run_tests.py --filter fold
python tools/run_tests.py --verbose
```

Necesita un compilador C++17 (`g++` o `$CXX`). El test de Spotify además necesita
los headers de ArduinoJson: el runner los busca donde los instala el IDE y, si no
los encuentra, lo informa como *skipped* (nunca como aprobado). Un fallo de
compilación también cuenta como fallo: un test que deja de compilar no puede
seguir pareciendo verde.

La lógica que se prueba vive en archivos puros (`*.h` sin Arduino) a propósito:
`rotation_policy.h`, `game_difficulty.h`, `spotify_parse.cpp`, `buddy_face.h`,
`buddy_sleep.h`, `buddy_event.h` y derivados. Si agregás una regla nueva, esa es
la forma de fijarla sin la placa.


El panel no tiene MISO, así que no se puede leer la pantalla por software. La
verificación honesta es el comando `dump` del firmware, que escribe el
framebuffer en ASCII por la consola.

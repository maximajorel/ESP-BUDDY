# ESP32 Gadget

Firmware para la **Waveshare ESP32-S3-LCD-1.47B** que convierte la placa en un
equipo de siete pantallas con un solo botón y un panel de administración web
servido por el propio equipo.

| | |
|---|---|
| MCU | ESP32-S3R8, dual-core LX7 @240 MHz, 8 MB PSRAM, 16 MB flash |
| Pantalla | ST7789 172×320 vía SPI, sin MISO |
| Sensores | QMI8658 (acelerómetro + giroscopio) |
| Extras | LED WS2812, botón BOOT, USB-C CDC |

Pantallas: **Reloj**, **Tragamonedas**, **Meteoritos**, **Red**, **Música**
(Spotify), **Sofía** (buddy companion) y **Config** (brillo, rotación y bloqueo).
La cara de Sofía también reacciona a **Google Calendar** por su cuenta: el equipo
guarda las credenciales, elige el calendario desde el panel y se mueve según el
recordatorio del próximo evento, sin depender del bridge.
Un toque corto interactúa con la pantalla actual; mantener un segundo abre el
menú de pantallas, cuyo orden y nombres se editan desde el panel web.

La entrada pública `http://192.168.4.1/` no pide sesión: muestra la cara de
Sofía, deja elegir la pantalla desde un menú a pantalla completa y abre el
ta-te-ti 1v1 contra el buddy, que reacciona en la cara del equipo mientras jugás.
La configuración sigue en `/admin`.

## Empezar

👉 **[esp32_gadget/INSTALL.md](esp32_gadget/INSTALL.md)** — instalación,
configuración de placa, primer arranque, WiFi, Spotify, consola serie y
problemas comunes, paso a paso.

👉 **[ARRANCAR.md](ARRANCAR.md)** — puesta a punto del entorno en Windows.

Resumen: el core ESP32 3.3.x, dos librerías (*Adafruit NeoPixel* y
*ArduinoJson*, más *JPEGDEC* para la carátula de Spotify) y el FQBN exacto que
está en INSTALL.md §3. Si te equivocás con la PSRAM, el equipo no arranca.

## Pruebas

```bash
make test                            # host suite + web asset round trip
make test PYTHON=python             # en Windows nativo
python3 tools/run_tests.py --verbose # equivalente, con la salida de cada test
```

El firmware tiene lógica pura a propósito (políticas, parsers, geometría) para
poder probarla en la compu sin la placa. La suite compila cada
`tools/*_test.cpp` con g++ y verifica que `web_assets.h` coincida con
`web_ui/`. Requiere un compilador C++17; el test de Spotify además necesita
ArduinoJson (el runner lo busca solo y lo informa como *skipped* si falta).

La corre el CI en cada push a `main` y en cada PR (`.github/workflows/tests.yml`),
así que no depende de que alguien se acuerde. El workflow usa `--fail-on-skip`:
un test que se saltea por falta de una dependencia no puede leerse como verde.

## Compilar y flashear

```bash
make build                 # verifica los assets y compila el firmware
make flash PORT=COM5       # compila y sube a la placa
make pack                  # reempaqueta el panel si tocaste web_ui/
```

El FQBN de INSTALL.md §3 y el resto de las opciones de placa ya están en el
`Makefile`. En WSL, si el único `arduino-cli` es el de Windows, mirá el
encabezado del `Makefile`: se le pasan rutas de Windows por parámetro.

La compilación del firmware completo se hace con `arduino-cli` y el FQBN de
INSTALL.md §3. El nivel de verificación de hardware es manual: el panel no tiene
MISO, así que la única forma honesta de ver la pantalla sin cámara es el comando
`dump` de la consola serie.

## Estructura del repositorio

```
├── esp32_gadget/       firmware (sketch, módulos y panel web)
├── tools/              pruebas de host, sondeo serie, empaquetado y mock de la API
├── docs/               arquitectura y convenciones
├── odd/tasks/          especificación y bitácora de cada funcionalidad
├── .agents/skills/     skill de este proyecto: compilar, flashear y verificar
├── .github/workflows/  CI: la suite de host en cada push y cada PR
├── Makefile            test, build, flash, pack
└── _legacy/            prototipos viejos y demos (solo referencia)
```

| Quiero… | Ir a |
|---|---|
| Instalar y usar el equipo | [esp32_gadget/INSTALL.md](esp32_gadget/INSTALL.md) |
| Entender cómo está armado | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| Cambiar o agregar una pantalla | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) § Cómo extender |
| Ver qué se está construyendo | [odd/tasks/](odd/tasks/) |
| Compilar, flashear y verificar | [.agents/skills/esp32-gadget-flash/SKILL.md](.agents/skills/esp32-gadget-flash/SKILL.md) |
| Probar el panel sin la placa | `python tools/mock_gadget_api.py` |

## Flujo de desarrollo

1. La lógica nueva que se pueda aislar va a un archivo puro, con su test en
   `tools/` escrito antes.
2. `python tools/run_tests.py` verde antes de tocar el firmware.
3. Compilación con el FQBN real y, si hay placa, `gadget_probe.py` con una
   secuencia reproducible en `tools/probes/`.
4. Trabajo grande: documento en `odd/tasks/` (specs, tareas, evidencia) y
   commits en unidades revisables.
5. Las credenciales del panel se generan desde `.env` (copiá `.env.example`):
   `python tools/gen_admin_defaults.py`. El firmware guarda sólo una sal y un
   hash, y se pueden cambiar después desde `/admin`.
6. El panel web se empaqueta antes de flashear:
   `python tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h`.

Los archivos generados (`font5x7.h`, `web_assets.h`) no se editan a mano; el
runner detecta si el panel quedó desactualizado.

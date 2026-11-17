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
| **Sofía** | Buddy companion: una cara procedural que reacciona a lo que hace Hermes (pensando, trabajando, esperando, error, listo) a través de su bridge local. |
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

```
esp32_gadget/
├── esp32_gadget.ino     arranque, botón, consola serie
├── display.*            driver ST7789 + giro de la imagen a horizontal
├── gfx.*                framebuffer, formas, texto, dígitos de 7 segmentos
├── font5x7.h            fuente (generada, no editar a mano)
├── button.h             máquina de estados del botón (toque / 1 s)
├── settings.*           credenciales, calibración y ajustes en NVS
├── rgb.*                LED WS2812 y celebración de casino
├── rotation.*           rotación automática, bloqueo y compensación del sensor
├── rotation_policy.h    cuándo girar la imagen y cuándo quedarse quieto (puro, test en host)
├── imu.*                QMI8658 (acelerómetro + giroscopio)
├── screens.*            registro de pantallas y marco común
├── screen_name.h        reglas de los nombres elegidos por el dueño (test en host)
├── screen_clock.cpp     pantalla reloj
├── screen_slot.cpp      pantalla tragamonedas
├── screen_game.cpp      pantalla meteoritos (ASCII)
├── screen_net.cpp       pantalla de red
├── screen_config.cpp    pantalla Config (brillo del LCD y del LED, rotación y bloqueo)
├── config_value.h       pasos, topes y mapeo de brillo (puro, test en host)
├── screen_music.cpp     pantalla de música (Spotify)
├── screen_buddy.cpp     pantalla Sofía (cara del buddy)
├── buddy_face.h         ojo cuadrado redondeado, color del dueño y suavizado (puro, test en host)
├── spotify.*            estado, tarea de consulta y consola
├── spotify_parse.*      parser del JSON de Spotify (puro, con test en host)
├── spotify_pkce.*       PKCE, SHA-256 y urlencode (puro, con test en host)
├── spotify_store.*      client id y token en NVS
├── spotify_auth.*       flujo OAuth con PKCE, sin client secret
├── spotify_http.*       HTTPS contra Spotify con las CAs del core
├── net.*                WiFi, NTP, API y servidor web
├── web_assets.h         panel web empaquetado (generado)
└── web_ui/              panel web: index.html, styles.css, app.js
```

Dos archivos son **generados**; si los editás a mano, se pierden:

| Generado | Con qué |
|---|---|
| `font5x7.h` | `python tools/gen_font.py esp32_gadget/font5x7.h` |
| `web_assets.h` | `python tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h` |

Olvidarse del segundo es el error más común: el equipo sigue sirviendo la versión
vieja del panel aunque hayas cambiado el HTML.

## Herramientas de desarrollo

En `../tools/`:

| Script | Para qué |
|---|---|
| `gadget_probe.py` | Maneja la consola serie desde Windows y guarda todo en un log. |
| `mock_gadget_api.py` | Mock de la API para probar el panel web en el navegador sin la placa. |
| `gen_font.py` | Genera la fuente desde glifos en ASCII art. |
| `pack_web.py` | Empaqueta el panel web dentro del firmware. |

El panel no tiene MISO, así que no se puede leer la pantalla por software. La
verificación honesta es el comando `dump` del firmware, que escribe el
framebuffer en ASCII por la consola.

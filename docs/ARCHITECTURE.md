# Arquitectura del gadget

Este documento es el mapa que necesita alguien que abre el repositorio por
primera vez y quiere cambiar algo sin romper lo que anda. La instalación y el
uso están en [`../esp32_gadget/INSTALL.md`](../esp32_gadget/INSTALL.md); acá se
explica **cómo está armado** y **dónde va cada cosa nueva**.

## Forma del sistema

El firmware es un ciclo único, sin sistema operativo propio ni scheduler: el
`loop()` lee el botón, atiende la consola serie, da servicio a la red y pide a
las pantallas que dibujen. Todo lo demás son módulos que ese ciclo llama.

```
esp32_gadget.ino        boot, botón, loop
├── console.*           consola serie: comandos, volcados, diagnóstico
├── screens.*           registro y ciclo de vida de las pantallas, menú, sleep
│   ├── screen_clock    reloj
│   ├── screen_slot     tragamonedas
│   ├── screen_game     meteoritos + calibración de ejes
│   ├── screen_net      datos de la red
│   ├── screen_music    Spotify
│   ├── screen_buddy    cara de Sofía
│   └── screen_config   brillo, rotación y bloqueo
├── gfx.* + display.*   framebuffer, formas, texto y panel ST7789
├── ui_color.h, theme.h paleta compartida
├── settings.*          NVS: credenciales, ajustes, récords, calibración
├── net.*               WiFi AP+STA, NTP, API HTTP y panel web
├── spotify_*.*         OAuth PKCE, consulta, parser y carátula
├── buddy_http.*        transporte del bridge: HTTP en la LAN, HTTPS validado
├── rotation.*          política de rotación aplicada al panel
├── imu.*, rgb.*        sensores y LED
└── buddy_*.*           estados, fases y expresiones de Sofía
```

## Regla de dependencias

De arriba hacia abajo, nunca al revés:

1. **Política pura** — `.h`/`.cpp` sin `Arduino.h`: `rotation_policy.h`,
   `game_difficulty.h`, `game_hits.h`, `config_value.h`, `screen_name.h`,
   `screen_order.h`, `steer_solve.h`, `tap_window.h`, `text_fold.cpp`,
   `buddy_face.h`, `buddy_anim.h`, `buddy_imu.h`, `buddy_url.h`, `buddy_link.h`,
   `ui_color.h` y los `spotify_parse/pkce`. Son la parte que se puede probar en la
   compu.
2. **Estado** — `settings.*` es el único dueño de NVS.
3. **Servicios** — `net.*`, `spotify*.*`, `imu.*`, `rgb.*`, `rotation.*`.
4. **Presentación** — `gfx.*`, `display.*`, `screens.*`, `screen_*`, `console.*`.

Una pantalla no habla HTTP directamente: lee estado ya resuelto (`spotifyState()`,
`netStatus()`, `buddyState()`) y dibuja. Un servicio no dibuja. Un archivo puro
no incluye `Arduino.h`; si lo necesita, la lógica no es pura y hay que separarla.

## Mapa de módulos

| Archivo | Responsabilidad | No debe hacer |
| --- | --- | --- |
| `esp32_gadget.ino` | Arranque, gestos del botón, orden del `loop()` | Comandos de consola, dibujo de pantallas |
| `console.*` | Parseo y ejecución de comandos serie, volcados, `api` en proceso | Estado de negocio |
| `screens.*` | Menú, orden, nombres, sleep, chrome común | Lógica de una pantalla puntual |
| `screen_*.cpp` | Estado y dibujo de una pantalla | Configurar red o NVS de otro módulo |
| `settings.*` | Leer/escribir NVS y exponer valores tipados | Validar reglas de UI |
| `net.*` | Red, servidor HTTP y handlers de la API | Formatear texto de pantalla |
| `rotation.*` | Aplicar modo/bloqueo y compensar ejes | Decidir gestos de pantalla |
| `gfx.*` / `display.*` | Primitivas de dibujo y envío al panel | Conocer pantallas |

## Estado y persistencia

Todo lo persistente vive en NVS, namespace `gadget`. Las claves son cortas por
límite del backend; esta tabla es la referencia para no inventar una nueva:

| Clave | Contenido | Dueño |
| --- | --- | --- |
| `cfg` | Blob `Settings` (WiFi, AP, brillo, récords, TZ) | `settings.cpp` |
| `orient` | Orientación fijada | `settings.cpp` |
| `rotMode` / `rotLock` | Modo de rotación y bloqueo | `settings.cpp` |
| `uiClr` / `bdEye` | Color de acento y de los ojos | `settings.cpp` |
| `rgbLvl` | Nivel del LED trasero | `settings.cpp` |
| `order` | Orden del menú de pantallas | `screens.cpp` |
| `name0`..`name6` | Nombres elegidos por el dueño | `settings.cpp` |
| `slpSec` / `slpMask` | Apagado automático | `settings.cpp` |
| `stLat`..`stValid` | Calibración de ejes del juego | `settings.cpp` |
| `bdUrl` / `bdTok` | Bridge de Sofía: `http://ip:puerto` en la LAN o `https://host` público | `settings.cpp` |
| `client_id` / `refresh` | Credenciales de Spotify | `spotify_store.cpp` |

Regla: un valor nuevo va a su propia clave cuando el blob `cfg` ya está en uso,
porque el blob se migra por tamaño y agregarle campos obliga a versionarlo. Un
valor que otro módulo escribe no se duplica en `Settings`.

## Convenciones

- **Un archivo, una responsabilidad.** Si un archivo necesita dos títulos para
  explicarse, probablemente sean dos archivos.
- **Nombres en inglés, interfaz en español.** Identificadores, comentarios
  técnicos y commits en inglés; lo que ve el dueño (pantalla, consola, panel)
  está en español.
- **`static` para lo local.** Una función que no cruza el archivo no se declara
  en el `.h`.
- **Errores explícitos.** Una operación que puede fallar devuelve `bool` o un
  código; no se ignora el resultado. La API devuelve `ApiResult` con código y
  JSON, nunca solo un string.
- **Sin números mágicos repetidos.** Un umbral o color que aparece dos veces en
  el mismo archivo se nombra en el `.h` del módulo.
- **Los límites se validan en un solo lugar.** El rango de brillo vive en
  `settings.*`; la pantalla y la API lo consumen, no lo reimplementan.

## Cómo extender

### Una pantalla nueva

1. Agregá el id al final de `screens.h` (`SCREEN_COUNT` crece) para no mover los
   ids que ya usa el panel.
2. Creá `screen_<nombre>.cpp` con `enter`, `tick` y `shortPress` (y `longPress`
   solo si reclama el gesto).
3. Registrala en las tablas de `screens.cpp`.
4. Sumá el nombre por defecto en `SCREEN_DEFAULT_NAMES`.
5. Si tiene una regla pura, va a un `.h` y a `tools/<regla>_test.cpp`.

### Un comando de consola

1. Agregá el `if (!strcmp(argv[0], "..."))` en `handleCommand` de
   `console.cpp`, con su ayuda en `printHelp`.
2. Usá `txReliable(true/false)` alrededor de una salida de varias líneas.
3. Si muta estado persistente, pasá por `settings.*` para que sobreviva al
   reinicio, igual que la pantalla equivalente.

### Un endpoint nuevo

1. Escribí un `static ApiResult apiX(const String &body)` en `net.cpp` y un
   `static JsonDocument parseBody(...)` con el guard común.
2. Registralo en la tabla de rutas `GET/POST` correspondiente.
3. Si el panel lo usa, actualizá `web_ui/app.js` **y** corré
   `python tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h`.
4. Probá el endpoint sin red con `api GET /api/...` desde la consola.

### Una regla nueva con prueba

1. Escribí la regla como función libre en un `.h`/`.cpp` sin Arduino.
2. Escribí `tools/<regla>_test.cpp` primero y mirá el rojo.
3. Si necesita fuentes acompañantes, agregala a `COMPANION_SOURCES` en
   `tools/run_tests.py`.

## Verificación

| Nivel | Qué corre | Comando |
| --- | --- | --- |
| Host | Lógica pura y panel empaquetado | `python tools/run_tests.py` |
| Compilación | Firmware completo con el FQBN real | ver INSTALL.md §3 |
| Consola | Comandos, `dump`, `api` en proceso | `python tools/gadget_probe.py` |
| Navegador | Panel sin placa | `python tools/mock_gadget_api.py` |
| Hardware | Gesto físico, pantalla, LED | manual |

El panel no tiene MISO, así que no se puede leer la pantalla por software: la
verificación honesta es `dump`, que escribe el framebuffer en ASCII.

## Decisiones registradas

- **Un solo botón.** Toda la interacción sale de una FSM (`button.h`) que
  distingue toque, doble toque y pulsación de un segundo.
- **Framebuffer completo en PSRAM.** Se dibuja y se empuja la pantalla entera;
  es más simple que dirty rectangles y entra de sobra en 8 MB.
- **La API es una función pura** de `(método, path, body)`. El servidor HTTP y
  la consola serie llaman a lo mismo, así que se puede probar sin red.
- **Assets web generados.** `web_assets.h` se empaqueta con `pack_web.py`; no se
  edita a mano. `verify_assets.py` lo comprueba contra `web_ui/`.
- **La rotación no espeja.** Solo se eligen las dos orientaciones no espejadas;
  una imagen espejada nunca es un resultado válido.

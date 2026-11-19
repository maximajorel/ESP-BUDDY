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
│   ├── screen_game     meteoritos (la calibración vive en CONFIG)
│   ├── screen_net      datos de la red
│   ├── screen_music    Spotify
│   ├── screen_buddy    cara de Sofía
│   └── screen_config   brillo, rotación y bloqueo
├── gfx.* + display.*   framebuffer, formas, texto y panel ST7789
├── ui_color.h, theme.h paleta compartida
├── settings.*          NVS: credenciales, ajustes, récords, calibración
├── admin_auth.*        credenciales del panel, sesiones y protección de rutas
├── net.*               WiFi AP+STA, NTP, API HTTP y panel web
│   └── net_ap_policy.h cuándo emite el AP (puro, test en host)
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
   `admin_auth.*`, `ui_color.h` y los `spotify_parse/pkce`. Son la parte que se
   puede probar en la compu.
2. **Estado** — `settings.*` es el único dueño de NVS.
3. **Servicios** — `net.*`, `spotify*.*`, `imu.*`, `rgb.*`, `rotation.*`.
4. **Presentación** — `gfx.*`, `display.*`, `screens.*`, `screen_*`, `console.*`.

Una pantalla no habla HTTP directamente: lee estado ya resuelto (`spotifyState()`,
`netStatus()`, `buddyState()`) y dibuja. Un servicio no dibuja. Un archivo puro
no incluye `Arduino.h`; si lo necesita, la lógica no es pura y hay que separarla.

### Quién puede tocar el panel web

El panel vive en `/admin` y pide sesión; `/` es público porque es donde van el
selector de pantalla y los juegos. La regla de qué pide sesión y qué no está en
`admin_auth.cpp` (`adminRequiresSession`), no repartida por los handlers:

- **Protegido**: toda página bajo `/admin` y todo verbo que escribe bajo `/api/`.
- **Público**: `/`, `/login`, los assets, `POST /api/session`, las lecturas
  (`GET /api/status`, `/api/scan`, `/api/spotify`, `/api/session`) y dos
  escrituras: `POST /api/screen` y `POST /api/face`.

Las dos escrituras públicas son la excepción deliberada (`adminIsPublicWrite`):
el selector de pantalla y el juego viven fuera del login. Una cambia qué muestra
el panel y la otra, por unos segundos, qué expresión tiene la cara. Ninguna toca
credenciales, red ni almacenamiento, las dos son reversibles por el dueño, y se
comparan exacto: una barra de más o cualquier otro verbo vuelve a pedir sesión.

La autenticación se aplica en el **transporte** (`net.cpp`), nunca dentro de
`netHandleApi`, porque la consola serie llama al dispatcher en proceso y llegar al
USB ya significa tener el equipo en la mano: `api POST /api/wifi {...}` sigue
funcionando sin sesión a propósito.

Las credenciales se guardan como sal + SHA-256 iterado, calculado por
`adminHashDerive`. El build hace el mismo cálculo en Python
(`tools/gen_admin_defaults.py`) para sembrar el valor del `.env`, y
`tools/admin_seed_test.py` fija que las dos implementaciones coincidan.


## Mapa de módulos

| Archivo | Responsabilidad | No debe hacer |
| --- | --- | --- |
| `esp32_gadget.ino` | Arranque, gestos del botón, orden del `loop()` | Comandos de consola, dibujo de pantallas |
| `console.*` | Parseo y ejecución de comandos serie, volcados, `api` en proceso | Estado de negocio |
| `screens.*` | Menú, orden, nombres, sleep, chrome común | Lógica de una pantalla puntual |
| `screen_*.cpp` | Estado y dibujo de una pantalla | Configurar red o NVS de otro módulo |
| `settings.*` | Leer/escribir NVS y exponer valores tipados | Validar reglas de UI |
| `admin_auth.*` | Hash de la credencial, tabla de sesiones, throttle de login y qué rutas piden sesión | Tocar NVS, leer la cookie o decidir códigos HTTP |
| `net.*` | Red, servidor HTTP y handlers de la API; aplica la sesión a cada request | Formatear texto de pantalla |
| `rotation.*` | Aplicar modo/bloqueo y compensar ejes | Decidir gestos de pantalla |
| `rgb.*` | Color, intensidad, efectos y dormir/prender del LED | Decidir qué color mostrar |
| `buddy_led.*` | La luz trasera como semáforo de Sofía mientras su bridge esté configurado | Dibujar en el panel |
| `gfx.*` / `display.*` | Primitivas de dibujo y envío al panel | Conocer pantallas |

### Quién manda en el LED trasero

Un solo píxel, dos dueños posibles: el **panel** (color, modo e intensidad que
elige el dueño) y **Sofía**, que usa el LED como luz de estado cuando su bridge
está configurado. `buddy_led.cpp` repinta su color de fase en cada vuelta del
loop, así que mientras Sofía esté configurada cualquier color elegido en el panel
se perdería en la vuelta siguiente: el panel lo dice y deja solo la intensidad,
que se multiplica sobre el color de ella. Sin bridge configurado, el LED es del
panel.

Un comando explícito del dueño (`rgb` en la consola, o el panel) pinta el LED en
el acto con `rgbApplyNow()` y lo despierta si el apagado automático lo había
dormido: el brillo que responde la API tiene que ser el que se ve.

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
- **El stack se pide donde se gasta.** `build_opt.h` sube el stack del lazo a
  16 KB porque el panel corre ahí y un pedido a Sofía por HTTPS hace el handshake
  TLS en esa tarea: con los 8 KB que da el core por defecto el desborde cae en el
  heap y se ve como `CORRUPT HEAP`, no como un stack. Con TLS de por medio, pedí
  el presupuesto de la tarea dedicada (12 KB) en vez de confiar en el default.
  Ese archivo solo acepta flags del compilador: un comentario ahí rompe el build.

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
3. Si el panel o la entrada pública lo usan, actualizá `web_ui/app.js` o
   `web_ui/landing.js`, y corré
   `python tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h`.
   El mock (`tools/mock_gadget_api.py`) tiene que reflejar la misma regla de
   acceso que el transporte; si no, la prueba sin placa miente.
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

# Contribuir

Gracias por mirar el proyecto. Esto es un equipo real, con un dueño y una placa
en un escritorio: el objetivo de una contribución no es sólo que compile, es que
la pantalla siga mostrando lo que el dueño espera y que se pueda probar sin
adivinar.

Antes de escribir código, leé estos tres archivos. Son cortos y evitan el 90 % de
las preguntas:

| Archivo | Para qué |
|---|---|
| [`AGENTS.md`](AGENTS.md) | Reglas duras, comandos, trampas del entorno y qué se considera terminado |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Cómo está armado, las capas, las claves de NVS y cómo extenderlo |
| [`esp32_gadget/INSTALL.md`](esp32_gadget/INSTALL.md) | Instalación y uso, con la configuración de placa exacta en §3 |

## Qué es y qué no es

- **Es** el firmware de un gadget personal (siete pantallas, un botón, panel web)
  más una app de bandeja en Tauri y las herramientas para probarlos.
- **No es** una librería ni un producto con soporte. No hay `LICENSE` en el
  repositorio: el código es del dueño, así que preguntá antes de reutilizarlo.
- **No hay usuarios anónimos**: hay un dueño, una placa y decisiones de producto
  que ya están tomadas. Una idea nueva se conversa antes de implementarse.

## Requisitos

| Pieza | Versión / nota |
|---|---|
| `g++` (o `clang++`) | C++17. Es lo único que hace falta para las pruebas de host. |
| Python 3 | Scripts de pruebas, empaquetado y consola serie. |
| `arduino-cli` (o Arduino IDE 2.x) | Probado con `arduino-cli 1.6.0` y el core `esp32:esp32 3.3.12`. |
| Librerías Arduino | *Adafruit NeoPixel*, *ArduinoJson* 7.x, *JPEGDEC* |
| `pyserial` | Sólo para `tools/gadget_probe.py` |
| Node.js 18+ y Rust estable | Sólo para `tray_buddy/` |

El proyecto se desarrolla en Windows con WSL como terminal. En WSL el único
`arduino-cli` suele ser el de Windows: el `Makefile` acepta rutas de Windows por
parámetro y trae un ejemplo en su encabezado. La placa **no** es visible desde
WSL; la consola serie se maneja con el Python de Windows.

## Puesta a punto

```bash
git clone git@github.com:maximajorel/ESP-BUDDY.git
cd ESP-BUDDY

# 1. Credencial del panel (sólo para sembrar el equipo la primera vez)
cp .env.example .env        # editá ADMIN_USER y ADMIN_PASS
make seed                   # genera esp32_gadget/admin_seed.h

# 2. La suite de host, que es la red de seguridad del repo
make test                   # 45 tests + round trip de web_assets.h

# 3. El panel web empaquetado dentro del firmware
make pack                   # sólo si tocaste esp32_gadget/web_ui/

# 4. Compilar el firmware con el FQBN real
make build
```

`.env` y `esp32_gadget/admin_seed.h` no se versionan: el firmware guarda una sal
y un hash, nunca la contraseña en claro, y la credencial se puede cambiar después
desde `/admin` sin recompilar.

En una consola nativa de Windows, `make test` necesita `PYTHON=python`.

## Probar sin la placa

La mayor parte del proyecto se puede verificar en la compu, y eso es deliberado:

```bash
python3 tools/run_tests.py --verbose          # todas las pruebas de host
python3 tools/run_tests.py --filter buddy     # una sola, mientras iterás
python3 tools/mock_gadget_api.py 8791         # el panel, en el navegador
```

El mock implementa el contrato real de la API y registra cada pedido que muta
estado en `tools/mock_requests.log`: sirve para probar qué mandó el panel, no
sólo que se veía bien. **Si agregás un endpoint, actualizá el mock con la misma
regla de acceso que el firmware**; si no, la prueba sin placa miente.

Los tests puros corren con `g++` porque la lógica está separada a propósito:
`rotation_policy.h`, `buddy_face.h`, `display_blit.h`, `config_value.h`,
`calendar_parse.cpp`, `weather_parse.cpp` y compañía no incluyen `Arduino.h`. Si
tu regla nueva necesita `Arduino.h` para compilar, la regla no está aislada.

## Probar con la placa

La pantalla **no tiene MISO**, así que no existe una forma de leerla por
software. La verificación honesta es el comando `dump` de la consola serie, que
escribe el framebuffer en ASCII.

1. Leé el puerto: `arduino-cli board list`. No lo adivines.
2. Asegurate de que ningún monitor, sonda o IDE esté usando el puerto.
3. `make flash PORT=COM5` (o el `arduino-cli upload` equivalente).
4. Confirmá `Hash of data verified` en la salida del upload.
5. Verificá con una secuencia reproducible guardada en `tools/probes/`:

```powershell
python tools\gadget_probe.py @tools\probes\config_screen.txt
```

Abrir el puerto reinicia la placa: la primera línea del sondeo espera el banner
(`boot 4`). Si tu cambio es visible, agregá o extendé un `.txt` en
`tools/probes/` y pegá la evidencia (`dump`, valores de `api GET /api/status`) en
el documento de la feature.

**No flashees sin autorización del dueño en la conversación actual.** El dueño
decide cuándo se sube algo al hardware.

## App de bandeja (`tray_buddy/`)

```bash
cd tray_buddy
npm install
npm run dev                 # desarrollo
npm run build               # instalador NSIS en src-tauri/target/release/bundle/nsis/

cd src-tauri
cargo test                  # transporte, store, agenda y arranque, sin placa
```

`cargo test` levanta un servidor HTTP falso en `127.0.0.1` y le habla con el
cliente real, así el transporte se prueba sin un ESP32 conectado. Para mirar el
popover con datos sin compilar nada está `node tools/preview.js day`.

## Flujo de trabajo

1. **Rama por feature**, desde `main`: `feat/<nombre>`, `fix/<nombre>`,
   `refactor/<nombre>`.
2. **Si el cambio es grande**, primero el documento: `odd/tasks/<feature>.md` con
   tres secciones y en este orden:
   - `## Specs` — requisitos numerados `S#`, **citando verbatim** el pedido del
     dueño, sus mensajes de error y sus ejemplos. No se resumen ni se agregan
     requisitos que nadie pidió.
   - `## Tasks` — una línea por tarea: id, `S#` que cubre, ruta y commit.
   - `## Log` — `L1` es el pedido original verbatim; después, correcciones,
     evidencia y decisiones, con números medidos cuando los haya.
3. **Test primero** cuando hay una regla de comportamiento: escribí el test,
   mirá el rojo, implementá, mirá el verde. Cada test afirma los efectos
   observables de la regla (código de salida, salida exacta, y que una entrada
   rechazada no cambie nada).
4. **Commits en unidades revisables**, de unos 400 líneas de diff como techo
   cómodo, con el test y la documentación junto al comportamiento que describen.
5. **Pull request** con la suite verde. El CI corre la suite de host en cada push
   a `main` y en cada PR; el firmware no se compila ahí (necesita ~2 GB de core),
   así que `make build` es un gate local y hay que decir si corrió.

### Mensajes de commit

`tipo(alcance): resumen`, con el tipo en inglés y el resumen en español, que es
lo que muestra el historial:

```
feat(calendario): escribir eventos en Google
fix(tray): el chip de efecto manda lo que el dueño eligió
docs(calendar): la causa raíz, resuelta, y la agenda real en el popover
test(panel): los nombres de clave de NVS, con sus límites
```

Tipos en uso: `feat`, `fix`, `docs`, `test`, `refactor`, `chore`. El alcance es
el módulo o el área tocada (`buddy`, `calendar`, `tray`, `web`, `panel`, `api`,
`tools`, `design`, `odd`).

## Reglas de código

Están completas en [`AGENTS.md` § Reglas duras](AGENTS.md#reglas-duras). Las que
más se olvidan:

- **Un archivo, una responsabilidad.** Si necesita dos títulos para explicarse,
  son dos archivos.
- **Identificadores y comentarios técnicos en inglés; lo que ve el dueño en
  español** (pantalla, consola, panel, instalador). La documentación del repo va
  en español.
- **Errores explícitos**: `bool` o un código, o `ApiResult` con código y JSON.
  Nunca se ignora el resultado de algo que puede fallar.
- **Los límites se validan en un solo lugar.** El brillo vive en
  `config_value.h`; `settings.*` y la API lo consumen.
- **El stack se pide donde se gasta.** Con TLS de por medio, la tarea dedicada
  pide su presupuesto de stack (12 KB) en vez de confiar en el default.
- **Los archivos generados no se editan**: `web_assets.h`, `font5x7.h`,
  `admin_seed.h`.

## Áreas que piden cuidado extra

Un cambio acá se revisa con otra lupa, porque el error se descubre tarde o no se
descubre:

| Área | Por qué |
|---|---|
| Autenticación y rutas públicas (`admin_auth.cpp`) | Decide qué pide sesión. Las excepciones son nominales y exactas; una barra de más tiene que volver a pedir sesión. |
| Credenciales y tokens (NVS, OAuth de Google/Spotify) | Un token filtrado o mal guardado no se puede deshacer. El firmware guarda sal + hash, nunca contraseñas. |
| Claves de NVS | Agregar un campo al blob `cfg` obliga a versionarlo. Un valor nuevo va a su propia clave; una feature con credenciales o preferencias propias usa su namespace (`calendar`, `weather`). |
| Migraciones de datos guardados en el equipo | Un blob viejo tiene que seguir leyéndose; no hay forma de recuperar lo que se sobrescribió. |
| Escrituras al calendario (`If-Match`/`etag`) | Sin `etag` se pisa un cambio ajeno. Un `412` es el resultado correcto, no un bug. |
| `build_opt.h` | Sólo acepta flags: un comentario ahí rompe el build. |
| Diseño de la cara de Sofía | Tiene su propia skill y reglas ganadas a fuerza de correcciones del dueño: leé `.agents/skills/sofia-buddy-face/SKILL.md`. |

## Diseño y documentación

- Las direcciones visuales viven en `design/<slug>/`, y los tokens en
  `design/system.md`. Una pantalla nueva del panel no inventa una paleta.
- El panel de administración es papel y tinta: sin gradientes y sin
  `box-shadow`; la elevación es color más un borde negro de 1 px.
- Un documento nuevo se escribe **en español**, con la misma voz que los que ya
  están: frases que explican el porqué, no adjetivos.

## Reportar un bug

Un reporte útil trae evidencia, no impresiones:

1. Qué esperabas y qué pasó.
2. Versión del firmware (`esp32-gadget 1.0.0` sale en el banner) y commit.
3. El log de la sonda serie (`tools/gadget_probe.log`) o el `dump` de la
   pantalla, con el comando exacto que lo produjo.
4. Si es del panel: qué endpoint y qué cuerpo se mandaron (mirá
   `tools/mock_requests.log` si fue contra el mock).
5. Si es intermitente: cuántas veces de cuántas, y qué había en el equipo en ese
   momento (WiFi, sesión, calendario conectado).

## Proponer una feature

Traé el problema antes que la solución: qué tiene que pasar en pantalla o en el
equipo, con qué datos, y qué pasa cuando falla. Las specs del documento van con
tus palabras citadas textualmente, porque dentro de tres meses nadie va a
recordar si el pedido decía "el mes" o "la semana". Si la idea cambia el alcance
de algo ya hecho, se conversa antes de escribir código.

## Licencia

El repositorio no incluye un archivo de licencia. El código pertenece al dueño:
preguntá antes de reutilizarlo, publicarlo o distribuirlo.

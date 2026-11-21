# Concentración: segundos desde la web y el número gigante en el vidrio

Tercera vuelta sobre el temporizador de estudio (ver `concentracion.md`, S7 y S11,
que este documento reemplaza en lo que toca). El dueño quiere poder poner
**segundos** desde la web, y quiere que en el vidrio el reloj sea el
protagonista: número grande abajo, ojos chicos arriba.

## Specs

S1. Segundos desde la web — verbatim: "El reloj temporizador debe permitirme
desde la web UI poner segundos tambien." La landing pública
(`/`, sin sesión, la misma autoridad de `POST /api/focus`) ofrece minutos **y**
segundos, manda una sola cantidad de segundos y muestra el restante en `mm:ss`.

S2. El número manda en el vidrio — verbatim: "En la pantalla lcd vamos a poner
bien grande el temporizador y por encima los ojos chiquitos, osea el temporizador
debe ocupar un poco mas que la mitad del alto de la pantalla lcd del ESP." Sobre
el panel de 320x172 el número `MM:SS` en siete segmentos ocupa 100 px de alto
(58% del alto) y los ojos se dibujan chicos (≈55% de su tamaño normal) en la
banda superior, sin poder tocar los dígitos ni su placa negra.

S3. La unidad de verdad es el segundo — el temporizador guarda y cuenta
segundos. Rango útil: 1 s a 99:59 (5999 s), que es lo máximo que entra con
cuatro dígitos grandes. `0` sigue cancelando. Un valor por encima del máximo se
recorta al máximo, igual que hoy con los minutos: "un campo web es una
sugerencia, no un contrato" (`buddy_focus.h`).

S4. El contrato HTTP no rompe a nadie — `POST /api/focus` acepta `seconds`
(nuevo) y sigue aceptando `minutes` (compatibilidad). Mandar los dos es un error
de cliente y devuelve `400`; un valor no entero o negativo devuelve `400` y no
toca una sesión en curso (regla que ya existía). `GET /api/status` publica
`seconds` (duración total) además de `remainingMs`, y la landing deja de
aproximar con minutos.

S5. Nada de lo ganado se pierde — la sesión sigue sobreviviendo a cambios de
fase del bridge, `/api/face`, cambio de pantalla y apagado del vidrio
(`concentracion.md` S4, S12), y el logro final sigue igual (palabra + barra llena
+ LED verde).

S6. Lógica pura y testeable — la aritmética de segundos, el recorte y el formato
`mm:ss` viven en `buddy_focus.h` (sin Arduino) y los fija
`tools/buddy_focus_test.cpp`, compilado por `python3 tools/run_tests.py`.

S7. El mock no miente — `tools/mock_gadget_api.py` refleja la misma validación
que el firmware, para que la prueba del panel sin placa siga siendo honesta.

## Tasks

| Task | Specs | Ruta | Commit |
| --- | --- | --- | --- |
| T1: documento de la etapa y trackeo | — | inline | feat(concentracion) |
| T2: RED de segundos, recorte y formato | S3, S6 | inline | feat(concentracion) |
| T3: `buddy_focus.h` en segundos + geometría nueva | S2, S3, S6 | inline | feat(concentracion) |
| T4: ojo chico arriba, número gigante, barra fina | S2, S5 | inline | feat(concentracion) |
| T5: `seconds` en `/api/focus` y `/api/status` | S3, S4 | inline | feat(concentracion) |
| T6: minutos + segundos en la landing y `web_assets.h` | S1, S4 | inline | feat(concentracion) |
| T7: mock con la misma regla, docs y verificación | S7 | inline | feat(concentracion) |

## Log

L1 — pedido del dueño, verbatim:

> 4.- El reloj temporizador debe permitirme desde la web UI poner segundos
> tambien. En la pantalla lcd vamos a poner bien grande el temporizador y por
> encima los ojos chiquitos, osea el temporizador debe ocupar un poco mas que la
> mitad del alto de la pantalla lcd del ESP

L2 — decisión del dueño sobre el orden de las etapas: "Reloj primero".

L3 — RED observado antes de implementar: `python3 tools/run_tests.py --filter
focus` → `buddy_focus_test compile` fallando por `BUDDY_FOCUS_DIGIT_W`,
`BUDDY_FOCUS_COLON_W` y `BUDDY_FOCUS_MAX_SECONDS` inexistentes. Después de
implementar la aritmética, un chequeo quedó rojo (`the minutes clamp is the
documented maximum`) porque el recorte de minutos caía en el techo de segundos
(99:59) en vez de en 99:00; se corrigió el wrapper para recortar los minutos
primero. GREEN: `all buddy_focus checks passed`.

L4 — verificación de la etapa:

- `python3 tools/run_tests.py` → `33 passed` (incluye `web_assets.h`).
- `python3 tools/verify_assets.py esp32_gadget/web_ui esp32_gadget/web_assets.h`
  → `PASSED (0 problems)` después de repackear.
- `arduino-cli compile` con el FQBN de `INSTALL.md` §3 → 1534894 bytes (48%) de
  flash y 55884 bytes (17%) de RAM. Compila.
- Geometría medida sobre la placa de 320x172: ojos en y 22..46 (cy 34, ry 12),
  barra en y 56..61 (200 px de ancho), placa negra desde y 64, dígitos de 64x100
  en y 66..166 y x 5..315. El número ocupa el 58% del alto del vidrio.
- Hardware pendiente: la placa está en COM5 pero el flasheo pide autorización
  explícita (regla de `.agents/skills/esp32-gadget-flash`). Sin flashear, para
  esta etapa.

# Config screen: on-device LCD and rear-LED brightness

Adds a seventh screen (`CONFIG`, appended last) that adjusts the LCD backlight and the rear WS2812 bead level with the single button, and makes the bead level survive a power cycle.

Branch: `feat/screen-settings` (chained from `feat/buddy-squircle-expressions`, HEAD 3b619ca).

## Specs

S1. "crear una pantalla de configuracion, por defecto al final" — a new screen with id 6 appended after `SCREEN_BUDDY`; `SCREEN_COUNT` becomes 7. The default pick order stays the identity, so CONFIG sits last, and an owner whose stored order has 6 entries is migrated by the existing `count-1` upgrade in `settingsGetScreenOrder` with CONFIG last.

S2. "1.- Brillo del LCD" — the screen changes the LCD backlight, applies it immediately with `displaySetBacklight`, and persists it in `settings.brightness` (the same value the web panel edits, `5..100`).

S3. "2.- Brillo del LED trasero" — the screen changes the rear bead intensity through `rgbSetLevel`, applies it immediately, and persists it so a reboot restores it. The panel's `rgb.level` uses the same storage.

S4. Decisions taken in this session:
  - Gestures: "tap = cambiar valor, doble tap = cambiar fila", "hold → selector de pantallas".
  - LED persistence: "Sí, clave NVS rgbLevel", applied at boot by `rgbBegin()`/sketch setup.
  - LED scale on the glass: "Porcentaje 0-100%, pasos de 10", mapped internally to `0..255`.
  - Web panel scope: "Solo lo mínimo: 7º tile y persistir rgb.level".

S5. A single button must not trap the owner: the hold inside CONFIG still opens the screen picker, exactly as on every other screen. Only a short tap and a double tap belong to the screen.

S6. The web panel recognises the new screen: `DEFAULT_NAMES` gains `CONFIG`, the picker gets a seventh tile, and `POST /api/settings` stores `rgb.level` instead of only applying it live.

## Tasks

| ID | Task | Specs | Route | Commit |
| --- | --- | --- | --- | --- |
| T1 | Pure `config_value.h`: step with wrap, LCD clamp, LED percent↔level mapping, plus `tools/config_value_test.cpp` written first | S2,S3,S4 | inline, test-first | 4824986 |
| T2 | `settingsGetRgbLevel`/`settingsSetRgbLevel`/`settingsSetLcdBrightness` in `settings.cpp`, applied at boot, `net.cpp` stores `rgb.level` | S3,S6 | inline | 4824986 |
| T3 | `screen_config.cpp` (declared in `screens.h` like every other screen) and the carousel wiring | S1,S2,S3,S5 | inline | 2cab202 |
| T4 | Web panel: `DEFAULT_NAMES`, seventh tile, repacked `web_assets.h` | S6 | inline | 2cab202 |
| T5 | Verification: g++ tests, asset check, statement of what could not run here | all | inline | — |

Slice boundary: 4824986 = the storage and value rules (host-testable, no screen).
2cab202 = the screen, the carousel wiring, the panel tile and the docs. The
second slice's raw line count is dominated by the generated `web_assets.h`
(repacked, verified by `tools/verify_assets.py`); the authored change is the
screen module plus the wiring.

## Log

L1 (verbatim, user):

```
Hace commit de lo actual.

Luego quiero que veamos la forma de crear una pantalla de configuracion, por defecto al final, que me permita configurar:

1.- Brillo del LCD
2.- Brillo del LED trasero
```

L2 (verbatim, user decisions):

```
1. ¿Cómo se maneja la pantalla con un solo botón? — tap = cambiar valor, doble tap = cambiar fila
2. El brillo del LED hoy no se guarda. ¿Lo hacemos persistente? — Sí, clave NVS rgbLevel
3. ¿Qué escala mostramos para el brillo del LED (0..255 en el hardware)? — Porcentaje 0-100%, pasos de 10
4. ¿Qué hacemos con el panel web en el mismo trabajo? — Solo lo mínimo: 7º tile y persistir rgb.level
```

L3 (evidence): `git status` on `feat/buddy-squircle-expressions` at session start reported "nothing to commit, working tree clean" at 3b619ca, so the first request produced no commit. A dedicated branch `feat/screen-settings` was created from that HEAD.

L4 (evidence): no `arduino-cli` in this environment (`which arduino-cli` empty; the documented build is a Windows `arduino-cli` invocation in INSTALL.md). Firmware compile and on-device checks cannot run here; only host-side g++ tests and the web asset check can.

L5 (evidence, corrected): `arduino-cli.exe` under `/mnt/c/Program Files/Arduino CLI` is reachable from WSL, so L4 was wrong about the compile. `arduino-cli compile` with the INSTALL.md FQBN and `--warnings all` exits 0 (1408354 bytes, 44% of program space). On-device behaviour (real NVS migration, tap timing, actual panel and bead) still cannot be verified here: there is no board attached.

L6 (verification): every `tools/*_test.cpp` compiles and passes with g++ except `spotify_core_test` (needs the ArduinoJson include path, and the Spotify code was not touched here) and `text_fold_test`, which needs its companion `text_fold.cpp` and passes with it. `python3 tools/verify_assets.py` reports 0 problems after the repack.

L7 (decision): console `bl` and `rgb <r> <g> <b> <nivel>` now persist, because a console tweak that vanished on reboot would contradict a screen that shows the same value. Not requested explicitly; recorded here because it changes two existing commands.

L8 (hardware evidence): the firmware was flashed to the board on COM5 (`arduino-cli upload`, hash verified) and driven over the serial console with `python tools/gadget_probe.py @tools/config_cmds.txt` (Windows Python + pyserial through PowerShell). Observed, in order:

- `hold -> menu en SOFIA`, six `tap`, then `menu -> CONFIG`: the picker reaches the new screen by the real gesture path.
- `dump 4` on entry: title, two rows, `BRILLO LCD` at 90% and `BRILLO LED` at 25%, caret on the LCD row.
- `tap` -> `config: brillo lcd 100%`; the next `dump` shows the first bar full and `100%`, and `display.brightness` 100 in `/api/status`.
- `doubletap` -> `config: fila led`; `tap` -> `config: brillo led 35% (89/255)`; the third `dump` shows the caret on the second row, its bar at 35% and the value `35%`.
- `/api/status` reports `order:[5,0,4,2,1,3,6]`: this board already had a customised six-entry order, and the migration appended CONFIG last without touching the owner's order. `names` ends with `CONFIG`.
- After a reboot (reopening the port resets the board), `brightness` is still 100 and `rgb.level` still 89: the LCD value and the bead level are stored, and the boot restores the bead.

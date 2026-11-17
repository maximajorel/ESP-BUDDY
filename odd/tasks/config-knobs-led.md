# Config knobs: −/+ controls, no value wrap, and a rear LED that answers at once

The CONFIG screen's bars get a `−` and a `+` button each, the button walks the
focus between those buttons with a double tap and presses with a single tap, the
values stop wrapping (100 + 1 used to land on 5% and look like the screen had
turned off), and the web panel's LED intensity becomes a percentage.

Branch: `feat/config-knobs-led` (from `feat/buddy-bridge-https`, HEAD f08553b).

## Specs

S1. "No funciona tan bien el selector de brillo y color de la luz led, no del LCD
  si no del led trasero..." — the rear WS2812 must answer an owner command even
  when the glass is asleep, and the panel's colour/intensity/mode must be the
  values actually on the bead.

S2. "en el modulo de config, cuando cmabio la rotacion o algo, se apaga la
  pantalla o no funciona del todo bien." — a CONFIG value must never jump to the
  other end of its range because of one extra tap (`100%` + a tap used to land on
  the `5%` floor, which reads as a dark screen), and changing the rotation must
  not discard the highlighted row nor re-enter the screen.

S3. "prefiero que en los deslizadores agregemos un + - al lado del slider y
  prefiero usar eso, navegando con doble tap entre esos botones y con un tap por
  cambiar el parametro... Aunque me lleve mas taps navegar la totalidad es mas
  practico" — each brightness row draws `−`, the bar and `+`; double tap moves
  the highlight over the controls (`LCD−`, `LCD+`, `LED−`, `LED+`, `ROTACION`,
  `BLOQUEO`, wrapping), a single tap acts on the highlighted control. Hold still
  opens the screen picker.

S4. "En la web UI el slider para el brillo quiero que sea por % no 255 como numero
  maximo" — the LED intensity control is `0..100` percent (step 5), shows `%`, and
  the panel converts to the bead's `0..255` level when it talks to the device.
  The HTTP API keeps `level` in `0..255`, the same value the console speaks.

S5. Decisions taken in this session:
  - Steps saturate instead of wrapping: `+` at the ceiling stays there, `−` at the
    floor stays there. The rotation row still cycles its three modes.
  - The focus survives a rotation change and a panel write; it resets only when
    the owner leaves CONFIG.
  - A rotation change repaints by pushing the current framebuffer, not by
    re-entering the screen (`screensGoTo` ran the enter hook and the music/CONFIG
    leave hooks again).
  - The panel keeps its explicit `Aplicar` button; the status poll must not
    overwrite a control the owner is editing, including its `<output>` text.

S6. Nothing else changes: the picker order, the screen names, the NVS keys
  (`rgbLvl`, `cfg.brightness`), the console commands and the API routes.

## Tasks

| ID | Task | Specs | Route | Commit |
| --- | --- | --- | --- | --- |
| T1 | `config_value.h`: saturating `cfgStepUp`/`cfgStepDown`, `tools/config_value_test.cpp` written first | S2,S3,S5 | inline, test-first | |
| T2 | `screensRepaint()` and the rotation path that uses it; CONFIG focus reset moved to `configLeave` | S2,S5 | inline | |
| T3 | CONFIG screen: `−`/`+` buttons, double-tap focus walk, single-tap press, percent values | S3 | inline | |
| T4 | `rgb.cpp`: an explicit colour/level/effect command repaints immediately and wakes a sleeping bead | S1 | inline | |
| T5 | Web panel: intensity in percent, outputs and controls protected from the poll, repacked `web_assets.h` | S4,S5 | inline | |
| T6 | Verification: g++ suite, asset round trip, firmware compile, mock-API browser check, docs | all | inline | |

## Log

L1 (verbatim, user):

```
No funciona tan bien el selector de brillo y color de la luz led, no del LCD si no del led trasero... 


Luego en el modulo de config, cuando cmabio la rotacion o algo, se apaga la pantalla o no funciona del todo bien. verifiquemos y corramos tests


Ademas en ese modulo prefiero que en los deslizadores agregemos un + - al lado del slider y prefiero usar eso, navegando con doble tap entre esos botones y con un tap por cambiar el parametro... Aunque me lleve mas taps navegar la totalidad es mas practico


En la web UI el slider para el brillo quiero que sea por % no 255 como numero maximo
```

L2 (evidence): `python3 tools/run_tests.py` at f08553b: 22 passed, 0 problems in
`web_assets.h`. The working tree was clean on `feat/buddy-bridge-https`, whose
history contains `master`; the feature branch was cut from that HEAD.

L3 (diagnosis, S2): `screen_config.cpp` called `cfgStepUp`, whose rule pinned by
`tools/config_value_test.cpp` was "100 -> 5, the floor and not 0". Pressing once
more at 100% therefore dropped the backlight to 5% and the panel looked off; the
same value rule also forced a full wrap to reach a lower value. The rotation
change additionally went through `screensGoTo(screensCurrent())`, which re-ran
`configEnter()` (fresh row, reset tap window) and the leave hooks of the screen
it was already on.

L4 (decision, chosen by the owner): when Sofía's bridge is configured the bead is
hers. `buddy_led.cpp` repaints her phase colour every loop, so the panel says so,
hides colour and mode, and keeps the intensity, which multiplies over her colour.
Owner's answer, verbatim: "2. Sofía manda: el panel lo dice".

L5 (evidence, S2): the dark screen had a concrete cause in the value rule:
`cfgStepUp` wrapped `100 -> 5`, so one tap too many on BRILLO LCD dropped the
backlight to the floor. The new rules saturate; `tools/config_value_test.cpp` was
changed first and failed to compile (`cfgStepDown` did not exist), then failed on
the walk assertions until the rule matched.

L6 (verification, host):
- `python3 tools/run_tests.py`: 22 passed, 0 problems in `web_assets.h`.
- `python3 tools/verify_assets.py esp32_gadget/web_ui esp32_gadget/web_assets.h`:
  0 problems after the repack (98667 bytes).
- `arduino-cli compile` with the INSTALL.md FQBN and `--warnings all`: exit 0,
  1427874 bytes (45% of program space), no new warning from the touched files.

L7 (verification, panel, against `tools/mock_gadget_api.py` on 8791 in headless
Chrome): with `buddy.configured` true the card showed the note, hid the colour and
mode rows, and `Aplicar` sent `{"rgb":{"level":153}}` for 60% — only the level,
level = 0..255. With `buddyToken:""` posted to the mock (configured false) the
rows came back and `Aplicar` sent
`{"rgb":{"level":89,"r":220,"g":20,"b":60,"effect":1}}` for 35% and `#dc143c`.
A slider left at 55% without applying still read 55 % after a poll cycle, so the
status refresh no longer stomps the control or its label.

L8 (not verified here): the physical bead and the panel pixels. The hardware
checks are `dump 4` on CONFIG (two bars with − and + and the focused box filled),
a tap on `+` and on `−`, a double tap walking the focus, a rotation change with
the row preserved, and the bead answering a panel write while the glass is
asleep. They need the board flashed with this branch.

L9 (bookkeeping): the repository hook quotes authored lines "since master", and
`master` is an ancestor of the branch this work started from (`feat/buddy-bridge-https`,
HEAD f08553b), so its count includes earlier features. This feature's slices are
`6357825` (value rules), `b9293c5` (repaint), `a20cda2` (CONFIG buttons),
`b311757` (LED applies at once), `47c9acf` (panel) and `71b6952` (this document).
The `web_assets.h` slice is generated: `tools/verify_assets.py` is what proves it.

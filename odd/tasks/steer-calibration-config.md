# Guided steering calibration moves from Meteoritos to CONFIG

The five-step tilt wizard lived inside the meteor game: a double tap on the
ready/over screen opened an INICIAR CALIBRACION prompt, and the wizard took over
the game screen. The owner wants it moved into CONFIG and removed from the game,
so the game is only the game and calibration is a setting.

## Specs

S1. Calibration in CONFIG — verbatim: "Y respecto a la calibracion, metamos el
wizard de calibracion en la config y lo saquemos del juego de meteorito dale?"
CONFIG gains a fifth row, **CALIBRAR**, that opens the same guided wizard on the
CONFIG screen: centre, then left, right, toward and away; each step closes with a
tap only after a real tilt and a return to centre; the progress dots and the live
deviation bar stay; a one-second hold cancels and keeps the previous calibration.
The solved axes, signs and baselines are written to the same NVS the game reads,
the row then shows **HECHA** (or **HACER** when there is no valid calibration),
and the panel's **Recalibrar controles** button clears it, jumps to CONFIG and
opens the wizard directly.

S2. No calibration in the game — verbatim: "lo saquemos del juego de meteorito".
The game loses the prompt, the wizard drawing and its states, the double-tap
gesture, the hold-to-confirm and the "2 TOQUES RAPIDOS: CALIBRAR" hints. A tap on
the ready/over screen starts or restarts the run immediately. When no calibration
is stored, the ready/over screen says the calibration is done from CONFIG, and
the game plays with the stored or factory axes.

S3. The wizard keeps the glass — while CONFIG runs the wizard it stays awake even
if CONFIG is included in the auto-off mask, and automatic rotation is held so the
very tilts being measured do not flip the image. Both are released when the
wizard ends.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S1 | inline: pure `steer_wizard.h` (centre average, gate, four captures, solver) + `steer_wizard_test` | 97356c0 |
| T2 | S1,S3 | inline: CONFIG CALIBRAR row, wizard drawing/input, NVS write, awake+rotation hold | 97356c0 |
| T3 | S2 | inline: strip the wizard/prompt/gesture from `screen_game.cpp`, routing, console | 97356c0 |
| T4 | S1 | inline: panel button jumps to CONFIG and opens the wizard; copy and docs | 97356c0 |
| T5 | S1-S3 | inline: host suite, compile, serial probe with `imufake`, board evidence | done |

## Log

L1 (verbatim): "Mmm la pelotita tiene que ser mas liviana, rebotar más, es una
pelotita rebotona... Y respecto a la calibracion, metamos el wizard de
calibracion en la config y lo saquemos del juego de meteorito dale?"

L2: Decisions. The wizard state machine became a pure header
(`steer_wizard.h`) so the flow is host-tested; it reuses `calibration_gate.h` and
`steer_solve.h`. CONFIG starts it directly when the CALIBRAR row is pressed (the
SI/NO prompt was only useful when a gesture could be triggered by accident; a
menu row is explicit). The panel's reset endpoint now navigates to CONFIG and
opens the wizard instead of dropping the owner into the game. CONFIG holds
rotation and the sleep timer while the wizard is open, because the wizard is
literally asking the owner to tilt the board. The ball tuning from the same
message is recorded in `buddy-idle-ball.md` L7.

L3: Evidence. `steer_wizard_test` RED before `steer_wizard.h`, GREEN after: centre
average and confirm, rejection without a tilt or while tilted, the strongest tilt
is the capture, four captures solve to x/lateral, y/fore-aft, z/gravity with both
signs, a leaning neutral gives that neutral as the baseline, and a gesture set
with no left/right pair reports incomplete. `python3 tools/run_tests.py`: 24
passed. Firmware 1,429,246 B (45% flash) / 55,188 B (16% RAM), flashed
`Hash of data verified`.

Board probe (`tools/probes/calibration_config.txt`, `imufake`, COM5):
`POST /api/steer/reset` navigated to CONFIG (`screen:6`) and opened the wizard
("config: calibracion, deja el equipo en el centro"). The five `press 300` taps
logged `0/4`, `1/4`, `2/4`, `3/4` and `calibrado lat=0 up=2 sign=1 signY=1`.
`/api/status` before: `sign:-1, latAxis:1, upAxis:2, calibrated:true`; after:
`sign:1, latAxis:0, upAxis:2, calibrated:true` — the synthetic gestures are
exactly what the wizard stored. The final `dump 4` shows CONFIG with the row
written. The ball tuning from the same message rode in the same build; its toy
path was already board-verified in `buddy-idle-ball.md` L5 and its new bounce
numbers are pinned by `buddy_ball_test`.

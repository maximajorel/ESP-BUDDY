# Meteoritos: 2D movement and a calibration prompt

Reworks the dodge game on `esp32_gadget`: rename the screen, use both in-plane
tilt axes for free movement, and replace the "tap to calibrate" surprise with an
explicit prompt opened by a double tap. Builds on `odd/tasks/esp32-gadget.md`
(S3, S5) and `odd/tasks/screen-chrome-and-names.md` (S10).

## Specs

S11. Ship it as Meteoritos — verbatim: "Che, el juego de METORITOS, se llama
esquivar pero quiero que por defecto se llame meteoritos". The shipped default
name becomes `METEORITOS` in the firmware, in the panel's "original names" and
in the docs; an owner rename still wins.

S12. Free 2D movement — verbatim: "ahora solo se mueve izquierda derecha, pero
tenemos todo un giroscopio! podemos hacer para arriba y abajo y que sea libre el
movimiento por el espacio." The ship moves on both axes of the playfield from the
two in-plane sensor axes. Firing still goes up, meteors still fall, collisions
use the ship's real row.

S13. Explicit calibration — verbatim: "ademas que con dos ap rapidos en el boton
de boot se abra la calibracion, que diga iniciar calibracion y moverse entre si y
no apretando rapido y manteniendo para confirmar y de ahi calibrar". In the ready
or game-over state, two quick taps open a prompt titled "INICIAR CALIBRACION"
with SÍ / NO; a tap moves the choice, a 1 s hold confirms it, and SÍ starts the
gesture. In that prompt the hold belongs to the prompt, not to the screen menu.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S13 | inline: pure tap-window module + host test (RED→GREEN) | — |
| T2 | S11 | inline: default name METEORITOS in firmware/panel/docs | — |
| T3 | S12 | inline: baselineY/signY, fused fore/aft tilt, ship row, bullets, collisions | — |
| T4 | S13 | inline: GAME_CONFIRM prompt, double-tap entry, hold hook in screens | — |
| T5 | S12,S13 | inline: panel invert-vertical switch, API, mock, console `steer` signY | — |
| T6 | all | inline: host tests, pack assets, compile, flash COM5, hardware verification | — |

Commits are empty for this topic: the whole project landed in the initial
import commit, so there was no earlier tree to slice against. Feature work from
here on commits per unit.

## Log

L1 (verbatim): "Che, el juego de METORITOS, se llama esquivar pero quiero que por
defecto se llame meteoritos, ahora solo se mueve izquierda derecha, pero tenemos
todo un giroscopio! podemos hacer para arriba y abajo y que sea libre el
movimiento por el espacio. ademas que con dos ap rapidos en el boton de boot se
abra la calibracion, que diga iniciar calibracion y moverse entre si y no
apretando rapido y manteniendo para confirmar y de ahi calibrar"

L2: Decisions taken while implementing. The two in-plane axes already fall out of
the existing calibration: `upAxis` is gravity, `latAxis` is the axis with the
biggest swing, and the remaining axis is fore/aft, reused as the vertical accel
axis with its own baseline. Vertical direction is not something a left/right
gesture can teach, so it gets its own stored sign (`signY`, default forward) and
its own panel switch "Invertir arriba/abajo". Double tap only opens the prompt in
READY and GAME_OVER: during play a tap must fire, and a dialog mid-run would throw
the run away. A single tap in those states is resolved after the double-tap
window so the two gestures cannot collide. A console `doubletap` hook exists
because the probe cannot send two taps 120 ms apart; it calls the same short-press
path the button uses.

L3: A second, smaller request arrived in the same session (S14): "Ah tambien
saquemos en el boot los colores, y que en la web ui quede un boton de ultima para
lanzar esa accion que me sirve como verificacion del trema de los colores". The
boot flashed red/green/blue/white via `bootColorTest()` on every power-up before
the splash. The test moved to `displayColorTest()` in `display.cpp`, the boot call
and the static function were deleted, and the action is now reachable on demand
from the console (`colors`) and from a new last button in the panel's
Mantenimiento card via `POST /api/panel/colors`. Docs, mock API and the HTTP route
test were updated. Small path: no extra feature document.

L4: Hardware evidence (2026-11-16). Host tests: `tap_window_test` PASSED (13
checks), `screen_name_test` and `screen_order_test` still PASSED; assets repacked
and `verify_assets.py` PASSED. Firmware compiled (1318002 bytes, 41% flash; 53420
bytes, 16% RAM) and flashed to COM5, `Hash of data verified`. Serial console with
the deterministic `tap`/`longpress` hooks: READY -> `tap` -> PLAYING (single tap
resolves after the 400 ms window); `doubletap` -> CONFIRM; `longpress` ->
"calibracion: confirmada" -> CALIBRATING; `tap` -> PLAYING. 2D movement with the
owner's axes (lat=1, up=2, sign=-1, signY=1) and fake IMU: fore/aft accel -0.12
moved the ship from row 18 to 9.50, +0.12 brought it back to 11.65; lateral +0.12
moved the column from 26.50 to 14.21. A first `steer` restore was needed after the
calibration test overwrote the stored axes; it was re-applied and the device now
reports lat=1/up=2/sign=-1/signY=1 again. Boot no longer prints `panel test:`
lines; `colors` prints the four and `color test done` on demand; `POST
/api/panel/colors` answers ok in 1.8 s and a GET on it is 405. Names on the device:
METEORITOS, order `[0,4,2,1,3]`, game record 1219 unchanged.

L5 (verbatim): "el eje vertical esta invertido". The owner tested the flashed build:
with the shipped default (`signY = 1`) tilting the top edge forward moved the ship
the wrong way. The default mapping was negated in `readTilt` (`signY = -signY`) so
an untouched device is correct on this board/posture; the stored signY stays 1 and
the panel switch keeps working as an exception. Verified on hardware with fake
IMU: after the flip, +0.12 on the fore/aft axis moves the ship up and -0.12 moves
it down (exactly the opposite of L4).

L6 (verbatim): "mmm no funciona tan bien la calibracion inclino izquierda y derecha
constantemente pero medio que falla ... de ultima que la calibracion sea como mover
la nave sobre el espacio o una pelotita o algo asi, entonces que los pasos sea
moverla a cada esquina y de ahi que interprete, o mucho bardo?". S15: the single
gesture cannot tell the two in-plane axes apart reliably once both axes move the
ship, and it never learns either sign. Replaced by a guided five-step flow: hold
the device still (neutral), then tilt toward you, away, left and right, one at a
time, returning near neutral between steps. `steer_solve.h` (pure, host-tested)
derives both axes, both signs and both baselines from the four captured vectors;
no prior sign is trusted. Recalibrating overwrites the stored signs, so the two
panel invert switches are exceptions applied after a calibration, not inputs to
it. Not much bardo: it is four one-second tilts with a return to center, and it
is the only way to learn "up" and "right" instead of guessing them.

L7: Implementation and checks (2026-11-16). `steer_solve.h` is pure and
host-tested by `tools/steer_solve_test.cpp`: postures with gravity on x/y/z, every
sensor axis inverted, offset neutrals, cross-talk on the other axis, a gesture too
small to measure and null captures. `PASSED (0 failures)`. The firmware compiles
and was flashed to COM5 (`Hash of data verified`, 1319162 bytes, 41% flash).
On-hardware verification of the five-step flow is blocked: while the calibration
test was running, the serial tool's DTR/RTS reset sequence parked the board in the
USB download loader (`boot:0x0 (DOWNLOAD(USB/UART0))`), and every host-side reset
kept sampling IO0 low, so the firmware never started. That is a test-harness/board
state issue, not a code path: the same build booted and ran commands before the
reset. `gadget_probe.py` was corrected to stop pulsing RTS (opening the port is
enough on this revision), but the board needs a physical RESET with BOOT released
before the flow can be exercised.

L8 (verbatim): "Bien! funciona bien... Hagamos mas dificil el juego, mas rapido,
que aparezcan cada tanto naves enemigas que disparan y solo se destruyen cuando le
disparas". S16 (harder/faster) and S17 (enemy ships). Difficulty becomes one pure
function of elapsed time (`game_difficulty.h`) so the tuning is host-tested and
the game cannot drift from what the test says: a level every 10 s, meteors from
750 ms/4.0 rows/s down to a 160 ms floor and up to 18 rows/s, fighters from 12 s
apart (first at 6 s) down to 4.5 s, firing every 2.2 s down to 0.8 s, their shots
11-18 rows/s. Enemy ships drift sideways and fire downward; only a player bullet
destroys them (one hit, +150 points), while their shots and their body end the
run. Up to 3 alive, 8 enemy shots. The ship gets a small speed bump (28 cols/s,
20 rows/s) to stay fair against faster meteors.

L9: Evidence for S16/S17 (2026-11-16). Host tests: `game_difficulty_test`
PASSED (level boundaries, every ramp monotonic to its floor/cap, no overflow at
0xFFFFFFFF) and `game_hits_test` PASSED (each hit box from both sides, the wider
fighter box vs the meteor box). Firmware flashed (`Hash of data verified`).
On-device with the fake IMU: level 2 at ~10 s; meteors denser; after ~6 s a
fighter reports `enemies 1` and `enemyShots 1`, and its column changes between
readings (44.62 -> 21.73 ...) while the player's window stays over `x=38.07`.
The aimed-kill check could not be completed: each time the ship was parked under
the predicted fighter, a meteor ended the run before the bullet resolved (or the
fighter bounced at the edge), so the "only a shot destroys it" integration is
covered by the host-tested hit boxes plus the wired calls, not by an on-device
kill. All test hooks used (`setship`, `closemenu`, `tap`, `imufake`) are
console-only. Device left with the default screen timeout (120 s, clock+network).

L10 (verbatim): "Podemos hacer que las naves enemigas tengan una barrita de vida? a
medida que van pasando los niveles hace falta pegarle con las balas mas veces para
destruirlas... Tambien a medida que pasan los niveles disparan con mas frecuencia
y sus balas son mas rapidas". S19: fighters get `hp`/`maxHp`; the difficulty
curve gains `enemyHp = 1 + lv/2`, capped at 6 (one extra shot every two levels),
and the fire/bullet ramps were steepened to 2200->700 ms and 11->20 rows/s. A
2 px, three-cell health bar is drawn above a fighter only when it needs more than
one shot, and a non-lethal hit flashes the bead so the drop is visible. The
console prints `hp=x/y` per fighter, with `gameage`/`spawnenemy` test hooks to
check level-scaled HP without surviving to that level. Host test extended for the
HP ramp; hit boxes unchanged.

L11: Evidence for S19 (2026-11-16). `game_difficulty_test` PASSED with the HP
assertions (level 1-2: one shot, 3: two, 5: three, 7: four, 9: five, 11: six, cap
6) and the steeper fire/shot ramps (2200->700 ms, 11->20 rows/s). Firmware
compiled (1326506 bytes, 42%) and flashed (`Hash of data verified`). On hardware,
with the fake IMU: a level-1 fighter reported `hp=1/1`; `gameage 30` (level 4) +
`spawnenemy` gave `hp=2/2` and drift 5.50; `gameage 60` (level 7) gave `hp=4/4`
and drift 7.00, with all three fighters alive and firing (`enemyShots 3`). The
`dump` frame shows the 2 px health bars above the fighters (`++++++` segments
above the glyph row). Test hooks: `gameage <s>` and `spawnenemy`, console-only.
Device left on the clock with the fake IMU released.

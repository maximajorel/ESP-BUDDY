# Sofía: a happy face without carets and an idle bouncing ball

Owner's fourth pass on the buddy companion. The tap happy face (`^ ^`) is
replaced by the squircle eyes plus a small curve under each eye, and a double tap
on the SOFIA screen spawns a red ball: shaking the board bounces it off the glass
edges, the eyes follow it, and another double tap removes it. The ball belongs to
the idle state only; while Sofía is thinking or running tools it never shows and
shaking the board must not make her dizzy.

## Specs

S1. Happy face keeps the squircle, no brows — verbatim: "No quiero que tenga
cejas los ojos... te dije que quede como parpados inferiores o posteriores para
las expresiones" (supersedes the earlier "con unas curvita abajo de los ojos" of
L1). The happy/poke face draws the same rounded-square eyes as every other state
(owner colour and glow) and lifts the **lower** eyelid into a squint. No eyebrow
ever: no stroke above the eye and no separate decorative curve below it. The old
`^ ^` caret and its halo are gone.

S2. Double-tap red ball — verbatim: "con dos toques, salga una pelotita roja que
si sacudo la pantalla rebote por los bordes y los ojos la sigan, si toco dos
veces de nuevo desaparece la pelotita, si no siempre esta ahi." Two quick taps
(inside the shared 400 ms window) put a red ball on the glass; it moves when the
board is shaken, bounces off the four screen edges losing some speed, and the
eyes follow it. Two quick taps again remove it. Until then it stays (it survives
bridging away from idle, but is not drawn while Sofía is busy).

S3. Idle only, no dizziness — verbatim: "cuando sofia esta pensando osea estoy
usando el agente y eso, no se distrae con la pelotita, tampoco se marea ni nada,
es mas que nada para un estado idle digamos..." The ball is simulated and drawn
only while the shown state is idle/bored, the face is awake and the bridge phase
is idle. On any other state/phase it is hidden and frozen. While the ball is on
the glass, the IMU reaction machine is held calm (no dizzy, no anger, no startle,
no tremble), and the idle bug distraction does not fire; the ball is the thing
the eyes follow.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S2 | inline: pure `buddy_ball.h` (spawn, shake step, edge bounce, gaze, toggle) + `buddy_ball_test` | 5ad8459 |
| T2 | S3 | inline: `buddyImuCalm` in `buddy_imu.h` + test case | 5ad8459 |
| T3 | S1 | inline: `screen_buddy.cpp` happy branch = squircle + raised lower lid | f40c763 |
| T4 | S2,S3 | inline: tap window in `screen_buddy`, ball physics/draw/gaze, idle gating, console `ball` | 5ad8459 |
| T5 | S1-S3 | inline: host suite, firmware compile, hardware evidence | pending board |

## Log

L1 (verbatim): "Me di cuenta que cuando toco el boton sale ^^ en sofia, pero
mejor quiero que la cara de alegria sea de otra forma, conservando el squircle y
con unas curvita abajo de los ojos para darle mas expresividad o cosas por el
estilo. Y que con dos toques, salga una pelotita roja que si sacudo la pantalla
rebote por los bordes y los ojos la sigan, si toco dos veces de nuevo desaparece
la pelotita, si no siempre esta ahi. Lo que si, cuando sofia esta pensando osea
estoy usando el agente y eso, no se distrae con la pelotita, tampoco se marea ni
nada, es mas que nada para un estado idle digamos..."

L2: Decisions. A single tap is still the boop, so the tap window
(`tap_window.h`, already used by the config and game screens) delays it by up to
400 ms; a double tap toggles the ball instead of booping. The ball is not
gravity-bound while the board is held still: the step uses the lateral
accelerometer minus its own slow tilt baseline, so a steady tilt does not drag
the ball into a corner and only a real shake injects velocity; friction and edge
restitution (0.78) settle it. The ball is a separate pure header
(`buddy_ball.h`) so the physics and the gaze mapping are host-tested without a
board. The three-dot HUD keeps drawing above it on the top-right; the ball stays
inside a 320x172 field inset by its radius.

L3: Implementation and host evidence. One work-unit commit (`5ad8459`, 544
authored lines) instead of two ~400-line slices: the happy branch and the ball
share the `drawFace` signature and the same `screen_buddy.cpp`, so any split
leaves a commit that does not compile or does not show the feature (same reason
recorded in `buddy-smooth-expressions.md` L5). `buddy_ball_test` is RED before
`buddy_ball.h` exists and GREEN after; `buddy_imu_test` gained the calm case
(`buddyImuCalm` drops the dizzy face and the burst counter, so the next shake is
dizzy again instead of angry). `python3 tools/run_tests.py`: 23 passed, 0 failed.
Firmware compiled with the INSTALL FQBN: 1,429,838 B (45% flash) and 55,188 B
(16% RAM), no new warnings (only the pre-existing `buddyLinkWireName` /
`buddyAskError` switch warnings in those files).

L4 (verbatim): "No quiero que tenga cejas los ojos... te dije que quede como
parpados inferiores o posteriores para las expresiones, anotalo en memoria y crea
una skill asi quede como regla para futuros usos"

The under-eye curve is removed: it read as a brow, which is exactly what the
owner rejected. The happy branch now draws the squircle and lifts the lower lid
(`buddyLidBandBottom` + `ditherLidRow`, bottom 0.3). The rule is saved in Engram
(`buddy/face-expression-rule`) and as the project skill
`.agents/skills/sofia-buddy-face/SKILL.md`: no eyebrows, expressions only through
upper/lower eyelids and the existing pose channels. First board probe (before
this change) also found a real wiring bug: a double tap was swallowed because
`buddyShortPress` discarded the `TAP_DOUBLE` that `tapWindowPress` returns;
`tapWindowPoll` only reports singles. Fixed and re-flashed.

Pending: on-board evidence. `tools/probes/buddy_ball.txt` is written for it
(single tap → happy dump; `ball on` + `imufake 2.6 0 1` → moved ball in
`buddydraw`; `imufake 0 0 1` → coasting; `doubletap` twice → toggle off/on; final
`dump` with no red ball). Flashing is still the owner's explicit call.

L5: Board evidence (2026-11-17, COM5, flashed with explicit owner
authorization). Firmware 1,430,126 B (45% flash) / 55,188 B (16% RAM) and
`Hash of data verified`. `tools/probes/buddy_ball.txt` passed end to end:

- Happy face: `buddyhappy 10` + `dump 4` shows a full-width squircle whose bottom
  is a flat raised lower lid, no caret, no under-eye curve, no brow.
- Ball: `ball on` puts it at the centre; `imufake 2.6 0 1` moves it and
  `buddydraw` reports `x=188.4 y=60.1 vx=2.3` then `x=263.7 y=59.5 vx=32.4`
  (it travels and then bounces, ending near the edges); `doubletap` makes
  `ball on=0 active=0` and a second `doubletap` returns `on=1 active=1`; the
  `dump 3` during the bounce shows the ball's bright highlight (`#`) and body
  (`.`) drawn over the eye area.
- Two real defects were found and fixed by that run: the double tap was
  swallowed because `buddyShortPress` dropped the `TAP_DOUBLE` returned by
  `tapWindowPress` (the poll only reports singles), and a random idle blink
  could turn the happy eye into a 4 px bar because `baseRy` was computed before
  the happy branch. Both re-flashed and re-verified.
L6 (verbatim): "che toco dos veses y no aparece la pelotita ehh?????????????????????"
(with the board on SOFIA, bridge `state:bored`, `phase:idle`, `ball on=0`)

Reproduced and fixed. The console `doubletap` worked, but the real button path
did not: `buddyShortPress` only armed the window and the boop/toggle waited for
`buddyTick` to poll it, so two physical releases more than 400 ms apart (a
normal double click, press duration included) never paired and the ball never
appeared. Now the first tap boops immediately and the second tap inside a
600 ms release-to-release window (`BUDDY_TAP_DOUBLE_MS`) toggles the ball; there
is no polling and no dead delay. The serial probe cannot send two releases 120 ms
apart (its per-command drain takes ~1.7 s), which is why this only showed up with
a finger; `doubletap` and `press` both still exercise the path. Re-flashed and
verified with `doubletap` (`ball on=1`) and a single `press` (boop).

L7 (verbatim): "Mmm la pelotita tiene que ser mas liviana, rebotar más, es una
pelotita rebotona..."

The ball is lighter and bouncier: restitution 0.78 -> 0.92, friction 1.6 -> 0.4
per second, shake gain 220 -> 260 px/s^2 per g and top speed 430 -> 520 px/s. The
host test now also pins that it bounces at three or more walls inside six seconds
and is still moving at the end. (The calibration move from the same message has
its own document: `steer-calibration-config.md`.) Flashed together with the
calibration move; the bounce constants are pinned by `buddy_ball_test` (three or
more wall hits inside six seconds, still moving at the end) and the toy path was
already verified on the glass in L5.

L8 (verbatim): "Mmm la pelotita sigue lenta y no se si esta bien la calibracion
eh? porque si lo pongo vertical para que la pelota vaya al lado izquierdo de la
pantalla, no va para ahi, queda flotando en el ojo derecho, la gravedad esta mal
digamos, la pelotita siempre cae a un borde segun la inclinacion, es como la
gravedad de la tierra. pero OJO, en el juego de meteorito, usando la calibracion
desde config funciona bien, es solo en SOFIA donde trollea"

Root cause: the ball had no gravity at all. It used the dynamic lateral
acceleration (raw minus its own tilt baseline), which by design cancelled a
steady tilt; so holding the board vertical produced no force and the toy floated.
It also ignored the steering calibration, which is why the game was right and
SOFIA wrong.

The ball now maps the raw accelerometer through the same `SteerCalibration` the
game uses (`latAxis`, `sign`, the third `fwdAxis`, `-signY`, the wizard baselines
and `rotationInPlaneSign()`), giving screen-space gravity: positive x = right,
positive y = down, exactly the direction the ship moves under the same tilt. A
~0.5 s filter of those channels is the steady tilt that pulls the ball; the
difference above the filter is the shake that kicks it. Without a calibration it
falls back to the raw axes so the toy still works. Speed: gravity 520 px/s^2 per
g, shake 520, top speed 800, friction 0.25/s; a hit slower than 60 px/s is
absorbed so gravity into a wall does not jitter.

Board probe (`tools/probes/buddy_ball_gravity.txt`, `imufake`): at level the ball
coasts; with `a[0]=+0.5` it ends against the left wall (`x=32`, `vx=4.3`) and with
`a[0]=-0.5` it crosses the glass to the right (`x=93.7`, `vx=25.5`, `vy=158`) —
the sign follows the calibration and the ball is fast now. `buddy_ball_test`
gained gravity-on-each-axis and rest-against-a-wall cases; suite 24 passed.

- The expression rule from L4 is enforced in `.agents/skills/sofia-buddy-face/
  SKILL.md` and Engram (`buddy/face-expression-rule`). The new `buddyhappy`
  console pin is documented in the console help.

L9 (hardware, `f3945c8` pulleado de la otra máquina): el head turn y la familia de
eventos idle se verificaron en la placa, no solo en el suite de host. Build
1.505.694 B (47%), upload a COM5 escribió 1.505.840 B y respondió `Hash of data
verified`. `tools/probes/buddy_face_turn.txt` maneja `buddydraw`, que imprime la
geometría de la última cara dibujada:

```
buddyface waiting 8000  -> eye0 rx=53 eye1 rx=46 deg=2.9   asking: turn + tilt
buddyface waiting 20000 -> eye0 rx=48 eye1 rx=51 deg=1.7   fidgeting: turn flips
buddyface error 9000    -> eye0 rx=50 eye1 rx=46 deg=2.6   apology: turn = -0.22
buddyface idle 10       -> eye0 rx=48 eye1 rx=48 deg=0.0   control: symmetric
bug 6                   -> eye0 rx=48 eye1 rx=48 deg=0.0   bug is gaze, not size
```

El control de `idle` es lo que hace la prueba concluyente: sin evento el par sale
exactamente simétrico (48/48, deg 0.0), así que la diferencia de ancho en las
bandas de waiting y apology es el canal `turn` y nada más. En el pin de error,
`lid=0.42`, `ry=36` y `hot=0.30` coinciden con la banda de disculpa de `buddyPose`.

No verificado en hardware: las otras seis clases de idle (wink, peek, yawn,
shiver, look-up, doze). No tienen pin de consola y las épocas son de 3 minutos, así
que descansan solo en `buddy_anim_test`. Un pin `buddyidle <kind>` las haría
verificables.

De paso, el run probó en placa el commit del AP: `ap off  (auto, sta conectado
30s): panel en http://192.168.100.75` — la regla del duty se aplicó sola y el panel
siguió vivo en la IP del router.

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

- The expression rule from L4 is enforced in `.agents/skills/sofia-buddy-face/
  SKILL.md` and Engram (`buddy/face-expression-rule`). The new `buddyhappy`
  console pin is documented in the console help.

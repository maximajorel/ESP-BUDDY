# Sofía: a happy face without carets and an idle bouncing ball

Owner's fourth pass on the buddy companion. The tap happy face (`^ ^`) is
replaced by the squircle eyes plus a small curve under each eye, and a double tap
on the SOFIA screen spawns a red ball: shaking the board bounces it off the glass
edges, the eyes follow it, and another double tap removes it. The ball belongs to
the idle state only; while Sofía is thinking or running tools it never shows and
shaking the board must not make her dizzy.

## Specs

S1. Happy face keeps the squircle — verbatim: "mejor quiero que la cara de
alegria sea de otra forma, conservando el squircle y con unas curvita abajo de
los ojos para darle mas expresividad o cosas por el estilo." The happy/poke face
draws the same rounded-square eyes as every other state (owner colour and glow)
and adds a thin upward curve under each eye. The old `^ ^` caret and its halo are
gone.

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
| T3 | S1 | inline: `screen_buddy.cpp` happy branch draws squircle + under-eye curve | 5ad8459 |
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

Pending: on-board evidence. `tools/probes/buddy_ball.txt` is written for it
(single tap → happy dump; `ball on` + `imufake 2.6 0 1` → moved ball in
`buddydraw`; `imufake 0 0 1` → coasting; `doubletap` twice → toggle off/on; final
`dump` with no red ball). Flashing is still the owner's explicit call.

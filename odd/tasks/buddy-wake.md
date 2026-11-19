# Sofía: the boot wake-up on the glass

Every power-on the 320x172 glass now wakes as Sofía instead of showing the
ESP32 text splash: the face's energy rises out of black, the eye slit grows,
she blinks, gives a small shake and puts on the first happy face. The visual
spec is `design/sofia-wake/01-amanecer.html` (chosen artboard of
`design/sofia-wake`); the tokens are in `design/system.md`.

## Specs

S1. Power-on wake, not a state — verbatim: "Con gentle design, podes crear una
animacion de inicio para sofia? osea para la primera vez que se prende sofi,
mostrar como se despierta o algo asi?" The wake plays once at boot, before any
screen gets a say, lasts `BUDDY_WAKE_MS` = 5000 ms and ends on the normal idle
face (open 1, glow 1, grow 1, bottom 0, tremble 0, bob 0, gaze 0).

S2. Shake and happy at the end — verbatim: "Este podria ser che, podemos hacer
como que haga un pequeño sacudon de ojos, y se ponga feliz?" Between 3420 and
3680 ms the eyes shake (`tremble`), and from 3800 to 4400 ms the face is happy:
the **lower** eyelid rises (`bottom` 0.32, then 0.30), `grow` 1.08 and a −3 px
hop. No brows, no mouth, no text, no assets: the happy face is the squint, as
S1 of `odd/tasks/buddy-idle-ball.md` already decided.

S3. The timeline of the artboard, in order:

| t (ms) | open | glow | grow | bob | gazeX | bottom | tremble | ease |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 0 | 0 | 0.25 | 0 | 0 | 0 | 0 | linear |
| 900 | 0.05 | 0.5 | 0.45 | 0 | 0 | 0 | 0 | out |
| 2200 | 1.07 | 1 | 1.02 | 0 | 0 | 0 | 0 | in-out |
| 2500 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | out |
| 2800 | 0.03 | 1 | 1 | 0 | 0 | 0 | 0 | in-out |
| 3100 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | out |
| 3300 | 1 | 1 | 1 | 1.2 | −2 | 0 | 0 | in-out |
| 3420 | 1 | 1 | 1 | 0 | 0 | 0 | 0.85 | out |
| 3560 | 1 | 1 | 1 | 0 | 2 | 0 | 0.6 | linear |
| 3680 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | out |
| 3800 | 1 | 1 | 1.08 | −3 | 0 | 0.32 | 0 | out |
| 4060 | 1 | 1 | 1.06 | 0.8 | 0 | 0.34 | 0 | in-out |
| 4380 | 1 | 1 | 1.04 | 0 | 0 | 0.30 | 0 | in-out |
| 4700 | 1 | 1 | 1 | 0 | 0 | 0.12 | 0 | in-out |
| 5000 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | out |

S4. Two new pose channels, preserving every existing expression — `BuddyPose`
gains `float open = 1.0f` (vertical aperture: 0 is the 4 px blink bar, 1 is the
full squircle) and `float glow = 1.0f` (the face's energy: scales the eye colour
and its three halos together; 0 is a black panel). At its default every channel
must draw exactly the frame it draws today, and no existing ladder, IMU
reaction, game mood or pinned face may change.

S5. Boot wiring and the way out — a board with a bridge URL and token plays the
wake in place of the text splash; an unconfigured board keeps the splash,
because that splash names the access point the owner has to join. A tap or a
hold during the wake skips it instead of acting on a screen. The console gets
`wake` to replay it (the panel has no MISO, so the probe is the only proof), and
`buddydraw` reports `open` and `glow` so the funnel is measurable without a
photograph.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S3,S4 | inline: `BuddyPose.open/glow` + pure `buddyEyeHalfH`/`buddyGlowPercent` in `buddy_face.h`, cases in `tools/buddy_face_test.cpp` | working tree (sin commit: la rama y el commit los decide el dueño) |
| T2 | S1,S3 | worker: pure `esp32_gadget/buddy_wake.h` ladder + `tools/buddy_wake_test.cpp` | working tree (sin commit: la rama y el commit los decide el dueño) |
| T3 | S4 | worker: `screen_buddy.cpp` `drawFace` honours `open` and `glow`; `s_draw` and `buddydraw` report them | working tree (sin commit: la rama y el commit los decide el dueño) |
| T4 | S1,S5 | worker: wake overlay in `screen_buddy`+`screens`, `.ino` boot branch, console `wake`, help line | working tree (sin commit: la rama y el commit los decide el dueño) |
| T5 | S1-S5 | inline: `tools/run_tests.py` 31 passed, firmware compile 47% flash / 17% RAM, probe `tools/probes/buddy_wake.txt`; flashing waits for the owner | working tree (sin commit: la rama y el commit los decide el dueño) |

## Log

L1. "Con gentle design, podes crear una animacion de inicio para sofia? osea
para la primera vez que se prende sofi, mostrar como se despierta o algo asi?"

L2. Design comment `kw9zk6va` on `design/sofia-wake/01-amanecer.html`: "Este
podria ser che, podemos hacer como que haga un pequeño sacudon de ojos, y se
ponga feliz?" Applied to the artboard: the timeline went from 3800 to 5000 ms,
with the shake at 3.4 s and the happy squint from 3.8 s.

L3. Deltas between artboard and firmware, deliberate:
- `grow` starts at 0.25, not 0.20: `drawFace` treats `grow <= 0.2` as 1.0 (the
  anger floor), so 0.20 would snap to full width on the frame it crosses the
  clamp. With `glow` at 0 the first 100 ms are black anyway.
- The shake keeps the firmware's fixed noise amplitudes (±1.6 px gazeX, ±1.0 px
  gazeY, ±2.2° rot at 25 Hz, `buddyTremble`); `tremble` only opens and closes
  that noise. Scaling it by the channel would silently soften the existing anger
  and dizziness shake, which nobody asked to change.
- The artboard draws ±4 px of shake because it previews a single pair; the glass
  shakes less. The order of events and the shape of every frame match.

L4. The continuity case in `tools/buddy_wake_test.cpp` caps the unitless
channels (open, glow, grow, bottom, tremble) at 0.35 per 16 ms and the two pixel
channels (bob, gazeX) at 1.0 px: the S3 table's 3 px hop eased out over 120 ms is
0.75 px per 16 ms by construction, so one limit for both units was arithmetically
impossible. Observed worst steps: 0.189 unitless, 0.747 px.

L5. `buddyEyeHalfH` floors the eye at 4 px, so a pose asking for less than 4 px
now draws the bar instead of a 1-3 px sliver. The only frame in the tree that does
that is the yawn wake (`pose.ry = 0`), where `rx` is also 0 and the renderer
returns before drawing, so no visible frame changes.

L7. Evidence (board, COM5): flashed `1507760` bytes with `Hash of data verified`
after a clean `compile -b <FQBN from INSTALL.md §3>`; `tools/probes/buddy_wake.txt`
run through `tools/gadget_probe.py` saw `wake: despertar de arranque en curso`, a
mid-funnel pose of `grow=0.93 open=0.91 glow=0.92` (three channels agreeing with
the ladder at t≈1.8 s, and the funnel is the only source of those values), a
dump at t≈3.9 s with a 76 px-tall eye closed by a full-width flat bottom row
(the raised lower lid, where the control frame tapers), and `open=1.00 glow=1.00`
with the normal rounded eye once the wake ended. The console answers in ~1.5 s
per command, so the first 900 ms of a cold boot cannot be sampled from the
console: that stretch is pinned on the host by `buddy_wake_test`, not on the
glass.

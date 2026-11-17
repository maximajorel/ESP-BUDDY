# Sofía: squircle eyes, expressions, gyro reactions and smoothing

Owner's third pass on the buddy companion: square eyes with rounded corners, more
expressions, gyroscope-driven dizzy/angry reactions, smoother motion, and an
eye colour the owner picks from the web panel and keeps across reboots.

## Specs

S1. Squircle eyes — verbatim: "los ojos lo hagamos cuadrados con los bordes
redondeados." Both eyes render as rounded squares (squircle), keeping every
existing expression primitive (top lid, bottom lid, per-eye squint, rotation,
happy arcs, blink bar, X for errors).

S2. Owner eye colour, persisted — verbatim: "me gustaria en la web ui poder
cambiar el color de los ojos si quiero y que quede persistente el cambio". The
web panel exposes a colour input, presets and a reset to the shipped cyan
`#00B4FF`; the value is stored in NVS on its own key and survives a reboot.
Emotional tints (dizzy/angry) blend on top temporarily and the owner's colour
returns. Only the eyes change: the LED bead stays the bridge's phase light.

S3. More expressions — verbatim: "Y analicemos la forma de hacer mas
expresiones". The state ladders gain secondary life: a slow breathing squash on
idle/bored/waiting, side glances while waiting, a bounce on done, and default
zero values for the new pose fields (`hot`, `rotL`, `rotR`, `bob`) so every
existing expression keeps its previous look.

S4. Gyroscope reactions — verbatim: "que reaccione al giroscopio, por ejemplo si
lo sacudo mucho que haga una animacion que se marea, o que se enoja." A sustained
hard shake (>2.5 g for 300 ms) makes it dizzy: the eyes orbit and the whole eye
line wobbles for 2.5 s. Shaking again while dizzy (or repeatedly inside a 6 s
window) makes it angry: inner corners drop, lids cut sharp, and the eyes glow
hot for 3.5 s. Every reaction expires on its own.

S5. Everything smoother — verbatim: "Aparte quiero que sea mas \"smooth\"
todo...". Continuous pose channels (gaze, size, lids, rotation, hot tint, bob)
are eased in time instead of snapping, and the face redraws at ~20 fps instead
of 10 fps. Boolean gestures (blink, happy arcs) still snap: an eyelid is fast.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S1,S2,S5 | inline: pure `buddy_face.h` (eye colour dim/hot, squircle predicate, pose filter) + host test | — |
| T2 | S1,S2,S5 | inline: `gfxFillRoundRectRot`, screen draws squircle with owner colour, 20 fps + filter | — |
| T3 | S3 | inline: `buddy_anim.h` new pose fields and secondary-motion ladders, test | — |
| T4 | S4 | inline: `buddy_imu.h` dizzy/angry state machine + eased pose, test | — |
| T5 | S2 | inline: NVS `bdEye` setting, `/api/status` eyeColor, `POST /api/settings` | — |
| T6 | S2 | inline: Sofía card colour control, app.js wiring, repack + verify assets | — |
| T7 | S1-S5 | inline: host tests, compile, flash, live verification (serial dump + API) | — |

## Log

L1 (verbatim): "Mejoremos el buddy companion osea SOFIA. los ojos lo hagamos
cuadrados con los bordes redondeados. Y analicemos la forma de hacer mas
expresiones y que reaccione al giroscopio, por ejemplo si lo sacudo mucho que
haga una animacion que se marea, o que se enoja. Aparte quiero que sea mas
\"smooth\" todo... Por otro lado me gustaria en la web ui poder cambiar el color
de los ojos si quiero y que quede persistente el cambio"

L2: Implementation. `buddy_face.h` (new, pure) owns the owner colour and its
derivations (`buddyEyeDim`, `buddyEyeHot`), the rounded-square predicate
(`buddySquircleInside`, corner radius 0.38 of the short half-axis), the minimum
eye gap (`buddyEyeGapPush`) and the easing filter (`buddyPoseFilterStep`, tau
100 ms, first frame adopts the target, booleans snap). `gfxFillSquircleRot`
renders the shape as one span per row (convex under rotation) and
`screen_buddy` clamps the pair apart with `BUDDY_EYE_GAP_MIN` (10 px) so a wide
or tilted pair never merges. The pose grew `rotL`, `rotR`, `hot` and `bob`;
surprise is now taller than wide (50x60 instead of 54x58) because the owner saw
the widened pair touching. The face redraws every 50 ms (20 fps) and every
continuous channel is eased.

L3: Gyroscope moods. `buddy_imu.h` counts shake bursts inside a 6 s window: the
first 300 ms burst above 2.5 g is dizzy (orbit + wobble + 0.15 hot for 2.5 s);
shaking again while dizzy, or a second burst in the window, is anger for 3.5 s
(inner corners drop via `rotL=+16`/`rotR=-16`, lid 0.34, hot 0.85, contained
tremble). Anger replaces the dizzy wobble and both expire on their own.

L4: Persistence/API. `settingsGet/SetBuddyEyeColor` on its own NVS key `bdEye`
(default `#00B4FF`); `/api/status` publishes `buddy.eyeColor`;
`POST /api/settings {"buddyEyeColor":"#RRGGBB"}` sets it and an empty string
restores the default; an invalid value is rejected with 400 and leaves NVS
untouched. The Sofía card gained the colour input, five presets and a reset
button; the panel posts the same field.

L5: Delivery. The firmware landed as one work-unit commit (744 authored lines)
instead of two ~400-line slices: the new pose channels (`rotL`, `rotR`, `hot`,
`bob`) are consumed by the renderer, the smooth filter and the IMU overlay at
once, so any split leaves a commit that does not compile or does not show the
feature. The panel (HTML/JS, generated `web_assets.h`, mock and INSTALL row) is
the second work unit.

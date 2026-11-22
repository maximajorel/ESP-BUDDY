---
name: sofia-buddy-face
description: "Trigger: Sofía buddy face, SOFIA screen, buddy eyes, expresiones de la cara, párpados, cejas, pelotita roja, idle toy. Rules and workflow for changing the procedural face on the Waveshare ESP32-S3 gadget."
license: Apache-2.0
metadata:
  author: gentleman-programming
  version: "1.0"
---

## Activation Contract

Activate when the task touches Sofía's face: `esp32_gadget/buddy_face.h`,
`buddy_anim.h`, `buddy_imu.h`, `buddy_ball.h`, or the `drawFace` renderer in
`screen_buddy.cpp`, including any request about expressions, eyes, lids, colour,
gyro reactions or the idle ball. Read this before choosing an expression shape.

## Hard Rules

- **No eyebrows. Ever.** The face has no brows, no accent stroke above the eye
  and no separate decorative curve below it. The owner asked for this twice.
- **Expressions come only from eyelids and the existing channels.** Upper lid
  (`lid`, `lidL`, `lidR`), lower lid (`bottom`), rotation (`rot`, `rotL`,
  `rotR`), gaze, hot tint, `grow`, blink, closed and surprise. A happy face is a
  raised **lower** eyelid on the squircle; it is not a caret, a smile line, an
  under-eye curve or a brow.
- **Keep the squircle.** Solid eyes use `gfxFillSquircleRotAA` with the owner's
  colour and the two-layer glow. Only a blink/closed eye may become a bar, and
  the error face may put black X strokes inside the squircle.
- **One lid implementation.** Use `gfxLidCutRot`, `buddyLidBandBottom` and
  `buddyLidLineBottom`; never hand-draw a new lid line in a state branch.
- **Lid bands must cover the rotated extent plus `BUDDY_GLOW_PAD`.** A hot or
  tinted sliver above or below the band is a bug, not a style.
- **No mouth, no text, no assets** on the glass. The face is eyes only; labels
  stay in the web panel. The one decoration allowed is the dance: procedural
  eighth notes (a dither-faded head, stem and flag, never an image) in the two
  side fields `x 18-46` and `x 274-302`, the only columns the eye extents and
  their glow cannot reach. A note may never overlap an eye.
- **The idle ball belongs to idle.** It is drawn only when the shown state is
  idle/bored, the face is awake and the bridge phase is idle. While the ball is
  toggled on, the IMU shake channel is held calm (`buddyImuCalm`) so bouncing it
  never reads as dizziness or anger. Its gravity must go through the same
  `SteerCalibration` the meteor game uses (`latAxis`, `sign`, the third axis,
  `-signY`, mirror); never use the raw tilt for steady gravity, because the
  dynamic path cancels it and the toy floats. A double tap toggles it: `buddyShortPress`
  boops on the first press and consumes the `TAP_DOUBLE` that `tapWindowPress`
  returns on the second. Never wait for `tapWindowPoll` before booping: the
  delay feels dead and a normal human double click then misses the window. The
  buddy window is 600 ms release-to-release because the press duration is part
  of the gap.
- **Never flash without explicit owner authorization in the session.** Compile,
  then ask; after flashing, prove with the serial `dump` (no MISO on this panel).

## Where Expressions Live

| Layer | File | Responsibility |
| --- | --- | --- |
| Pose vocabulary | `buddy_anim.h` | `BuddyPose` fields and the state/age ladders (`buddyPose`). |
| Geometry and easing | `buddy_face.h` | Squircle predicate, lid bands, extents, glow shades, `BuddyPoseFilter`. |
| Physical reactions | `buddy_imu.h` | Shake → dizzy/anger, tilt, sleeping; `buddyImuCalm` for the ball. |
| Music reaction | `buddy_dance.h` | Tempo per track (hashed from `trackId`, 84-136), beat and bar from `progressMs`, the dance pose and the note field. |
| Idle toy physics | `buddy_ball.h` | Spawn, shake step, edge bounce, gaze mapping. |
| Renderer | `screen_buddy.cpp` | `drawEyes`: squircle + glow + lid bands, shared by the full face and the split frame (`drawSplitFrame`). `drawFace` adds the ball, the bug, the Zzz and the dance's notes (`drawMusicNotes`); `buddyTick` adds the HUD and decides when the dance owns the body. |

## Workflow

1. Change the pure layer first (`BuddyPose` field/ladder and/or `buddy_face.h`
   helper) so the expression is host-testable.
2. Add/extend a `tools/*_test.cpp` case for the new channel; run
   `python3 tools/run_tests.py` until green.
3. Wire the renderer using the shared lid helpers; keep the previous look at
   zero for every existing expression.
4. Compile with the FQBN in `esp32_gadget/INSTALL.md` §3.
5. Ask before flashing. Verify with `tools/gadget_probe.py` and a probe in
   `tools/probes/` (use `buddyface <state> [ageS]` for a pinned expression and
   `dump` to read pixels), then record the evidence in the ODD feature document.

## The dance

The music owns the body only when nothing else asks for the face: `idle` only,
never while working, waiting, in error, with the ball on, or while the calendar,
the study countdown or the web game own the glass. A playing track calls
`buddySleepStimulus`, so music wakes a sleeping face. The pose is *written*, not
added: the idle face's microsaccades and glances would read as a twitch under a
beat. A paused player is not a beat, so the dance stops and its notes die; the
progress position is what phases the beat, so resuming lands on the song again.

`dance <seconds>` on the console and the "Bailando" chip in the panel pin the
same reaction on a synthetic 112 BPM beat, so it can be dumped with `dump` and no
Spotify account.

## Pitfalls

- A lower lid drawn as a straight `gfxFillRect` must be followed by
  `ditherLidRow` so its edge is sub-pixel; otherwise it steps.
- The happy branch still has to push the eye pair apart and respect
  `BUDDY_EYE_GAP_MIN`; a wide squint must not merge the two glows.
- `buddySetBall` keeps the toggle across busy states but the ball is frozen and
  hidden; do not tick its physics while it is not visible.

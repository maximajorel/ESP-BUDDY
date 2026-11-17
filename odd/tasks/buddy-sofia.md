# Sofía buddy screen

A sixth screen turns the gadget into a body for Sofía (Hermes Agent): a procedural
face that reacts to what the agent is doing, driven by the local bridge Hermes
exposes on the LAN. No microphone, no speaker, no OpenCharm firmware: the face,
the button and the RGB carry the companion. Faces follow the OpenCharm language
(MIT) and the bridge contract verified live.

## Specs

S21. Buddy companion — verbatim: "En realidad lo que hay que hacer no es para ver
el estado, es construir un 'Buddy companion' hay muchos proyectos online que ya
lo hicieron!" and "por ahora solo tengo únicamente el esp, sin micrófonos ni esos
chiches! pero podemos extraer las caras! reacciones etc segun lo que hace
Hermes". A sixth screen (default name `SOFIA`) shows a procedural face on the
320x172 panel that reacts to Hermes' state: idle, thinking, tool, waiting, error,
done. Idle life: blink, look around, a poke reaction on tap, RGB mood, and the
existing display timeout. No audio.

S22. Local bridge — the bridge runs at `192.168.100.64:8787` (LXC, systemd):
`GET /state` → `{state, label, ts, run_id, active}` protected by a token header,
`POST /buddy/ask` → starts a run. The device polls every ~1.5 s, keeps the token
in NVS and never stores or sends the Hermes API key. Labels are ≤40 characters and
must never contain tool arguments.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S21 | inline: pure `buddy_state.h` (name→state, label clean) + host test | — |
| T2 | S21 | inline: NVS buddy URL/token, order migration 5→6 screens | — |
| T3 | S21 | inline: `screen_buddy.cpp` poller task, snapshot, procedural face, RGB | — |
| T4 | S22 | inline: status/settings/ask API, panel tile + Sofía card, mock, docs | — |
| T5 | S21,S22 | inline: compile, flash, configure the token, verify against the bridge | — |

Commits are empty for this topic: the initial import holds the project; this
feature lands as its own work-unit commit after verification.

## Log

L1 (verbatim): "En realidad lo que hay que hacer no es para ver el estado, es
construir un 'Buddy companion' hay muchos proyectos online que ya lo hicieron!"
Luego: "Ok igual por ahora solo tengo unicamente el esp, sin microfonos ni esos
chiches! pero podemos extraer la caras! reacciones etc segun lo que hace Hermes
digamos que opinas?"

L2 (bridge, from Hermes, verified live): GET /state → {state, label, ts, run_id,
active}; POST /buddy/ask {"prompt"}; header X-Buddy-Token or Bearer; states idle,
thinking, tool, waiting, error, done; token `pohX...` (32 chars) for the board,
API key only in /etc/hermes-buddy/env. No run listing exists, so the buddy sees
the aggregate (active_runs) plus its own runs in detail. Face language: OpenCharm
faces.json (MIT): glyph eyes (`o`, `^`, `x`, `−`), mouth (`v`, `w`), tilt, scale.

L3: Decisions. The face is procedural: big white oval eyes with black pupils,
lineless brows, a small mouth, and glyph eyes for the emotional states (`^ ^` for
done, `x x` for error) to stay close to OpenCharm without assets. RGB is the mood
lamp (blue idle, amber thinking/tool, violet waiting, red error, green done). The
poller runs in its own FreeRTOS task (same pattern as Spotify) with a 1.5 s period
and a 1.5 s timeout; the UI only redraws on a new snapshot or an animation frame
(~12 fps). The new screen defaults to not included in the display timeout; the
owner can include it from the panel. The order migration appends the new screen
when it finds the old five-entry order, so the owner's order and names survive.

L4: Evidence (2026-11-16). `buddy_state_test` PASSED (state names, unknown→
offline, label trim/clean/cut). Firmware compiled 1340994 bytes (42% flash) /
53764 bytes (16% RAM), flashed with `Hash of data verified`.

Live checks on the device (192.168.100.75) against the real bridge
(192.168.100.64:8787): the screen order migrated from [0,4,2,1,3] to
[0,4,2,1,3,5] and the names gained SOFIA; before the token the status reported
`unconfigured`; after `POST /api/settings {"buddyToken":...}` the poller
reported `configured=true, online=true, state=idle, label="aca estoy"` (the
accent folded for the 5x7 font); `POST /api/screen {"index":5}` selected the
face; `POST /api/prompt`-equivalent `POST /api/buddy/ask {"prompt":"Respondé
solo: listo"}` ran through the bridge and the status went idle → thinking
(label "pensando") → done ("listo"), back to active 0. The serial `dump 3` of the
face shows the brows, the two oval eyes with pupils, the mouth, the state title
and the footer on the 318px canvas.

The panel served by the device includes the new SOFIA tile and the Sofía card
(URL, token, live state, prompt box); the JS and the packed assets were verified
by `verify_assets.py` (PASSED), but the card itself could not be opened in the
browser at the end of the session because the harness ran out of process slots
(EAGAIN) — the API endpoints it uses are the ones verified above. The token
remains a LAN-only secret in NVS and is never returned by `/api/status`.

L5 (from Sofía, 2026-11-16): protocol refinements and an eyes-only redesign. The
bridge returns `done` for 8 s and then idle; it may add states without notice, so
an unknown state now falls back to **idle**, not offline (offline is only for a
bridge that did not answer). `GET /state` is polled every 1 s with a 2 s timeout;
`/buddy/ask` answers 202 and is asynchronous, so the firmware paints thinking
locally on success and lets the next poll correct it. The face lost every word:
the whole 320x172 canvas is two big circular **cyan** eyes (RoboEyes-style), with
lids, pupils, happy arcs, black X strokes for errors and dim eyes when the bridge
is unreachable; labels are no longer shown on the glass (the panel keeps the
text). The button never fires `/buddy/ask`: the tap is a boop and the hold opens
the picker; prompts only come from the panel's explicit form. `active == -1`
means the bridge could not read the aggregate; the eyes ignore it and the panel
shows the raw value.

L6: Cyan eyes-only build verified (2026-11-16). `buddy_state_test` PASSED with the
new unknown→idle fallback. Firmware compiled 1340054 bytes (42% flash) / 53764
bytes (16% RAM) and flashed with `Hash of data verified`. After the reboot the
token and URL were still in NVS: `buddy {configured:true, online:true,
state:idle, label:"aca estoy"}` and screen 5 `SOFIA`. The serial `dump 2` shows
the full canvas as two large cyan eye shapes with black pupils and **no text,
no footer, no divider** — exactly the eyes-only design. Poll is 1 s with a 2 s
timeout; `/buddy/ask` paints thinking locally on 202 and the next poll corrects.
Sofía's other points are respected: unknown states fall back to idle, `active`
-1 is ignored by the face (the panel shows the raw value), done is left to the
bridge's 8 s, no SSE on the device, the button never fires `/buddy/ask`
(tap = boop, hold = picker), and labels are no longer drawn on the glass.

L7 (verbatim): "che loco los ojos estan verdes no cyan JAJAJAJA le saquemos las
pupilas tambien". The first cyan (0x00E5FF, G=229/B=255) read as green on the
panel, so the colour moved to a bluer cyan (0x00A8FF, G=168/B=255) and the pupils
were removed: the eyes are solid, and the gaze now comes from drifting the whole
eye a few pixels (thinking pulls them up, the rest sways). Flashed
(1339942 bytes, 42%) and verified: screen 5 SOFIA, bridge online/idle, and the
`dump 2` shows two solid eye shapes with no interior gaps. If the hue still reads
green, the panel colour test (`colors`/Probar colores) tells whether the blue
channel is the issue rather than the face.

L8 (verbatim): "Mira mejoremos estos temas, te mando docs de parte de sofi... no
esta reaccionando siempre los ojitos... ojo cuidado ahi investiga bien... Te paso
el documento asi lo cumplas! C:\Users\Máximo Majorel\Downloads\buddy-tareas-firmware.md"

Read Sofía's firmware list (2026-11-16). Implemented the firmware side:
- `buddy_state.h`: `bored` is a known state now (wire name `bored`), unknown names
  still degrade to idle. Panel names it "Aburrida".
- `buddy_anim.h` (new, pure): duration ladders for `bored` (0-20 neutral, 20-60
  side gaze, 60-120 drooping lids, 120+ asleep with a slow bob), long `thinking`
  (0-3 up-left, 3-8 narrowed, 8-20 looks at the owner, 20+ long blinks but never
  asleep), tool surprise (200 ms wide, fired from adoption so the 300 ms hold
  cannot eat it), cross-eyed error with fast blinks, 1.2 s happy `done`, dim
  offline; idle microsaccades every ~3 s and double blinks every 2.6-6 s.
- `buddyShownState()` holds a state younger than 300 ms, so a one-poll flicker
  never flashes the face (Sofía's defence-in-depth request).
- `screen_buddy.cpp` parses the bridge's `since` (epoch float) and exposes
  `stateAgeMs`; the poll stays at 1 s / 2 s timeout. `stateAgeMs` is in
  `GET /api/status` for debugging.
- `displayColorTest()` now paints three bars (red | green | blue) for 5 s instead
  of four full-screen flashes: one glance shows a channel swap or a dead blue.
- Host tests: `buddy_state_test` and new `buddy_anim_test` both PASSED; build
  1,341,810 B (42%) / 53,788 B RAM; flashed "Hash of data verified".

Live evidence (after flash, screen 5 SOFIA): bridge `/state` = `bored`/`aburrida`
age 341 s; device status `state:bored label:aburrida stateAgeMs:341209`; the
`dump 2` shows only two thin horizontal bars at rows 43-45 — the asleep face of
the 120 s+ bored ladder, eyes only, no label, no chrome.

Open: (a) the colour verdict from the three bars (screen vs LED is already known:
it is the glass, the framebuffer reads 0x055F); (b) the Desktop monitor run is in
flight; (c) the IMU reactions of task 4 are not implemented yet; (d) task 4.9
(long press 3 s = sleep) conflicts with the existing hold = screen picker, so the
owner has to choose before the button map changes.

L9: Desktop monitor (22:46-22:48) proved the bridge now reports Desktop activity and
the device follows it: `BRIDGE=thinking "escuchándote"` with `DEVICE=thinking` in
the same second, then both back to idle. The old "only blinks" was the bridge's
previous API-only detection plus the face's subtle old expressions; the ladders
now change the face obviously.

Found a real bug with that log: the device clock was ~37 s ahead, so the bridge's
absolute `since` produced a 40 s age for a 3 s state. `buddyGetView()` now compares
the epoch age with the local observation and trusts the local one when they
disagree by more than 10 s; `/api/status` reports `stateAgeMs` correctly (2.8 s,
16.9 s in the probes).

IMU (task 4, high-impact subset) is wired: `buddy_imu.h` (pure) + `buddy_imu_test`
PASSED; shake >2.5 g for 300 ms → dizzy wobble with lids for 2.5 s, more shaking
→ annoyance; a sharp >3.2 g hit → 600 ms wide-eyed startle; tilt → gravity gaze;
face-down → 0.6 lid squint; zero/NaN samples are ignored and every timer expires.
Hardware proof with `imufake` + `dump 3`: upright shows full ellipses; `0 0 -1`
shows flat-topped half-ellipses (the squint lid). Build 1,342,490 B (42%), flashed
with `Hash of data verified`.

Still open: the colour verdict from the three bars (screen vs hardware), the
long-press decision (hold = picker today vs Sofía's 3 s = sleep), and the lower
impact IMU ideas (lift-to-wake, follow motion, rhythmic shake, 4.9 sleep).

L10: the rest of the IMU ideas that do not need a product decision are in.
`buddy_imu.h` now also handles:
- 4.3 lift: after resting flat (+Z) the Z axis drops below 0.55 g for 200 ms
  (not face-down) -> `wake` for 4 s; the tick clears `closed` and opens the eyes
  wide only while the state is `bored` asleep.
- 4.4 set it down: a soft >1.8 g spike after 1 g is the short (300 ms) version of
  the startle; a hard >3.2 g hit keeps the 600 ms one.
- 4.6 follow: |gyro X| > 40 dps pulls the gaze 8 px towards the turn for 500 ms.
- 4.7 rhythmic shake: 4 gyro direction changes inside 1.2 s with soft amplitude
  -> 2 s of pleased half-closed eyes, never while dizzy.
The pose merge now adds the tilt to the follow offset instead of overwriting it
(that was a real bug the new test caught). `buddy_imu_test` PASSED with the new
cases; `buddy_anim_test` still PASSED. Build/flash of this increment in progress.
Open: 4.9 (sleep on long press) needs the owner's choice because hold already
opens the screen picker, and the colour verdict needs the owner's eyes.

L11 (verbatim): "se ve azul rojo verde sy leo de izquierda a derecha con el
boton boot debajo" and "No agregar dormir por botón".

Colour root cause found. The three bars are drawn red | green | blue; the owner
read them left to right as **azul | rojo | verde**. That is exactly what a byte
swap inside `displayPush` produces on this panel (it takes the low byte first):
0xF800 -> 0x00F8 (blue), 0x07E0 -> 0xE007 (red), 0x001F -> 0x1F00 (green). The
cyan eyes 0x055F -> 0x5F05 -> green, which is why they looked "puramente verde".
The swap was removed (`s_panel = v;`), so the panel now gets red, green and blue
as drawn and the eyes should be cyan. No hardware fault.
The owner chose **no sleep gesture** for the button, so task 4.9 is intentionally
not implemented; the idle auto-off keeps working and the hold still opens the
picker.

L12 (verbatim): "que cuando esta pensando, o es como que cierre un poco un ojo y
rote un poco los circulos, cuando tarde mucho en responder o algo asi, como que se
canse y entrecierre los ojos (cortar la parte de abajo). Si queres busca e
investiga detalladamente las expresiones de EMO".

Thinking now has an EMO-style shape language, not just lids: `BuddyPose` gained
`rot` (the whole eye rotates), `lidL`/`lidR` (per-eye squint) and `bottom` (the
bottom lid cuts up: the tired look the owner asked for). `screen_buddy` draws a
rotated ellipse with a scanline quadratic and clips top/bottom with black bands.
The ladder: 0-3 s curious (both eyes tilt +8 deg, right eye 30% squint, gaze
up-left), 3-8 s focused (narrow, asymmetric), 8-20 s attention, 20 s+ tired
(bottom 35% cut, eye line sags -5 deg, long blinks, never asleep). Debug:
`buddyface <state> [ageSec]` pins a face for 20 s so `dump` can show it without
waiting; used to verify `thinking 1` (right eye flat top, left open) and
`thinking 25` (bottom cut) at step 2. Host tests extended with rot/lidR/bottom.

The EMO principle applied: charisma comes from asymmetry (one eye), tilt (the
eye line, not the whole face), the bottom lid for tiredness, and timing; the IMU
reactions supply the secondary motion the LCD cannot.

L13 (verbatim): "C:\Users\Máximo Majorel\Downloads\emociones buddy parte 2.md"

Sofía's second doc replaces section 3 and adds the functional channel. Implemented
the firmware side:
- `buddy_phase.h`: `busy|ok|ask|idle` parsed as authoritative (never derived from
  `state`), the default LED table, and `buddyPhaseFinished()` which encodes the one
  rule the bridge cannot send: `busy -> idle` is a finish on Desktop/Telegram.
- The poller reads `phase`, `led` and `elapsed`; `elapsed` now drives the state age
  (preferred over `since` because the device clock can be off). `/api/status` exposes
  `buddy.phase` and `buddy.led`.
- `buddy_led.cpp`: the bead is the phase light. Busy yellow solid, ask red slow
  pulse (1.5 s), idle cyan at 80%, and the 5 s green blink at 60% -> 300 ms fade to
  cyan on `ok` **and** on busy -> idle. New busy/ask cancels the celebration. Runs
  every loop, not only on SOFIA, so it is visible from afar.
- HUD in `screen_buddy.cpp` (top-right 20 px): three 4 px dots with a 1.2 s
  staggered opacity cycle for busy; a green check with the 250 ms 0.8->1.1->1.0 pop
  for ok; a red `!` pulsing 1.0<->1.15 at 600 ms for ask; idle draws nothing.
  Busy -> idle synthesizes the check for 3 s. The HUD sits over the face and never
  covers the eyes.
- Gesture rotation: `buddyGestureAt()` gives a new slot every 3-6 s (seed and slot
  hashed, so the rhythm is irregular). Thinking cycles up-left / narrow / look+blink
  / up-right / long blink, and after 20 s adds the tired bottom-lid "uf" gesture.
  Tool cycles reading / one-eye focus / tension / double blink, and after 30 s adds
  the sigh. The eyes' FACE moved to 0x00B4FF so the glass cyan matches the LED.
- Host tests: buddy_phase (names, finish rule, colours), buddy_anim (slot timing,
  five/six thinking gestures, never asleep, tool cycle and double blink), plus the
  existing buddy_state/imu/ui_color; all PASSED.

L14: verification of the second doc. Build 1,350,038 B (42%), flashed "Hash of
data verified"; host tests buddy_phase / buddy_state / buddy_anim / buddy_imu /
ui_color all PASSED.
- LED: an ask run was sampled every 500 ms. thinking/busy -> rgb `255,190,0`
  (yellow solid). done/ok -> `0,153,48` and `0,0,0` alternating (green at 60%,
  250 ms on/off), then `0,227,145` (the 300 ms fade), then idle `0,144,204`
  (0.8 of 0,180,255). Idle is the same cyan as the eyes (0x00B4FF).
- HUD: `dump 2` during the run showed the three dots at the top-right; after the
  run the green check; `buddyphase ask 30` showed the red `!` (bar + dot) and the
  status reported `phase:ask`, `led:[255,40,40]` with the rgb base pulsing
  (138 -> 254 -> 170 -> 110), i.e. the 1.5 s red pulse.
- Debug commands added: `buddyphase <idle|busy|ok|ask> [sec]` pins the HUD/LED for
  15 s; `buddyface <state> [ageSec]` pins a face for the dumps.
- The busy -> idle synthesis is `buddyPhaseFinished()` (unit-tested): the same
  branch paints the check and starts the green blink, so Desktop/Telegram runs
  (which never send `ok`) get the completion signal.
- Not implemented (optional in the doc): the label keyword hints ("terminal",
  "web", "archivo"); the generic thinking/tool cycles cover every tool.

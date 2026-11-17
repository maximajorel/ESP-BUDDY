# Sofía: idle life, a real angry face, smooth edges

Second polish pass on the buddy face: the eyes already react, but they sit
still while idle, anger reads as "dizzy with red eyes", and the edges and glow
step instead of fading. This pass adds the idle micro-motion that makes solid
eyes feel alive, gives anger its own anatomy, and dithers the borders.

## Specs

S1. Microsaccades — verbatim: "Saltos de mirada de 1–3 px cada 2–5 s mientras
está `idle`."

S2. Irregular blinking with bursts — verbatim: "No un timer fijo. Intervalo
aleatorio 2–6 s, con **ráfagas** (a veces dos seguidos, a veces uno largo)."

S3. Idle breathing — verbatim: "Ciclo lento de 4–5 s que mueve los ojos 1 px
arriba/abajo, como pecho que sube y baja."

S4. `busy → idle` synthesized on the board — verbatim: "La placa tiene que
**fabricar** el tilde y el titileo verde al detectar esa transición" and
"⚠️ **Guardá el `phase` anterior.** Si titilás cada vez que el poll devuelve
`idle`, el LED titila para siempre y pierde todo el sentido: el titileo es una
**transición**, no un estado."

S5. Anger is its own face — verbatim: "Cuando se enoja cuando lo sacudo, no
parece tanto una cara de enojo, si no que reusa la de mareo pero con ojos
rojos, ahi tenemos que hacer que tiemble los ojos, que se agrande y que las
cejas sean de expresividad de enojo". later corrected by S17: the emotion is
carried by the angled eyelids, not by brows. So anger stops reusing the dizzy
orbit: the eyes tremble, they grow, and the black lid block angles.

S6. Soft edges and a glow that fades — verbatim: "veamos que queden mas suaves
los bordes, el glow no transiciona tan buen pero puede ser un tema de
limitacion de la pantalla...". Ordered dithering against the layer behind, and
a multi-step halo gradient, instead of hard one-pixel steps.

S7. Side glances and perspective — verbatim: "ademas me gustaria que pueda
mirar a los costados cada tanto, creo que se puede ir jugando con la
perspectiva". The idle face looks left and right every few seconds, and the
pair foreshortens: the eye the gaze leads with narrows, the other widens.

S8. More life, never a still picture — verbatim: "estaria bueno agregar mas
estados, animaciones, para que sea mas vivo." Every continuous channel stays
eased; new gestures ride on top instead of replacing the existing ladders.

S9. Dizzy and annoyance stay different — verbatim: "|a| > 2.5 g sostenido ~300
ms → ojos en espiral, párpados caídos, bamboleo 2–3 s y se recupera solo. **Si
insistís**, no vuelve al mareo: pasa a **fastidio** (ojos apretados)." Dizzy is
worried brows, droopy lids, an orbit; annoyance is angled brows, a tremble, a
hot tint and a bigger eye. The two poses must not share their anatomy.

S10. Tilt compensation — verbatim: "Con el acelerómetro: las pupilas se van
hacia \"abajo\" real." Already delivered by `buddy-smooth-expressions` S4; this
pass keeps it and pins it with a regression test only.

S11. Lifted → awake — verbatim: "Cambio sostenido en Z (de apoyado a en mano) →
apertura de ojos en 100 ms desde dormido." Already delivered by
`buddy-smooth-expressions` S4; kept and regression-tested.

S12. Deep inactivity closes the eyes and freezes the face — verbatim: "Despues
de tiempo de bastante inactividad cierre los ojos y que ahi no pase nada". The
asleep face keeps no breath, no saccade, no blink.

S13. Random distractions on the glass — verbatim: "QUe cada tanto aparezcan
eventos aleatorios, donde aparezca algo en pantalla con lo que se distraiga, por
ejemplo algun bicho, o que se ponga a jugar con algo, esta animacion termina
cuando yo me empiezo a comunicar con sofi". A bug crawls across the glass, the
eyes follow it, and the event dies the moment the bridge says Sofía is busy,
thinking or talking.

S14. Anger is red, slanted and loud — verbatim: "Cuando se enoja aparece los
ojos como cansados no enojados y tiembla, ahi se tiene que poner rojo y que y
que sea asi la posicion de las barras negras: \"/\" ahora esta al reves.
ademas tiene que ponerse rojo el led de fondo". So: fully red eyes, the black
upper lid blocks angled like "\/" (inner corners down, which the eye's own rotation gives
once the lid follows that rotation instead of staying horizontal), the tremble,
no droopy bottom lid, and the rear bead red while the anger lasts.

S15. Sleeping on the table shows ZzZ — verbatim: "Si esta mucho tiempo acostado
aparte de estar con los ojos cerrados, que aparezca una animacion de \"ZzZz\"
para que parezca que este durmiendo, se despierta si muevo el ESP". Lying flat
for a while with the eyes shut floats Z letters up from the face; any real
movement wakes it at once.

S17. No brows, ever - verbatim: "Otra cosa que vi que es como que a veces se
forma unas cejas que son unas rayas finas, no, no tengamos cejas, solo parpados
que seria como el bloque negro superior". The face has no eyebrow lines: the
only upper feature is the black eyelid block. Every brow channel, helper and
test is removed, and the eyelid is what angles for anger.

S16. The diagnostic the panel needs — the board has no readable bus, so when a
pixel is wrong the only honest question is "which numbers drew it": the console
gains `buddydraw`, which prints the geometry of the last face drawn (eye boxes,
lid, brow, growth, push).

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S1,S2,S3,S7 | inline: `buddy_anim.h` fixation saccades, offset-slot blink bursts, breath, glances; `buddy_face.h` perspective, tremble, coverage/dither, lid line; tests | d1eabb5 |
| T2 | S5,S9 | inline: `buddy_imu.h` rage anatomy (grow, brows, tremble) and a worry-browed dizzy; test | 285f566 |
| T3 | S4 | inline: `buddy_phase.h` HUD transition machine; test | 5bc94f7 |
| T4 | S5,S6,S7,S8 | inline: `gfx` dithered squircle, three-step halo, brows, tremble, perspective, dithered lid line, HUD twinkle, sceptical error brows | c487fe6 |
| T5 | S1-S11 | inline: host suite, firmware compile, hardware probe on COM5, log and commits | 41aa6ed |
| T6 | S12 | inline: the asleep face freezes in `buddy_anim.h`; test | 9dda017 |
| T7 | S14,S17 | inline: no brows anywhere; rotated eyelid cut in `gfx`, red anger in `buddy_imu.h`, red bead in `buddy_led.cpp`; tests | 9dda017 |
| T8 | S13 | inline: `buddy_anim.h` distraction schedule and bug path, eyes track it, abort on any Sofía activity; test | 9dda017 |
| T9 | S15 | inline: `buddy_imu.h` lying time and movement wake, ZzZ in `screen_buddy.cpp`; test | 9dda017 |
| T10 | S9,S12-S17 | inline: dizzy spiral and the bead mood, then the host suite, compile, flash and probe on COM5 | ae7ad27 |

## Log

L1 (verbatim): "trabajemos en pulir las expresividades y pulir lo uqe ya
tenemos en el buddy de SOFIA, ahora los ojitos me gustan pero tiene solo una
animacion cuando piensa, ademas me gustaria que pueda mirar a los costados cada
tanto, creo que se puede ir jugando con la perspectiva, quiero que la animacion
sea mas smooth, y estaria bueno agregar mas estados, animaciones, para que sea
mas vivo." With the seven-item list (microsaccades, blink bursts, breathing,
synthesized `busy → idle`, shake ladder, tilt compensation, lift-to-wake) and
the closing brief: "veamos que queden mas suaves los bordes, el glow no
transiciona tan buen ... ahi tenemos que hacer que tiemble los ojos, que se
agrande y que las cejas sean de expresividad de enojo... realiza un analisis de
las posibles expresiones e implementemos".

L2 (analysis, before code). Expressions the geometry can carry with the
existing primitives, ranked by what they cost: (a) brows above each eye, a
thick angled line: inner corner down = anger, inner corner up = worry, one of
each = scepticism/confusion; (b) eye scale, bigger = intensity (anger) or
surprise (already), smaller = focus; (c) per-eye lid asymmetry (already);
(d) tremble, a 25 Hz jitter applied after the easing filter so the filter
cannot eat it; (e) foreshortening, one eye narrower than the other, which reads
as the head turning; (f) lid + bottom lid together = a slit, the classic angry
eye. Everything above is one new pose channel each, so every existing
expression keeps its current look at the default zero. Chosen for this pass:
brows (anger, worry, scepticism on the error face), eye growth for anger,
tremble, foreshortening for the side glances.

L3 (implementation). `buddy_anim.h`: `buddySaccadeAt` places one 1-3 px jump on a
3.6 s slot whose offset is hashed (gaps 2.2-5.0 s) and eases the first 60 ms of
it; `buddyBlinkAt` places a blink on a 4 s slot with a hashed offset (gaps
2.1-5.9 s), adds a companion blink 300 ms later in 30% of the slots and a
260 ms long blink in 20%; `buddyGlanceAt` looks 10-18 px to a side once per 12 s
for 1.4-1.8 s; `buddyBreath` is one 4.5 s sine. The idle pose is the sum of the
three, with `bob = breath * 1 px`. The error face gained the sceptical brows
(one lifted, one dropped).

`buddy_face.h`: `buddyPerspective` (2 px lead, 6% foreshortening, 3% widening),
`buddyTremble` (25 Hz, applied after the ease), `buddySquircleSdf` /
`buddySquircleCoverage`, `buddyBayer4` / `buddyDitherLit`, `buddyLidLineTop` and
`buddyLidLineBottom` for the sub-pixel lid. The pose grew `browL`, `browR`,
`grow` and `tremble`, all neutral by default and all eased by the filter.

`buddy_imu.h`: anger is `grow 1.12`, `browL/R 0.9`, `tremble 1.0`, `lid 0.32`,
`bottom 0.12`, `rotL/R ±8`, `hot 0.9` and the existing wall slam; dizzy keeps
the orbit, `lid 0.38`, `hot 0.12` and now raises the inner corners
(`browL/R -0.5`). `buddy_phase.h`: `BuddyHud` with `buddyHudStep`,
`buddyHudShown` and `buddyHudCheckAge`.

`gfx.cpp`: `gfxFillSquircleRotAA` fills the core as spans and dithers the
one-pixel boundary against the layer behind it. `screen_buddy.cpp`: the halo is
three dithered rings at 7/17/32%, the eye body is AA against the inner ring, the
lid line is dithered at its sub-pixel remainder, brows are drawn above the lid
bands, `grow` scales the box, `buddyPerspective` leads and foreshortens per eye,
the tremble is added after `buddyPoseFilterStep`, and the HUD draws whatever
`BuddyHud` says, with a green twinkle over the first 900 ms of the check.

L4 (evidence). Host suite: 18/18 green, including the new checks on saccade gaps
(2-5 s), blink gaps (bursts under 600 ms, regular gaps under 6.2 s), glance
range (10-20 px, returns to centre), breath (4.5 s, one pixel), perspective
(leads and foreshortens), tremble (bounded, keeps shaking), coverage and dither,
lid-line sub-pixel rest, the angry vs dizzy anatomy, and the HUD guard (repeated
idle polls never re-fire the check, ask cancels it, ok stays while reported).
Firmware: arduino-cli compile with the INSTALL.md FQBN on the committed tree,
exit 0, 1,415,086 bytes (44%), no warning from any buddy/gfx file. Hardware: not
flashed yet; the board is on COM5 and flashing needs the owner's go-ahead in this
session. The probe that would prove it on the glass is
`tools/probes/buddy_life.txt`: neutral brows on the old expressions, the angry
anatomy after two shakes, the dizzy face after one, and the synthesized check
after a `busy -> idle` phase drop.

L5 (verbatim, the owner's next list): "1.- Despues de tiempo de bastante
inactividad cierre los ojos y que ahi no pase nada 2.- QUe cada tanto aparezcan
eventos aleatorios, donde aparezca algo en pantalla con lo que se distraiga, por
ejemplo algun bicho, o que se ponga a jugar con algo, esta animacion termina
cuando yo me empiezo a comunicar con sofi 3.- Cuando se enoja aparece los ojos
como cansados no enojados y tiembla, ahi se tiene que poner rojo y que y que sea
asi la posicion de las barras negras: \"/\" ahora esta al reves. ademas tiene
que ponerse rojo el led de fondo 4.- Si esta mucho tiempo acostado aparte de
estar con los ojos cerrados, que aparezca una animacion de \"ZzZz\" para que
parezca que este durmiendo, se despierta si muevo el ESP" plus "vamos bien!".

L6 (hardware finding, before L5). The first flash of this pass proved the
synthesized check, the brows, the AA edges and the angry growth on the glass,
and the `buddydraw` diagnostic caught a real bug the host tests could not: the
right eye's brow was slanted with the mirrored sign, so anger read as worry on
that side. `buddyBrowLine` moved the geometry into the pure header with a test
that pins "both inner corners drop", and the firmware was reflashed with the fix.

L7 (verbatim): "Otra cosa que vi que es como que a veces se forma unas cejas que
son unas rayas finas, no, no tengamos cejas, solo parpados que seria como el
bloque negro superior". The brows added in T4/T2 come out entirely: the pose
loses browL/browR, buddyBrowLine and drawBrow are deleted, the error face loses
its slanted lines, and the anger angle moves to the eyelid itself (the lid cut
rotates with the eye, so the black block reads "\/" while angry).

L8 (delivery and evidence for S12-S17). One work-unit commit (09f0ac1) carries
the whole pass, because the pieces share their functions: removing the brow
channels without touching `drawFace` would not compile, and the sleep and the
bug both live in the same tick. 1,417,510 B (45%), uploaded with "Hash of data
verified". Host suite 18/18 green, including the new checks: the asleep face
freezes (closed, bob 0, no blink for 6 s of frames), the bug schedule (several
events, on the glass, eyes travel, one-way crawl), the ZzZ loop (rises, stays by
the face, fades), lying and the movement wake, and the lid angle ("both lids
fall towards the nose").

Hardware, `tools/probes/buddy_life.txt`, COM5:
- `buddydraw` while angry: `grow=1.12 lid=0.46 hot=1.00 deg=+9.1/-12.9`, and
  `fbpix` inside an eye returns 0xF9E3 = rgb(248,60,24): the eyes are the hot
  red, not the owner colour. Opposite rotations are what makes the black lids
  read "\/".
- The same probe with `imufake 0 0 3 0 0 0` shows the dizzy and the angry faces
  as different shapes.
- `buddyphase busy` -> `idle` draws the check (2 HUD rows with ink at +0.5 s)
  and nothing after the 3 s window: the guard holds on the glass.
- `bug 8` while idle puts the amber bug (0xFD84 body, 0x7A42 legs) at
  x 114..133, y 64..78; the next `buddyphase busy` frame has no bug at all.
- `buddyface bored 130` with the board lying: the eyes are two shut bars and
  the Z letters drift on the right side of the glass.
- The rear bead's red anger and its faint ember while asleep are code-reviewed
  and host-tested through `buddyMood()`, but a single WS2812 cannot be read
  back: they are the one item this probe cannot prove.

L9 (audit pass). The objective's item 5 says "ojos en espiral", and the dizzy
reaction was a fixed ellipse: the gaze kept its reach instead of winding in.
`buddyImuPose` now spirals on the reaction's own clock (radius 12 -> 3 px while
it turns two and a half times), pinned by "the dizzy gaze turns" and "spirals
inwards instead of orbiting".

The bead's red anger was the one decision without a host test, because the mood
was an inline ternary in two places in `screen_buddy.cpp`. It moved to
`buddyMoodFrom()` in `buddy_imu.h` with four assertions (sleep wins over rage,
anger from the flag or from the tremble, calm otherwise), and `buddydraw` now
prints the mood, so the decision is provable on the glass even though the WS2812
itself cannot be read back.

Hardware on COM5 after this pass (1,417,606 B, hash verified):
`draw mood=calm ... ` at rest; `draw mood=angry grow=1.12 lid=0.46 hot=1.00
deg=+10.1/-11.9` and, 500 ms later, `deg=+7.2/-14.8` — the tremble moving the
eyes; and with a deeply bored face `draw mood=sleeping ry=4`, the shut bars.

L10 (final audit, measured pixels). Two items were only tallied by colour
before; both are now measured as ordered sequences with `fbpix` on the flashed
firmware:

- The halo fades instead of stepping. A horizontal scan at y=88 across the
  right eye's edge walks the four shades in order and nothing jumps: eye 100%
  (0x05BF) -> halo 32% (0x01CA) -> 17% (0x00E5) -> 7% (0x0062) -> black, each
  step two pixels wide, with the 1 px dither at every boundary. That is the whole
  answer to "el glow no transiciona tan bien": the panel has no alpha, so this
  ordered ladder is the smoothest transition it can physically show.
- The angry eyelid really slants. On one eye the lid cut ends at y=83 on the
  outer side and is still black past y=119 on the inner side: the black block
  falls towards the nose, so the pair reads "\/". The lid block also covers the
  halo above it: the only colour anywhere above the cut line is black.
- The angry eye's pixels are 0xF9E3 = rgb(248,60,24) with `hot=1.00`, and
  `buddydraw` reports mood=angry, grow=1.12, lid=0.46, deg +11.2/-10.8.

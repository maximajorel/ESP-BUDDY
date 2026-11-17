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
cejas sean de expresividad de enojo". So anger stops reusing the dizzy orbit:
the eyes tremble, they grow, and two angled brows carry the emotion.

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

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S1,S2,S3,S7 | inline: `buddy_anim.h` fixation saccades, offset-slot blink bursts, breath, glances; `buddy_face.h` perspective, tremble, coverage/dither, lid line; tests | d1eabb5 |
| T2 | S5,S9 | inline: `buddy_imu.h` rage anatomy (grow, brows, tremble) and a worry-browed dizzy; test | 285f566 |
| T3 | S4 | inline: `buddy_phase.h` HUD transition machine; test | 5bc94f7 |
| T4 | S5,S6,S7,S8 | inline: `gfx` dithered squircle, three-step halo, brows, tremble, perspective, dithered lid line, HUD twinkle, sceptical error brows | c487fe6 |
| T5 | S1-S11 | inline: host suite, firmware compile, hardware probe (needs the owner), log and commits | — |

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
Firmware: arduino-cli compile with the INSTALL.md FQBN, exit 0, 1,415,074 bytes
(44%) and no warning from any buddy/gfx file. Hardware: not flashed yet; the
board is on COM5 and flashing needs the owner's go-ahead in this session.

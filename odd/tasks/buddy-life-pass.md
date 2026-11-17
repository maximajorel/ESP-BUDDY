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
| T1 | S1,S2,S3,S7 | inline: `buddy_anim.h` fixation saccades, Poisson blink bursts, breath, glances; `buddy_face.h` perspective, tremble and dither helpers; tests | — |
| T2 | S5,S9 | inline: `buddy_imu.h` rage anatomy (grow, brows, tremble) and a worry-browed dizzy; test | — |
| T3 | S4 | inline: `buddy_phase.h` HUD transition machine; test | — |
| T4 | S5,S6,S7,S8 | inline: `gfx` dithered squircle, gradient halo, brows, tremble, perspective, HUD twinkle in `screen_buddy.cpp` | — |
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

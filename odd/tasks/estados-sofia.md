# Estados de Sofía: más reacciones para idle, trabajando y "te necesita"

Etapa 2 del pedido del dueño (etapa 1: `concentracion-reloj-grande.md`). Acá se
diseña **el vocabulario de reacciones** de la cara por estado, no la web. La
pantalla tiene 320x172, solo ojos, sin boca ni sonido: todo lo que se comunica
sale de párpado, mirada, forma, brillo y ritmo.

## Specs

S1. Idle — verbatim: "Idle". La cara quieta gana reacciones nuevas que la lean
como compañía y no como protector de pantalla: el chequeo del reloj cuando hay
Concentración, la mirada que busca al dueño de vez en cuando, y el estiramiento
después de mucho tiempo sin nada.

S2. Trabajando — verbatim: "Cuando esta trabajando". `thinking` y `tool` dejan de
compartir una sola expresión: leer y usar una herramienta tienen ritmos
distintos, y una tool larga tiene que mostrar paciencia sin cambiar de estado.

S3. "Te necesita" — verbatim: "A veces Hermes, desde la app, telegram o canales
compatibles, solicita ejecutar codigo o precisa que le responda cosas, este
quiero que sea un nuevo estado y nueva reacción". Estado nuevo `needs_you`: se
lee desde el otro lado del cuarto y se distingue de `error` y de `waiting`.

S4. El canal no está verificado — decisión del dueño: "Solo doc: lo dejo
cosmetico". Las aprobaciones de Hermes viven en memoria del gateway
(`docs/IDEAS.md`) y el bridge no las ve: el diseño documenta la reacción, la
implementación del canal queda pendiente y **nada promete que se dispare**.

S5. Sin tocar lo que anda — las reacciones nuevas se suman a las escaleras
existentes de `buddy_anim.h` (parpadeo irregular, microsacadas, respiración, 12
eventos de idle, IMU, pelotita, temporizador) y no cambian el contrato con el
bridge: el estado lo nombra el servidor, la emoción y la duración son de la
placa.

S6. Se puede mirar antes de programar — cada dirección es un artboard autónomo
bajo `design/estados-sofia/` con las poses clave, sus tiempos y el tradeoff que
hace, para que el dueño elija antes de que el firmware cambie una línea.

S7. Todo respeta el sistema — el diseño usa la geometría y los colores del vidrio
de `design/system.md` (ojo `#00b4ff`, calor `#ff3c1e`, oro `#ffd700`, negro) y
las reglas de forma: squircle con radio 0.38, párpado negro pegado al borde, sin
cejas, sin boca.

## Tasks

| Task | Specs | Ruta | Commit |
| --- | --- | --- | --- |
| T1: documento y trackeo de la etapa | — | inline | `998715e` |
| T2: artboards de las tres direcciones | S1, S2, S3, S6, S7 | inline | `aa851e6` |
| T3: la dirección de idle quedó elegida para implementar (handoff del artboard 01) | S6 | decidir | — |
| T4: gestos de quietud de idle: estiramiento y vistazo al dueño | S1, S5, S7 | inline | verificando |
| T5: el temporizador mira su reloj: ritmo bajo 5 min y último minuto | S1, S5 | inline | verificando |
| T6: proporción y color nuevos del temporizador en el vidrio | S7 | inline | verificando |
| T7: `needs_you` en el bridge y en el firmware, marcado cosmético | S3, S4 | por definir | — |

## Log

L1 — pedido del dueño, verbatim:

> 2.- Usando gentle design quiero mas diseños y reacciones para estados:
> - Idle
> - Cuando esta trabajando
> - Investiga como funciona esto:
>  - A veces Hermes, desde la app, telegram o canales compatibles, solicita
>    ejecutar codigo o precisa que le responda cosas, este quiero que sea un
>    nuevo estado y nueva reacción

L2 — decisión del dueño sobre el canal de aprobaciones: "Solo doc: lo dejo
cosmetico". Decisión sobre el orden: "Reloj primero" (etapa 1).

L3 — diseño escrito en `design/estados-sofia/`: `01-idle-compania.html`
(Idle, dirección "compañía de escritorio"), `02-trabajando-ritmo.html`
(trabajando, dirección "leer no es teclear") y `03-te-necesita.html`
(`needs_you`, dirección "insistencia con techo"). Los tres son direcciones
alternativas entre sí en cuanto a cuánto se mueve la cara; ninguno está
implementado todavía.

L4 — handoff del artboard `design/estados-sofia/01-idle-compania.html`
("implement") al firmware. Lo que se implementó de esa dirección:

- **Estiramiento**: `buddyStretchAt(ageMs)` en `buddy_anim.h`, 1.4 s cada 240 s de
  quietud (nunca antes de los primeros 4 min), con plateau para que se lea como
  cuerpo que se mueve y no como rebote.
- **Busca al dueño**: `buddyOwnerLookAt(ageMs, seed)`, un vistazo de 1.1 s por
  slot de 75 s que cae en los primeros 30 s del slot (separación 45–105 s, con
  hash, sin estado).
- **Nada encima de un evento de idle**: `buddyPose` aplica los gestos de quietud
  sólo si `buddyIdleEventAt` no está activo; el bicho y el bostezo ganan.
- **Con reloj**: bajo 5 minutos el vistazo al número pasa de 9 s a 4.5 s
  (`buddyFocusDipPeriodMs`), y en el último minuto hay una ventana de 900 ms con
  doble parpadeo y mirada al dueño (`buddyFocusFinalMoment`,
  `buddyFocusFinalBlink`), todo puro y sin flags.
- **Proporción y color**: ojos 32×24 a ±22 (12 px de aire, la silueta de la cara
  completa), dígitos 68×104 con trazo 13, y el número y la barra pintados con
  `eyeRgb` — el color que los ojos ya resolvieron en ese frame.

Verificación de ese handoff:

- RED antes de implementar: `buddy_anim_test` no compilaba por `BuddyOwnerLook` y
  `buddyQuietGestures` inexistentes.
- `python3 tools/run_tests.py` → `33 passed` (incluye 14 chequeos nuevos de los
  gestos de quietud y 15 del reloj: ritmo, ventana final, doble parpadeo,
  silueta de los ojos y trazo del dígito).
- `python3 tools/verify_assets.py` → `PASSED (0 problems)`.
- `arduino-cli compile` con el FQBN de `INSTALL.md` §3 → 1535550 B de flash (48%)
  y 55884 B de RAM (17%). Compila.
- Hardware pendiente: no se flasheó (pide autorización explícita).

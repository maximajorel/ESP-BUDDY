# Concentración: un temporizador abajo de los ojos de Sofía

Segundo pedido del dueño. La pantalla SOFIA gana un modo de concentración: una
cuenta regresiva dibujada **debajo de los ojos**, que se puede arrancar desde la
web pública **sin iniciar sesión**. Sofía no pierde nada de lo que ya hace —
parpadea, mira, se enoja, reacciona al bridge — y el temporizador no se cae
cuando el bridge cambia de fase o llega un `/api/face`. Al terminar, la cara
celebra y el LED se pone verde.

## Specs

S1. Arranque sin sesión — verbatim: "Sin necesidad de iniciar sesion, quiero
agregar "Concentración"". La entrada pública `http://192.168.4.1/` ofrece
arrancar la concentración en la pantalla de SOFÍA con la misma autoridad con la
que hoy elige la pantalla (`POST /api/screen`, que ya es pública). La ruta nueva
se agrega a la whitelist pública existente; `/admin`, la configuración, la red y
la cuenta siguen exigiendo sesión, y la ruta nueva no expone ningún dato de
configuración.

S2. El temporizador vive en el vidrio — verbatim: "que sea un temporizador que
se muestra en pantalla de SOFIA, debajo de los ojos". El número se dibuja en la
pantalla de Sofía, por debajo de la línea de los ojos, con el reloj de la placa
como fuente de verdad (no un contador de frames). La landing web muestra el
estado y manda la orden, pero no replica el temporizador.

S3. Sofía no pierde nada — verbatim: "sofia conserva sus animaciones y
reacciones, pero aparece el temporizador abajo". Parpadeo, gaze/head-turn,
moods, la pelotita y el resto de la máquina de expresiones siguen funcionando
mientras la concentración está activa; el temporizador ocupa la banda inferior y
no reemplaza la cara.

S4. El temporizador no se cae — verbatim: "Si le habl oa sofia y detecta cambios
en el bridge el temporizador no desaparece". El estado del temporizador es
independiente de la fase del bridge: cambiar de fase (idle → thinking → tool…),
recibir `/api/face`, tocar el botón o cambiar de pantalla y volver **no**
detiene, no reinicia ni oculta la cuenta. Solo lo terminan su propio vencimiento
o una orden explícita de cancelación.

S5. Sofía estudiosa — verbatim: "aqui haremos animaciones nuevas que representen
a una "Sofia concentrada y estudiosa"". Animaciones nuevas y propias del modo:
algo que se lea como "está estudiando" (por ejemplo, la mirada baja hacia el
número y vuelve, un ritmo de parpadeo más lento y parejo, una marca de progreso
que acompaña la cuenta). No se inventan sonidos ni se usa la boca: el equipo no
tiene audio propio.

S6. Final — verbatim: la decisión del dueño "Cara + LED, sin sonido". Al llegar a
cero: una animación de logro en el vidrio (una sola vez, no en loop) y el LED RGB
en verde. Sin sonido. Después del festejo, la cara vuelve a su estado normal y el
temporizador desaparece.

S7. Duración — default 25 minutos, con presets 15 / 25 / 45 / 60 y un campo
libre acotado a 1–180 minutos. Arrancar de nuevo con otra duración reemplaza la
cuenta en curso; cancelar la apaga y devuelve la cara a su estado normal.

S8. Lógica pura y testeable — la máquina de estados del temporizador (duración,
tick, vencimiento, cancelación, supervivencia a cambios de fase y de mood) vive
en un header puro de `esp32_gadget/`, al estilo de `buddy_phase.h` /
`buddy_ball.h`, con su `tools/*_test.cpp` compilado por `python3
tools/run_tests.py` sin placa.

S9. La web nueva — el control de Concentración nace sobre el rediseño de
`odd/tasks/web-redesign.md` (papel claro, stickers, cards), no sobre la UI vieja:
en la landing, una card en la franja de juegos/herramientas con los presets, el
campo libre, el botón de arrancar y el estado en curso.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S1,S2,S4,S8 | mapa del firmware: cómo dibuja `screen_buddy`, dónde vive la fase del bridge, cómo se sirve una ruta pública y qué hace `POST /api/face` | pending |
| T2 | S8 | `buddy_focus.h` puro + `tools/buddy_focus_test.cpp` (RED primero) | pending |
| T3 | S1 | ruta pública acotada `POST /api/focus` + whitelist de `admin_auth`/`net` | pending |
| T4 | S2,S3,S5 | dibujo del temporizador bajo los ojos en `screen_buddy` + animaciones del modo estudioso | pending |
| T5 | S6 | cierre: animación de logro + LED verde | pending |
| T6 | S9 | card de Concentración en la landing nueva | pending |
| T7 | todas | suite host, `panel_http_test.py`, pack, compilación, evidencia de hardware | pending |

## Log

L1 (verbatim, del mensaje del dueño): "2.- Sin necesidad de iniciar sesion,
quiero agregar "Concentración" y que sea un temporizador que se muestra en
pantalla de SOFIA, debajo de los ojos, sofia conserva sus animaciones y
reacciones, pero aparece el temporizador abajo, aqui haremos animaciones nuevas
que representen a una "Sofia concentrada y estudiosa" . Si le habl oa sofia y
detecta cambios en el bridge el temporizador no desaparece"

L2 (decisiones del dueño): el temporizador se arranca sin login; los ojos y las
reacciones se conservan; los cambios de bridge (y de mood) no lo interrumpen; al
terminar, "Cara + LED, sin sonido".

L3: Decisiones de ingeniería tomadas para no frenar el trabajo. (a) El estado
vive en el firmware y se apoya en el reloj de la placa; la web solo manda
duración y consulta el estado, así un recargado del navegador no pierde la
cuenta. (b) La duración default son 25 minutos con presets 15/25/45/60 y campo
libre 1–180. (c) Cancelar es explícito (`minutes: 0`); ningún otro evento apaga
la cuenta. (d) El temporizador solo se dibuja en la pantalla SOFÍA; si el dueño
se va a otra pantalla, la cuenta sigue corriendo y vuelve a mostrarse al volver.

# Rediseño de la web: papel claro, stickers y el admin en pestañas

Primer pase del rediseño pedido por el dueño. La web del equipo (landing
pública, panel `/admin` y `/login`) pasa al lenguaje visual de
`DESIGN.md` que él mismo dejó en Descargas (Slush: papel pastel, bordes negros
tipo sticker, radios blandos, tipografía display aplastada), y **los ojos
desaparecen de toda la web**: la cara de Sofía se queda en el vidrio del equipo,
donde corresponde. El panel deja de ser un scroll de diez regiones con sidenav y
pasa a secciones separadas en cards tipo pestañas.

## Specs

S1. Rediseño integral con la referencia del dueño — verbatim: "Quiero rediseñar
la UI de toda la web, sacaremos los ojos, y mejoraremos el admin separando
inteligentemente las secciones en cards tipo pestañas, nuestra referencia de
diseño: '/mnt/c/Users/Máximo Majorel/Downloads/DESIGN.md'". Landing, admin y
login usan el mismo sistema: papel claro (`#ffffff`, `#dceeff`, `#cccccc`,
`#e9e9e9`), tinta `#000000`, bordes negros de 1px, radios 20–40px en cards y
pill (1600px) en navegación/botones/chips, la paleta sticker completa
(`#4da2ff`, `#55db9c`, `#e9ccff`, `#fb4903`, `#ffd731`, `#5c4ade`) repartida en
superficies y stickers, **sin gradientes y sin box-shadows** (la elevación es
color + borde). Botones: relleno negro con texto blanco o fantasma blanco con
borde negro; el azul no es color de acción. Las franjas de sección alternan
cielo / blanco / gris con grit.

S1b. Tipografía del sistema, no webfonts — el panel vive en el AP del equipo y
se abre en un teléfono sin internet (`README.md`: "no necesita internet"; el
firmware no sirve fuentes). El efecto display aplastado se consigue con peso
800/900, caja alta, `letter-spacing: -0.045em` y `line-height: 0.8` sobre
`-apple-system, "Segoe UI", Roboto, …`. Se documenta la desviación en
`design/system.md` en vez de inventar una fuente.

S2. Los ojos fuera de la web — verbatim: "sacaremos los ojos". Se eliminan de
`web_ui/`: `faceMarkup()`, `mood()`, `gaze()`, `blink()`, `drift()`, el host
`[data-face="hero"]`, las llamadas `face(...)` del ta-te-ti y el bloque `.face`
de `styles.css`. El vidrio conserva la cara y sus animaciones
(`buddy_face.h`, `screen_buddy.cpp` no se tocan). La landing sigue llamando
`POST /api/face` solo si hace falta que el equipo reaccione; no dibuja nada.

S3. Admin en cards tipo pestañas — verbatim: "mejoraremos el admin separando
inteligentemente las secciones en cards tipo pestañas". Un solo `tablist`
(`role="tablist"`), una pestaña por sección con los ids actuales
(`aplicaciones`, `pantalla`, `luz`, `juegos`, `spotify`, `integraciones`, `red`,
`sofia`, `sistema`, `cuenta`) para que `href="#…"`, `/admin#spotify` y los
`aria-labelledby` sigan valiendo. Roving tabindex, `aria-selected`,
`aria-controls`, teclado ←/→/Home/End, `hash` = pestaña activa (deep-link y
atrás/adelante), y en el teléfono la tira scrollea sola hasta la pestaña activa.
Cada pestaña muestra sus secciones como cards; no queda scroll de diez regiones.

S4. Cero regresión funcional. Todos los ids, textos, endpoints, formularios y
estados que `app.js` y `landing.js` tocan siguen existiendo y funcionando; el
juego, el carrusel de pantallas, el menú a pantalla completa, el toast, el
banner de credenciales y el cierre de sesión se comportan igual. Assets inline
(sin CDN, sin packs de iconos, sin fuentes), servidos por el `WebServer` del
equipo: `python tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h`
regenerado y `tools/verify_assets.py` limpio.

S5. El login también — verbatim: "toda la web". `/login` usa las mismas cards,
pills y bordes; conserva el formulario, los mensajes de error y el foco.

S6. Accesibilidad — skip link, foco visible en todo control, contraste AA
(texto negro sobre cualquier sticker; blanco solo sobre negro o violeta),
`prefers-reduced-motion` respetado en marquesina y transiciones.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S1,S1b,S6 | inline: tokens y primitivas en `styles.css` (superficies, tipografía, cards, botones, pills, marquesina, tablist) | deade10 |
| T2 | S2,S3,S4 | inline: `admin.html` a tablist + cards y `app.js` tab controller | deade10 |
| T3 | S1,S2,S4 | inline: landing nueva sin ojos (`index.html`, `landing.js`, CSS de landing) | deade10 |
| T4 | S5 | inline: `login.html` al sistema nuevo | deade10 |
| T5 | S4 | `pack_web.py`, `run_tests.py` (31 ok), `verify_assets.py` limpio | deade10 |
| T6 | S1 | `design/system.md` + `design/system.html` + `design/web-redesign/` | deade10 |
| T7 | S4 | compilación del firmnware con `arduino-cli` y el FQBN de INSTALL.md §4 | pending (sin placa) |

Evidencia visual (Chrome headless sobre el mock, 390 y 1084 px): landing, login
y admin renderizan sin desborde horizontal; el panel entra en una pestaña a la
vez y la tira scrollea sola en el teléfono. Ventana de trabajo: 390 px reales
via iframe (la ventana de Chrome en Windows no baja de ~500 px y eso disfraza
de bug lo que sólo era recorte de captura).

## Entrega

- Tramos de revisión, en este orden: (1) `styles.css` (sistema de diseño),
  (2) `admin.html` + `app.js` (pestañas), (3) `index.html` + `landing.js` (sin
  ojos), (4) `design/` (direcciones y sistema).
- Corte de commits: `deade10` (código, assets y diseño) y el commit de
  documentación que contiene este archivo (no se cita su propio hash: sería
  obsoleto al amendarlo). Todo vive en la rama `feat/web-redesign`; publicar, PR
  y merge quedan a decisión del dueño.
- `esp32_gadget/web_assets.h` es generado (`pack_web.py`): sus ~12.500 líneas de
  array C no son superficie de review. El repo ya lo commitea dentro de los
  commits de feature (ver `d2ee961`), así que se mantiene la convención. Sin
  contar ese archivo, el cambio autorado del corte es ~1.300 líneas netas, y es
  un rediseño completo: no se puede partir más sin dejar la hoja de estilos en
  un estado intermedio que no compila visualmente.

## Log

L1 (verbatim, del mensaje del dueño sobre los tres pedidos juntos): "1.- Quiero
rediseñar la UI de toda la web, sacaremos los ojos, y mejoraremos el admin
separando inteligentemente las secciones en cards tipo pestañas, nuestra
referencia de diseño: '/mnt/c/Users/Máximo Majorel/Downloads/DESIGN.md'"

L2 (verbatim, decisión): "Fuera de toda la web" — la cara/ojos reproducidos y
cualquier preview de cara en el panel se eliminan; la cara vive solo en el
vidrio del equipo.

L3: Decisiones. (a) El repo real está en el disco de Windows
(`/mnt/c/Users/Máximo Majorel/workspace/esp32`); el cwd de la sesión está vacío
y no se usa. (b) La herramienta `gentle_design` resuelve su raíz contra el cwd
de la sesión, así que reporta "no design sources": los artefactos de diseño se
escriben a mano en `design/` siguiendo el skill, sin canvas. (c) Las webfonts
de la referencia (Lateral, Aeonik Pro) son comerciales y además inútiles en un
AP sin internet: se usan stacks del sistema, documentado en S1b. (d) Las cintas
3D se reemplazan por stickers SVG planos con borde negro, porque la referencia
prohíbe gradientes y el equipo no tiene render 3D.

L4: Dirección visual elegida entre los artboards de `design/web-redesign/`:
`01-cartel.html` (cartel de stickers, tipografía display gigante y las pantallas
como stickers), por ser la más fiel a la referencia y la más escaneable en un
teléfono. El resto queda como alternativas.

L5: Evidencia y correcciones del QA visual. (a) La red de Chrome en Windows no
baja de ~500 px de ancho, así que las capturas a 390 estaban mostrando 390 de un
layout de 500: el "desborde" del login era un artefacto. Las capturas de verdad
a 390 se hacen con un `iframe` de 390 px. (b) Con eso, login y landing estaban
bien y no hubo que tocar el layout del login (un cambio de grid a flex se hizo y
se revirtió al comprobarlo). (c) Correcciones reales que quedaron: el topbar
envuelve cuando hay chip de sesión, las grillas usan `minmax(0,1fr)` para que una
palabra larga no ensanche el teléfono, y el footer del panel (que vive fuera de
`<main>`) tenía el texto pegado al borde. (d) El mock devolvía seis nombres de
pantalla y el séptimo tile mostraba "—": se agregó `CONFIG` al mock, no era un
bug de CSS.

L6: Espejo del panel real para iterar por sección. `design/admin-espejo/` tiene
diez planchas, una por pestaña de `admin.html` (aplicaciones, pantalla, luz,
juegos, música, integraciones, red, Sofía, sistema, cuenta), con la hoja de
estilos del panel inlinada y los valores sin tocar: es una copia, no una
dirección nueva. `_src/` guarda el CSS recortado, la plantilla, el sprite y una
parte por sección; `_src/build.py` las ensambla. Sirve de base para pedir
cambios sección por sección; no hay plancha elegida todavía, así que las diez
son el estado actual.

L7: Primeros comentarios del canvas sobre `design/admin-espejo/` (16, todos
aplicados). Globales a las diez planchas: el proyecto se llama ESP BUDDY, se fue
la línea de firmware del top bar, el eyebrow de cada sección, el título grande
repetido de cada pestaña y la segunda línea del footer. Por sección: Aplicaciones
reordena arrastrando el grip (sin flechas, el plegado abierto); Pantalla quedó en
tres desplegables (Aspecto, Reloj, Apagado automático) con el interruptor a la
derecha; Juegos también con el interruptor a la derecha; Música quedó sólo con el
estado en vivo; Integraciones y Red pasaron a sub-pestañas; Sofía perdió las
tarjetas de estado y el bridge se reporta en un pill a la derecha del encabezado.

L8: Segundo y tercer lote de comentarios sobre `design/admin-espejo/`. (a) El
logo dejó el rombo: el sticker amarillo lleva los dos ojos de Sofía en negro,
también en tema oscuro. (b) La navegación pasó de tira horizontal a aside de
188 px a la izquierda, con los botones del ancho de su propio texto; debajo de
760 px vuelve a tira con scroll. (c) Se agregó un interruptor de tema en el
header: `[data-theme="dark"]` cambia paper, sky, mist, gray e ink, los stickers
conservan su color y todo lo que se pinta sobre un sticker fuerza texto negro
para no perder contraste; el color-scheme nativo acompaña a los controles.

L9: Cuarto lote de comentarios sobre `design/admin-espejo/` (8). Nav: la columna
del aside mide `max-content`, así todos los pills tienen el mismo ancho, el de la
palabra más larga (INTEGRACIONES). Aplicaciones pierde el botón de nombres y orden
originales. Pantalla pasa a cinco desplegables verticales (brillo, color,
rotación, reloj, apagado) y el color de interfaz usa el nuevo campo con su hex.
Luz RGB pierde los atajos de paleta y el color queda con etiqueta y hex al lado.
Música suma portada (SVG plano de la paleta, sin imágenes) y álbum, con más aire.
Integraciones estrena el campo `.field-input` (caja propia, valor en mono,
contador de caracteres) en el Client ID de Spotify. Sofía: el botón dice
"Restablecer color".

L10: Sofía ya no tiene el formulario "Pedirle algo a Sofía". En su lugar hay un
selector de estados simulados (chips `.chip`, uno activo con relleno de tinta:
en calma, aburrida, pensando, trabajando, esperando, listo, se viene algo, error)
más "Volver al estado real" para soltar la simulación.

L11: Espejo del inicio. `design/inicio-espejo/` replica la entrada pública en
tres planchas: `01-inicio` (página completa a 390 px), `02-menu` (el menú a
pantalla completa) y `03-juego` (el ta-te-ti con una partida a medias). Usa la
parte de landing de `styles.css`, con los valores sin tocar, y el sprite real;
`_src/` guarda la hoja recortada, la plantilla, las tres partes y el mismo
esquema de build que el espejo del admin. Quedan fuera el admin y el login.
Antes de diseñarlo se sincronizó `design/system.md` y `design/system.html` con
`styles.css` (tema oscuro, aside, campos, sub-pestañas, chips y filas
arrastrables), que estaban viejos desde la implementación del panel.

L12: Primer lote de comentarios del canvas sobre `design/inicio-espejo/` (18).
Inicio: fuera la marquesina, el pill de conexión, el kicker y el footer; el
display dice ESP-BUDDY; la sección pasa a llamarse Pomodoro y el botón de
arrancar usa el `.btn` del sistema (faltaba en la hoja recortada). Menú: fuera el
subtítulo, los contadores, el hint "Juegos primero", el acceso a Música y los
`<small>`; "Ajustes" pasa a "Administración". Juego: fuera el mensaje y el botón
de reinicio (la partida se resetea sola), el título va en mayúsculas y el tablero
deja de encogerse con `scrollbar-gutter: stable`. Además se sumó `04-login.html`
al diseño, con la hoja de login recortada de `styles.css`.

L13: Implementación del espejo del inicio y del login (planchas
`design/inicio-espejo/`, que ya llevan los dos lotes de comentarios). Entrada:
fuera marquesina, pill de conexión, kicker, tagline y footer; el display dice
ESP-BUDDY; Pomodoro sin estado y con el texto corto; menú sin subtítulo,
contadores, hint, Música ni `<small>`, con Juegos y Administración en la misma
fila. Juego: fuera el mensaje y el botón de reinicio (la partida se resetea sola
a los 2,2 s), título en mayúsculas y `scrollbar-gutter: stable` para que los
casilleros no cambien de tamaño. Login: ojos de Sofía, ESP BUDDY, "Administrar
Buddy", sin el párrafo del `.env`, link del sistema y `input:not([type])` para el
campo de usuario que se servía sin `type`. Escritorio: hoja de 960 a partir de
980 px, las siete pantallas en una fila, tarjetas más aireadas; desde 620 px el
dock deja de scrollear y pasa a grilla de cuatro columnas.
Slice: la implementación entra en un solo commit de código (`1ad3646`) porque la
entrada, el login y el plan de escritorio comparten la misma hoja y los mismos
componentes; las ~330 líneas de autoría se reparten entre `index.html`,
`login.html`, `landing.js` y `styles.css`. El diseño va en su commit aparte
(`abcad88`), como en el panel.

L14: Flasheado y verificado en la placa (COM5). Compilación exit 0 con el FQBN
fijo (sketch 1570734 bytes, 49%; RAM 22%), subida con `Hash of data verified`.
El equipo arranca, se conecta a VICTORWIFI en 192.168.100.75 y sirve lo nuevo:
`/` trae ESP-BUDDY, Pomodoro y "Sofía se concentra con vos." sin rastro de
marquesina, pill, tagline, footer ni botón de reinicio; `/login` trae los dos
ojos, ESP BUDDY, "Administrar Buddy" y el link como pill; `/admin` con sesión
trae `#themeBtn`, diez `nav-item`, `#bridgePill`, siete chips de estado y dos
tiras de sub-pestañas, sin `#firmwareLine`, eyebrows, `#orderReset`, `#swatches`
ni `#buddyAskForm`. `dump 4` dibuja la cara de Sofía en el vidrio. Secuencia
reproducible en `tools/probes/web_panel_after_flash.txt`.

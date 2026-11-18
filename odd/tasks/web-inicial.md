# Web inicial: selector de pantallas, menú de apps y ta-te-ti

La entrada pública `/` deja de ser un cartel: muestra la cara de Sofía, permite
elegir qué se ve en el equipo sin sesión y abre los juegos de la web, con el
ta-te-ti 1v1 contra el buddy como primero.

Diseño de referencia: `design/web-inicial/02-cara-primero.html` (elegido).
Los otros dos artboards del mismo diseño son alternativas.

## Specs

S1. El selector de pantallas vive fuera del login:

> "fuera del login que esté el selector de pantallas. pero esto como un boton de
> menu que se despliega y muestra en una grilla las pantallas posibles"

> "Aca se abre en pantalla completa las pantallas a elegir del ESP"

Consecuencia: `POST /api/screen` pasa a ser el único verbo de escritura público
bajo `/api/`. Solo cambia qué muestra el panel; no toca credenciales, redes ni
almacenamiento. El resto de las escrituras sigue pidiendo sesión.

S2. El menú principal lista las aplicaciones de la web, empezando por juegos:

> "el menu principal tendrá otras aplicaciones de la web, iniciando por juegos."

S3. Primer juego: ta-te-ti 1v1 contra Sofía, con tres dificultades. La superficie
del juego es minimalista, sin cara de fondo; los ojos que reaccionan son los del
equipo:

> "Haremos un ta te ti, con tres dificultades, los ojos deben reaccionar a las
> cosas que pasen en el juego ya que es un 1v1 con el buddy."

> "Mmm el ta te ti no hace falta que tenga el fondo los ojos. Los ojos solo
> reaccionan en el ESP digamos. el ta te ti es mas minimalista"

> "cuando hago clic en las celdas del ta te ti, cambia de tamaño y comprime las
> otras, tienen que ser cuadrados homogeneos redondeados todos."

Las tres dificultades son Fácil, Normal y Difícil, y solo cambia la calidad de
la jugada del buddy, no el tablero. Las nueve casillas son cuadrados iguales:
filas y columnas `1fr` con mínimo cero, para que una marca nunca redimensione la
grilla.

S4. Los juegos de la web no son los juegos del equipo:

> "Tene en cuenta que no es lo mismo  los juegos de la web que los del
> dispositivo por lo que esto no va"

Meteoritos y Tragamonedas no se ofrecen como juegos web: son pantallas del
equipo y se eligen desde la grilla. La sección Juegos de la landing solo lista
ta-te-ti.

S5. La cara del espejo reproduce los gestos que dibuja el panel:

> "Esto estaria bueo si puede replicar y gestos que veo en la pantalla del ESP"

El navegador replica mirada, parpadeo, respiración y las poses (idle, poke,
piensa, alerta, enojo, sueño, error). No es un espejo del estado real del
firmware: eso requiere que el equipo publique su pose, que no existe hoy.

S6. Dirección visual elegida: "Cara primero".

> La cara ocupa el escenario y las pantallas se deslizan siempre visibles, así
> que cambiar lo que se ve no cuesta ningún menú; a cambio, la landing es más
> larga y el menú completo queda escondido detrás del botón.

Incluye: sin bloque de marca sobre la cara, sin eyebrow "Ahora en la pantalla",
sin sección "También en la web" ni aclaraciones de sesión.

S7. El equipo reacciona a la partida (pedido posterior):

> "Ok asi esta bien el diseño. Podemos hacer que el ESP reaccione?"

La web no describe una pose: nombra un estado de ánimo de un vocabulario cerrado
(`focus`, `thinking`, `alert`, `happy`, `anger`, `sleepy`, `sad`) y el firmware lo
aplica como overlay temporal sobre la pose del bridge. Reglas:

- siempre expira (`400..8000 ms`, por defecto 2500), así una pestaña cerrada no
  deja la cara congelada;
- cada ánimo limpia los canales que no usa, para que la expresión anterior no se
  filtre;
- el overlay gana mientras dura y la cara vuelve sola al bridge al vencer;
- al abrir el juego el equipo pasa a la pantalla SOFIA y al cerrarlo vuelve a la
  pantalla que el dueño tenía.

S8. Las casillas del ta-te-ti son cuadrados iguales, y una marca no redimensiona
la grilla:

> "cuando hago clic en las celdas del ta te ti, cambia de tamaño y comprime las
> otras, tienen que ser cuadrados homogeneos redondeados todos."

## Tasks

- T1 · S1 · Regla pública para `POST /api/screen` con su caso en el test de rutas · 5be0e0d
- T2 · S1, S6 · Landing nueva en `index.html` + estilos en `styles.css` · f19a36f
- T3 · S2, S1 · Lanzador a pantalla completa con apps y grilla de pantallas leída de `/api/status` · f19a36f
- T4 · S3, S5 · Ta-te-ti con tres dificultades y cara reactiva (mirada, párpados, poses) · f19a36f
- T5 · S1-S6 · Empaquetado, documentación y verificación (tests, build) · f19a36f
- T6 · S3, S5, S8 · Juego minimalista y celdas homogéneas · 710d70f
- T7 · S7 · El equipo reacciona: `POST /api/face` público, `buddy_game.h` con su
  test, overlay en `screen_buddy.cpp`, y la web nombrando ánimos · 17c9285 + este commit

Corte de revisión: la entrega web va en un solo commit porque el artefacto
generado (`web_assets.h`, ~4300 líneas de diff) domina el recuento; el código
propio ronda las 1400 líneas (HTML, CSS y JS). Separarlo dejaría commits cuya
página no carga o cuyos assets no coinciden con `verify_assets`.

## Log

L1 (pedido original): "Quiero que modifiquemos la web. fuera del login que esté el
selector de pantallas. pero esto como un boton de menu que se despliega y muestra
en una grilla las pantallas posibles. el menu principal tendrá otras aplicaciones
de la web, iniciando por juegos. Haremos un ta te ti, con tres dificultades, los
ojos deben reaccionar a las cosas que pasen en el juego ya que es un 1v1 con el
buddy."

L2: "Quiero que modifiquemos la web" queda acotado a la entrada pública `/`; el
panel `/admin` no cambia de estructura en esta entrega.

L3: Los juegos del equipo no son juegos de la web. Se quitaron Meteoritos y
Tragamonedas de la oferta web y se dejaron como pantallas.

L4: Se quitó de la landing lo que no aportaba: bloque de marca sobre la cara,
eyebrow "Ahora en la pantalla", sección "También en la web" y las aclaraciones
de sesión y de juegos del equipo.

L5: Artboard elegido `design/web-inicial/02-cara-primero.html`. El menú abre a
pantalla completa con las pantallas en grilla.

L6: La cara del navegador replica gestos del panel, pero no lee su estado real:
el firmware todavía no publica la pose. Queda anotado como límite explícito.

L7 (evidencia de T1-T5):
- `python tools/run_tests.py` → 27 suites en verde, `web_assets.h` sin problemas.
- `admin_auth_test`: 113 checks, 0 fallas; el rojo previo fue exactamente
  "POST /api/screen: got protected want public".
- Firmware compilado con el FQBN real: exit 0, 47% de flash, 1499714 bytes.
- Sin placa: el panel se manejó con `tools/mock_gadget_api.py` y Chromium
  headless por CDP. Sin errores de consola. `scrollWidth == clientWidth == 430`.
  El menú abre a pantalla completa (430), la grilla lista las pantallas,
  `POST /api/screen` cambia el equipo y el carrusel sigue el cambio.
- Ta-te-ti: en Difícil, 6 partidas autojugadas contra movimientos aleatorios,
  6 victorias del buddy y 0 derrotas; el récord sobrevive al recargar.
- El mock quedó alineado con la regla de acceso: `/api/screen` público,
  `/api/settings` y `/api/screens/order` siguen respondiendo 401.

L8 (límites declarados): la cara es una réplica de gestos, no un espejo de la
pose del firmware; el pill dice "En línea" (conexión) y no "En vivo". El mock
todavía lista 6 pantallas (le falta CONFIG), desvío previo a esta entrega.

L9 (pedido del equipo reaccionando): "Ok asi esta bien el diseño. Podemos hacer que el ESP reaccione?"

L10 (evidencia de T7):
- `tools/buddy_game_test.cpp`: 44 checks, 0 fallas (nombres, vencimiento con
  wraparound de `millis()`, y qué canales limpia cada ánimo).
- `admin_auth_test`: 116 checks, 0 fallas; el rojo previo fue exactamente
  "POST /api/face: got protected want public".
- Contra el mock, manejado por CDP: abrir el juego lleva el equipo a la pantalla
  SOFIA (5) y manda `focus`; una jugada manda `thinking`; la respuesta manda
  `alert`; ganar manda `anger` (6000 ms); cerrar limpia el ánimo y devuelve la
  pantalla que el dueño tenía (0).
- Firmware compilado con el FQBN real.

L11 (evidencia en la placa, 2026-11-18):
- Flasheado en COM5 con el FQBN real: `Hash of data verified`.
- `tools/probes/buddy_game_face.txt` sobre la placa: los cinco ánimos llegan al
  renderizador. `buddydraw` después de `focus` da `lid=0.18`; después de `anger`,
  `mood=angry grow=1.12 hot=1.00 push=17` con ojos en ±8° (el `dump` los muestra
  inclinados); después de `sleepy`, `lid=1.00` y `ry=4`; al limpiar vuelve a la
  pose del bridge. `thinking` se aplica (lid base 0, la asimetría vive en
  lidL/lidR, que `buddydraw` no imprime).
- HTTP real sin cookie: `POST /api/face` → `{"ok":true}`, `POST /api/screen` → 200,
  `POST /api/settings` → 401.
- Página servida por la placa en `http://192.168.100.75/`: 7 pantallas en el orden
  guardado, abre el juego, cambia el equipo a SOFIA (5) y la partida corre sin
  errores de consola.
- Hallazgo del día: con el bridge configurado pero caído, la placa muestra la
  placa de estado en vez de los ojos, así que el ánimo no se veía. El overlay
  ahora aparta ese cartel mientras dura el juego; el cartel vuelve al terminar.

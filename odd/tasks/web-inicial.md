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

S3. Primer juego: ta-te-ti 1v1 contra Sofía, con tres dificultades, y la cara
reacciona a lo que pasa en la partida:

> "Haremos un ta te ti, con tres dificultades, los ojos deben reaccionar a las
> cosas que pasen en el juego ya que es un 1v1 con el buddy."

Las tres dificultades son Fácil, Normal y Difícil, y solo cambia la calidad de
la jugada del buddy, no el tablero.

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

## Tasks

- T1 · S1 · Regla pública para `POST /api/screen` con su caso en el test de rutas · commit 1
- T2 · S1, S6 · Landing nueva en `index.html` + estilos en `styles.css` · commit 2
- T3 · S2, S1 · Lanzador a pantalla completa con apps y grilla de pantallas leída de `/api/status` · commit 3
- T4 · S3, S5 · Ta-te-ti con tres dificultades y cara reactiva (mirada, párpados, poses) · commit 4
- T5 · S1-S6 · Empaquetado, documentación y verificación (tests, build) · commit 5

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

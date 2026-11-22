# Temporizador partido: la cara a la izquierda, el número a la derecha

El layout que el dueño aprobó para el evento del calendario pasa a ser un
componente reutilizable del vidrio. El temporizador de Concentración lo reusa:
los ojos reales de Sofía ocupan la mitad izquierda y la cuenta regresiva la
derecha. La cara no se reimplementa: sale del mismo renderer de siempre, con
giroscopio y bridge incluidos.

## Specs

S1. Layout reutilizable — verbatim: "Me gusta mucho como quedo la UI en el LCD
cuando hay un evento del calendario, me gustaria hacer lo mismo para el
temporizador, los ojos de un lado y el temporizador a la derecha, entonces bueno
transformamos ese layout a un componente reutilizable." El vidrio partido del
evento (ojos a la izquierda, columna de datos a la derecha, regla vertical al
medio) se extrae a un componente que dibujan tanto el evento como el
temporizador. Una sola definición de la geometría, no dos copias.

S2. Los ojos son los de siempre — verbatim: "los ojos que quedan a la izquierda
tienen que seguir siendo los mismos que estan siempre en pantalla completa no?
Sensible al giroscopio, a los eventos con el bridge. No tiene que ser unos ojos
nuevos similares me explico?" La mitad izquierda usa el renderer real de la cara
(párpados, `grow`, `turn`, perspectiva, rotación, hot/dim, blink) y recibe el
mismo pose que ya sale del IMU y del bridge. Prohibido un dibujo paralelo de
ojos "parecidos".

S3. El temporizador a la derecha — verbatim: "el temporizador a la derecha".
Una sesión activa muestra el layout partido en vez del layout viejo (ojos chicos
arriba, dígitos abajo a todo el ancho). Al terminar, el cartel LISTO aparece en
la misma columna derecha durante su ventana y después la cara vuelve a pantalla
completa.

S4. Tipografía y contenido — decisión del dueño en sesión: los números siguen
siendo de siete segmentos, reescalados al ancho del panel derecho; el panel
lleva etiqueta, número y LISTO, sin barra de progreso.

## Tasks

T1. `esp32_gadget/buddy_split.h` puro: geometría del componente (divisor,
paneles, marco de la cara partida, corrimiento del par) y prueba de host.

T2. `screen_buddy.cpp`: extraer el par de ojos de `drawFace` a `drawEyes`, el
marco partido a `drawSplitFrame` con painter, migrar el evento del calendario y
pintar la columna del temporizador; `buddy_focus.h` pasa a la geometría de su
columna. Son un solo commit porque el renderer extraído y su primer segundo
consumidor no se pueden compilar por separado: `d340191`
(`refactor(screen): share the split glass between calendar and timer`).

T3. Pruebas de host: `tools/buddy_split_test.cpp` nuevo y
`tools/buddy_focus_test.cpp` actualizado; `python3 tools/run_tests.py` y
`arduino-cli compile` con el FQBN de `INSTALL.md` §3. La evidencia se registra
en este documento, sin flashear: el dueño autoriza el flash aparte.

## Log

L1 (verbatim, del mensaje del dueño): "Me gusta mucho como quedo la UI en el LCD
cuando hay un evento del calendario, me gustaria hacer lo mismo para el
temporizador, los ojos de un lado y el temporizador a la derecha, entonces bueno
transformamos ese layout a un componente reutilizable. Tambien quiero cambiar la
tipografia de los numeros, y los ojos que quedan a la izquierda tienen que
seguir siendo los mismos que estan siempre en pantalla completa no? Sensible al
giroscopio, a los eventos con el bridge. No tiene que ser unos ojos nuevos
similares me explico?"

L2 (decisiones del dueño, preguntadas antes de escribir): (a) tipografía de los
números: siete segmentos reescalados al panel, no una fuente nueva ni la 5x7;
(b) contenido del panel derecho: etiqueta + número + LISTO, sin barra de
progreso.

L3 (decisión de ingeniería): el renderer de ojos se extrae a una función que
recibe el marco (centros, alto y escala) para que pantalla completa y panel
izquierdo dibujen el mismo código con distinto tamaño. La mirada de estudio
hacia el número se conserva tal cual (baja la mirada); adaptarla a la derecha
sería otra spec, no esta. El HUD de la esquina se suprime mientras el vidrio
está partido, igual que hace el evento: la columna derecha es del contenido y la
regla ya separa las dos zonas.

L4 (evidencia, `d340191`): `python3 tools/run_tests.py` → 38 pasaron, incluidos
`buddy_split_test` (pane, marco, seis extremos reales en x e y, corrimiento del
par) y `buddy_focus_test` (columna: etiqueta, dígitos, LISTO). Compilación
`arduino-cli` con el FQBN de `INSTALL.md` §3, exit 0, sin warnings: sketch 49%,
RAM global 22%. La preservación de la cara a escala 1 se verificó inline contra
`git show HEAD:esp32_gadget/screen_buddy.cpp`: mismas fórmulas y redondeos en
bob, gazeY, baseRx/baseRy, eyeX, eyeDeg, extents, push, color y párpados; el
literal `4` del blink pasó a `baseRy`, que con `shut` sigue siendo 4. El
verificador independiente de `gentle-ai-verify` no fue admitido por el
mecanismo; queda reportado como no disponible, no reintentado.

L5 (frontera de slice): la rama `feat/temporizador-partido` nace en `79d8929`
(punta de `refactor/architecture-dedup`, 9 commits previos ajenos a esta
feature). El slice de esta feature es el único commit `d340191`; para revisión,
la PR se encadena con base `refactor/architecture-dedup`, no con `main`. No se
flasheó ni se abrió PR: son decisiones del dueño.

L6 (flash y prueba en la placa, autorizados por el dueño): `arduino-cli upload`
a COM5 con el binario de `.build-split`, hash verificado. Probe nuevo
`tools/probes/temporizador_partido.txt`; resultados: con la sesión activa
`buddydraw` reporta `eye0 x=43 rx=24 ry=24` / `eye1 x=113 rx=24` (marco partido,
media escala) y `dump 4` muestra la cara a la izquierda y, en la columna derecha,
`CONCENTRACION` con cuatro dígitos de siete segmentos y dos puntos. Tras
`{"seconds":0}`, `buddydraw` vuelve a `eye0 x=112 rx=49` / `eye1 x=229 rx=46`
(pantalla completa). `tools/probes/calendario_local.txt` sigue pasando: el
evento usa el mismo componente y la ficha derecha se dibuja igual.

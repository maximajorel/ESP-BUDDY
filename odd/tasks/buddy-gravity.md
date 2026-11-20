# La pelotita con la gravedad de verdad

Tercer pedido del dueño. La pelota roja que aparece con dos toques dejó de ser un
"feel" con números mágicos: ahora la física está en unidades del Sistema
Internacional, con la gravedad normal de la Tierra, la escala real del vidrio y un
factor de cámara lenta explícito para que se pueda mirar.

## Specs

S1. Gravedad real — verbatim: "3.- LA pelotita de sofia al tocar dos veces sigue
siendo medio liviana, quiero que respete mejor la gravedad del planeta tierra,
buscá informacion asi hacemos una gravedad precisa". La aceleración por defecto es
la gravedad normal **g0 = 9,80665 m/s²**, el valor establecido por la **3.ª CGPM
(1901, CR 70)**, resolución 2: "980,665 cm/s²"
(`bipm.org/en/committees/cg/cgpm/3-1901/resolution-2`, citado por ISO 80000 y por
NIST/CODATA, que publica "standard acceleration of gravity = 9.806…"). La
gravedad aparente en la superficie varia ±0,5 % entre el ecuador y los polos
(9,780 a 9,832 m/s²); el modelo usa el valor convencional, que es el que
corresponde a "la gravedad del planeta" como constante.

S2. Escala medida, no estimada — verbatim, la medición manda sobre el número del
pedido: el objetivo decía "≈9,88 px/mm"; medido con la geometría del panel da
**9,73 px/mm**. Panel de 172×320 px y 1,47" de diagonal activa (Waveshare
ESP32-S3-LCD-1.47B, "1.47inch LCD display, 172×320"): con aspecto 172:320, la
altura activa es `1,47" / sqrt(1 + (172/320)²) = 32,89 mm` y 320/0,03289 = **9728
px/m**. De ahí: `BUDDY_BALL_ACCEL = 9,80665 × 9728 = 95.401 px/s²` por g de
inclinación. Antes eran 520 px/s²/g: **184 veces menos**.

S3. Cámara lenta explícita — verbatim: la decisión del dueño, "Física real +
cámara lenta". Con la escala real, una caída libre de 32,9 mm dura **82 ms**: es
lo que haría una pelota de verdad y no se puede jugar. La simulación corre con
`BUDDY_BALL_SLOWMO = 0,15` segundos simulados por segundo de pared, aplicado al
**reloj** y no a la aceleración, así la trayectoria es exactamente la física,
reproducida lenta. Todo lo derivado de la gravedad conserva su relación física;
sólo cambia la velocidad de reproducción, y es un único número documentado.

S4. Proyección del vector gravedad del IMU — verbatim: "proyección del vector
gravedad del IMU sobre el plano del vidrio". La IMU entrega g en un vector de 3
ejes (`imu.h`, ±4 g); el llamador ya proyecta sobre dos ejes calibrados
(`screen_buddy.cpp`, `ballInputs`) y separa inclinación (IIR lento) de sacudida.
El modelo nuevo suma **ambas como aceleraciones**: una sacudida es lo mismo que
una inclinación, sólo que rápida. No queda ningún "gain" sin unidad.

S5. Materiales reales. Restitución **0,90** (goma sobre vidrio, banda real 0,80–
0,90; el 0,92 anterior era un superbote). Sin arrastre: a 0,8 m/s el arrastre de
una bolita de 2,7 mm es <5 % del peso, así que el modelo es gravedad +
restitución y nada más. El tope de velocidad deja de ser un parámetro de juego y
pasa a ser un riel de seguridad por encima de la caída de altura completa
(√(2·g·h) = 5730 px/s).

S6. Integración honesta. Verlet de velocidad, que es **exacta** con aceleración
constante (por eso el test compara contra `y = ½·g·t²` y no contra una
tolerancia gruesa), con subpasos de 1 ms simulado para que nada atraviese una
pared: a la velocidad máxima un subpaso mueve 6 px, menos que el radio de 13 px.

S7. Nada de números mágicos. Se fueron `BUDDY_BALL_GRAVITY_GAIN`,
`BUDDY_BALL_SHAKE_GAIN` y `BUDDY_BALL_FRICTION`. Cada constante que queda tiene
unidad y una línea que la justifica o una fuente.

S8. Tests — `tools/buddy_ball_test.cpp` fija: las constantes con sus unidades, la
caída libre contra `½·g·t²` (0,5 % de tolerancia), que un plano nivelado ni
empuja ni arrastra, que una sacudida es aceleración, la restitución real en la
pared, que no hay túnel ni con 50× el tope, el reposo de una pelota lenta, el
campo, la sacudida de 3 g durante diez segundos y el mapeo de la mirada.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S1,S2,S3,S5 | evidencia: g0 citado, escala del panel medida, restitución y arrastre justificados | (este corte) |
| T2 | S3,S4,S5,S6,S7 | `buddy_ball.h` reescrito en SI con Verlet y subpasos | (este corte) |
| T3 | S8 | `tools/buddy_ball_test.cpp` reescrito contra la física | (este corte) |
| T4 | todas | suite host + compilación del firmware | (este corte) |

## Entrega

- Tramos de revisión: (1) `buddy_ball.h` + `tools/buddy_ball_test.cpp`, el
  modelo SI completo; (2) este documento con la evidencia y las fuentes.
- No toca `web_assets.h` ni la web: es firmware puro y una prueba de host.
- Rama `feat/concentracion`; publicar, PR y merge son decisión del dueño.

## Log

L1 (verbatim): "3.- LA pelotita de sofia al tocar dos veces sigue siendo medio
liviana, quiero que respete mejor la gravedad del planeta tierra, buscá
informacion asi hacemos una gravedad precisa"

L2 (decisión del dueño en la consulta): "Física real + cámara lenta" — unidades
SI reales y un factor de tiempo aparte, por encima de la fidelidad total (60 ms)
y de una escala ficticia de mesa grande.

L3: Medición que corrige el número del pedido. El objetivo decía 9,88 px/mm; la
geometría del panel (1,47" de diagonal activa, 172×320) da 9,73 px/mm = 9728
px/m. Se corrige el spec con la medición, como manda la política de iteración. Si
la diagonal de 1,47" fuera la del módulo y no la del área activa, el área sería
más chica y px/m más alto: el test sostiene la relación entre las constantes, y
la nota queda para cuando haya calibre o datasheet a mano.

L4: Riesgo asumido y medido. La pelota ahora cae mucho más rápido que antes
(1 g cruza los 172 px del alto en ~0,55 s de pared, contra ~1 s para cruzar el
ancho con el modelo viejo). Es el efecto buscado: "respetar mejor la gravedad".
Si al dueño le resulta demasiado, se toca `BUDDY_BALL_SLOWMO`, un solo número,
sin volver a inventar ganancias.

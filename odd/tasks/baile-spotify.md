# Baile de Sofía: baila cuando suena Spotify

Emoción nueva del pedido "nuevas emociones para SOFIA (buddy)", punto 1. Cuando el
dueño escucha música en Spotify, la cara baila: los ojos siguen el compás del
tema y salen notas musicales que suben y se desvanecen. Alcance: la capa pura de
pose/notas, el renderer de `screen_buddy.cpp`, el disparador de consola y el chip
del panel. No cambia el contrato con el bridge.

## Specs

S1 (pedido, literal): "Cuando esté escuchando yo musica en spotify, sofía bailara,
los ojos se moveran a ritmo y saldran notas musicales que se desvanecerán".

S2 (decisión del dueño, quién manda): "Baila si está al pedo (y la música la
despierta)" — contrato literal de la opción elegida: "idle + Spotify → baila /
thinking / tool / waiting / error → su gesto / dormida + Spotify → se despierta
bailando / pelotita encendida → la pelotita manda (no baila)".

S3 (decisión del dueño, el ritmo): "Tempo por tema + fase real del playback" —
contrato literal: "trackId A → 112 BPM, 4/4 faseada desde progressMs / pausa → se
congela el baile / nuevo tema → nuevo BPM, vuelve el beat". El BPM es estable por
`trackId` (84 a 136) y la fase sale de `progressMs` más el tiempo transcurrido
desde el último poll.

S4 (decisión del dueño, cómo se prueba): "Consola + panel" — "consola: dance 10 /
panel Sofía: [Bailando] ← chip nuevo".

S5 (reglas del vidrio que no se tocan): "No eyebrows. Ever." y "No mouth, no text,
no assets". Las notas son glifos procedurales, no imágenes; la cara sigue siendo
dos squircles con párpado negro y la única decoración permitida son las notas del
baile.

S6 (lo que ya anda no cambia): las reacciones nuevas son una capa que se suma a
la pose del estado y a las reacciones existentes (IMU, pelotita, calendario,
concentración, juego web, eventos de idle). Con el baile apagado, cada frame es
exactamente el que se dibujaba antes.

## Tasks

| ID | Qué | Specs | Ruta | Commit |
| --- | --- | --- | --- | --- |
| T1 | `buddy_dance.h` puro (BPM por tema, beat, pose, campo de notas) + `tools/buddy_dance_test.cpp` | S1, S3, S6 | inline | `d2483c7` |
| T2 | Render: notas dithered y pose de baile en `screen_buddy.cpp` | S1, S5 | inline | `cfee993` |
| T3 | Disparador: Spotify sonando + gate de estado, despertar por música, pin | S2 | inline | `cfee993` |
| T4 | Consola `dance`, `/api/face` con `dance` y chip "Bailando" del panel | S4 | inline | `147ec3b` |
| T5 | Doc del proyecto, compilación y evidencia en la placa | S1-S6 | inline | `206ac2c` |
| T6 | El corte del baile en cada poll + `why=` en `buddydraw` | S1, S3 | inline | `1c0b8d0` |
| T7 | El baile aprobado: salto ("bote parejo") en `buddy_dance.h` con blend posterior al easing | S1, S2, S5 | inline | `35f1c63` |
| T8 | Direcciones orgánicas D y E en el canvas, con selector de FPS | S1, S6 | inline | `077c8e3` |
| T9 | Presupuesto de cuadro: blit sin switch por píxel, snapshot cada 250 ms, redondeo sub-píxel e instrumentación | S6 | inline | `077c8e3` |

## Log

L1 (pedido original, literal): "Vamos con nuevas emociones para SOFIA (buddy)

1.- Cuando esté escuchando yo musica en spotify, sofía bailara, los ojos se
moveran a ritmo y saldran notas musicales que se desvanecerán"

L2 (decisiones del dueño en el cuestionario previo, literales): "Baila si está al
pedo (y la música la despierta)"; "Tempo por tema + fase real del playback";
"Consola + panel".

L3 (decisiones de implementación, con su razón):

- Baila sólo en `idle`. `done` ya tiene su propia emoción (el rebote de la
  victoria y la sonrisa de alivio) y las dos se pisaban; el pedido habla de
  "está al pedo", que es idle.
- Una pausa no congela el gesto: para el baile y mata las notas, y el beat queda
  donde estaba el progreso, así que al reanudar vuelve a caer en el tema. Un
  gesto congelado en el vidrio se lee como una cara trabada, no como una pausa.
- Las notas viven en las bandas laterales (x 12-30 y 291-308). El baile mueve el
  par de ojos, así que la banda se midió en la placa con el sway completo: el ojo
  izquierdo nunca arranca antes de x 42 y el derecho nunca termina después de 278,
  glow incluido. El sway se recortó a 7 px justamente para dejar esas dos columnas
  libres: el compás ya se lee en los párpados, el roll y el rebote.
- `web_assets.h` es generado y por eso el diff de la rama cuenta miles de líneas:
  el cambio escrito a mano ronda las 700.
- Corte de slices: T1 (capa pura + test) y luego T2+T3 (render y disparador, un
  solo comportamiento) y T4 (consola, API y panel) en commits separados.

L4 (verificación host): `python3 tools/run_tests.py` en verde (39 tests, incluye
el nuevo `buddy_dance_test`), `tools/verify_assets.py` limpio y compilación del
sketch con el FQBN de INSTALL.md §3 sin errores (1.574.970 bytes de flash, 22% de
RAM).

L6 (reporte del dueño, literal): "estoy viendo el cambio y no parece tan 'feliz' al
escuchar la musica, quiero como que los ojos vayan saltanto, que sea bien
dinamico, si queres metele un gentle design y lo laburamos juntos. Ahora pasa
tambien que es como que baila, y deja de bailar, y vuelve a bailar, cuando la
musica no paro nunca".

L7 (el corte, causa encontrada): `spotify.cpp` pone el feed en `LOADING` al
arrancar **cada** poll (cada 3 s) y el gate leía el feed instantáneo, así que el
baile se apagaba y se prendía en cada vuelta. Arreglo: `buddyDanceLatchStep` en
`buddy_dance.h` cree sólo las respuestas con autoridad (un tema, o un 204 que
dice que no hay nada), y espera `BUDDY_DANCE_HOLD_MS` (20 s) antes de rendirse con
un dato viejo. El pin de consola no cambia. Además `buddydraw` ahora imprime
`why=` y el firmware loguea cada transición del baile con el motivo.

L8 (diseño para elegir): `design/baile-sofia/` con tres direcciones animadas del
vidrio, las tres sobre el mismo compás de 112 y con selector de tempo —
`01-bote-parejo.html` (salta el par entero en cada tiempo), `02-salto-cruzado.html`
(un ojo por tiempo, pide `bobL`/`bobR`) y `03-vuelo-por-compas.html` (un vuelo
grande por compás, sin canales nuevos). El documento de la feature enlaza este
diseño; la dirección elegida se implementa en una tarea nueva (T6).

L9 (handoff del diseño): el dueño eligió la dirección A, "Bote parejo", de
`design/baile-sofia/01-bote-parejo.html`. Implementación en T7: el salto es una
curva de vuelo por tiempo (10 px, con dos saltos chicos en el cuarto tiempo),
estiramiento en el aire (`ry` 48→54), aplaste al caer (44), alegría en el párpado
inferior (`bottom` 0..0.20) y el párpado de arriba en 0 mientras vuela. Cambio
técnico que el artboard no mostraba: el salto se escribe **después** del filtro de
pose (`buddyPoseMix` con un blend de 160 ms), porque el easing de 100 ms aplanaba
un arco de 545 ms en un balanceo. `buddydraw` ahora imprime `bob` y `bottom` para
poder probarlo.

L10 (evidencia en la placa del salto): con `dance 20` y diez `buddydraw`
seguidos, la placa reporta `bob` entre 0 y −7.5 px, `bottom` hasta 0.15 y `lid` 0
en el aire contra 0.06–0.09 al aterrizar, con las notas vivas (1 a 3). `dance 0`
vuelve a `bob=0.0` y `why=nada sonando`.

L11 (pedido del dueño, literal): "necesito que levantes gentle design porque
siento que es poco organico, necesito que las animaciones de los ojos sean mas
'smooth' tambien quiero optimizar un poco para que vayamos bien de FPS".

L12 (diagnóstico de "poco orgánico"): no era el tamaño del salto sino la
repetición perfecta y el refresco. Tres causas concretas: (a) cada tiempo era
idéntico, sin acentos ni variación; (b) el aterrizaje era una banda con bordes
duros; (c) el redondeo de `bob` truncaba hacia cero, sesgando medio píxel abajo
en cada salto. Direcciones nuevas: `04-oleaje-organico.html` (acentos, pico
corrido por hash, aterrizaje en campana, aliento de fondo, roll no periódico) y
`05-respiracion-con-pulso.html` (flotación sin salto, ≤3 px por tiempo). Las dos
traen selector de FPS que cuantiza el reloj igual que el panel, para ver el
problema real.

L13 (FPS, sin medir todavía): el `switch` de orientación estaba **dentro** del
lazo de 55.040 píxeles de `displayPush`; ahora hay un lazo por orientación y el
mapeo vive en `display_blit.h`, pineado en el host por `display_blit_test.cpp`
contra la implementación vieja (las cuatro orientaciones, píxel por píxel). El
snapshot de Spotify pasó de 20 lecturas por segundo a una cada 250 ms (mutex +
~600 bytes de copia). Y `buddydraw` ahora imprime `frame draw=… push=…` con
promedio y peor caso, que es el número que decide si el cuadro puede bajar de
50 ms a 33 ms. Falta la placa enchufada para tener ese número.

L5 (evidencia en la placa, autorizada por el dueño en la sesión): firmware
flasheado por COM5 y probeado con `tools/gadget_probe.py`.

- `dance 16` sobre la pantalla SOFIA: `buddydraw` responde `dance on=1 bpm=112
  beat=13 phase=0.30 bar=3 notes=2 pin=1` y la pose cambia cuadro a cuadro
  (`lid` 0.07→0.13, `deg` -8.0→+5.5, los ojos se corren 104→86→102 px): el baile
  late y los ojos se mueven a ritmo.
- `dump 1` muestra una nota sólida en la banda derecha (cabeza 137-143, asta
  126-136, banderola 126-131) y otra más arriba ya dithered, casi desvanecida;
  las dos bandas quedan limpias de píxeles del ojo.
- `dance 0` apaga el baile: `dance on=0 ... notes=0` y la bandera desaparece del
  vidrio.
- `api POST /api/face {"mood":"dance","ms":5000}` responde `HTTP 200 {"ok":true}`
  y la cara baila: el chip del panel funciona contra el equipo real.
- `spotify status`: `auth listo`, `token presente`, `feed cargando http=204` —
  con nada sonando la cara no baila sola, que es exactamente S3. Queda pendiente
  del dueño la prueba con un tema real andando.

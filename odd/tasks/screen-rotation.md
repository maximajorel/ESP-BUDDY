# Auto-rotacion con el acelerometro, bloqueo y compensacion

El equipo ya tiene acelerometro (QMI8658) y el driver de la pantalla ya sabe
dibujar el lienzo horizontal en las dos orientaciones no espejadas (CW y CCW, 180
grados una de la otra). Lo que falta es decidir cual usar sin consola: que la
imagen siga como esta apoyado el equipo, con un bloqueo para congelarla, en la
pantalla CONFIG y en el panel web, y que el juego y la cara de Sofia sigan
sintiendo "izquierda" como lo que se ve.

Branch: `feat/screen-rotation` (chained from `feat/screen-settings`, HEAD 3c096e3).

## Specs

S1. Auto-rotacion por acelerometro — "Auto-rotación con acelerómetro + bloquear".
Con el equipo girado 180 grados en su propio plano, la imagen pasa de una
orientacion no espejada a la otra. Solo se eligen `ORIENT_CW` y `ORIENT_CCW`: una
imagen espejada nunca es un resultado posible de esta politica.

S2. Bloqueo — la fila `BLOQUEO` de la pantalla y el interruptor del panel congelan
la orientacion actual: mientras este bloqueado, el sensor no cambia nada.
Desbloquear devuelve el control al sensor sin saltar de imagen.

S3. Las dos superficies exponen lo mismo:
  - CONFIG en el equipo: fila `ROTACION` con `AUTOMATICA` / `HORIZONTAL` /
    `INVERTIDA` (un toque cambia el valor) y fila `BLOQUEO` con `NO` / `SI`.
  - Panel web, tarjeta "Aspecto": el mismo selector de tres valores y un
    interruptor "Bloquear rotación".

S4. Persistencia: el modo y el bloqueo sobreviven a un corte de luz, cada uno en su
propia clave NVS, porque el blob `cfg` se versiona por tamaño.

S5. Compensacion de lo que ya lee el acelerometro — "Compensar todo (juego y cara
de Sofia)". Con la imagen invertida, inclinar el equipo a la derecha sigue
llevando la nave a la derecha de lo que se ve, y la cara de Sofia sigue mirando
hacia el lado que baja.

S6. Honestidad con el equipo plano: acostado de espaldas, la gravedad no tiene
componente en el plano y el sensor no puede distinguir un giro de 180 grados. En
ese caso se mantiene la ultima orientacion; no se adivina.

S7. Un juego en curso nunca se interrumpe: jugando o calibrando, la orientacion no
cambia sola (el dueno esta inclinando el equipo a proposito).

S8. Compatibilidad: `orient <0-3>` por consola y `POST /api/settings
{"orientation":N}` siguen funcionando igual, y la orientacion guardada se respeta
al arrancar.

S9. Documentacion: INSTALL.md (tabla de comandos y seccion 9) y el README describen
el modo automatico, el bloqueo y su limite con el equipo plano.

## Tasks

| ID | Task | Specs | Route | Commit |
| --- | --- | --- | --- | --- |
| T1 | `rotation_policy.h` (puro: politica, espejado, ciclo de modos) + `tools/rotation_policy_test.cpp` primero | S1,S2,S5,S6 | inline, test-first | e1ed4a6 |
| T2 | Claves NVS `rotMode`/`rotLock` en `settings.*`; modulo `rotation.*` (aplicar + tick + hold); arranque y `loop()` en `.ino`; comandos `rot` y `orient` | S1,S2,S4,S8 | inline | 3d85843 |
| T3 | Compensacion: `rotationInPlaneSign()`/`rotationMirrorInPlane()` usados por `screen_game.cpp` (readTilt + hold) y `screen_buddy.cpp` | S5,S7 | inline | 4190bbd |
| T4 | Pantalla CONFIG: cuatro filas con las dos nuevas, sin romper las dos de brillo | S3 | inline | 65c9e38 |
| T5 | Panel web: `/api/status` y `POST /api/settings`, `index.html`, `app.js`, mock, `web_assets.h` repackado | S3,S4 | inline | 5f8f3e9 |
| T6 | Verificacion: tests g++ de todo `tools/`, `verify_assets.py`, compilacion `arduino-cli` con el FQBN de INSTALL.md | all | inline | — |

Limites de rebanada: cada commit es una unidad revisable por si sola; el
contador del hook de commits mide la rama encadenada completa contra `master`
(incluye los commits de `feat/screen-settings` y anteriores), no esta rebanada.
La rebanada de este trabajo son los cinco commits e1ed4a6..5f8f3e9.

## Log

L1 (verbatim, user):

```
Mi ESP tiene acelerometro? si es asi podemos agregar en la config tanto en la pantalla como en la web, para bloquear la rotacion de pantalla o invertir como esta la rotacion?
```

L2 (verbatim, decisiones del dueno):

```
1. ¿Cómo querés que se comporte la rotación en el equipo? — Auto-rotación con acelerómetro + bloquear
2. Si hay auto-rotación, ¿qué pasa con lo que ya lee el acelerómetro? — Compensar todo (juego y cara de Sofia)
```

Vista previa aprobada de la tabla de CONFIG:

```
CONFIG
> BRILLO LCD  [========  ] 90%
  BRILLO LED  [==        ] 25%
  ROTACION    [ Automatica ]
  BLOQUEO     [ No ]

TOCA: CAMBIAR   2 TOCS: FILA
```

L3 (evidencia): el QMI8658 ya esta inicializado (`imu.cpp`, `imuBegin()` en
`esp32_gadget.ino`) y en uso por `buddy_imu.h` y por el volante del juego
(`readTilt` en `screen_game.cpp`); `display.cpp` ya implementa las cuatro
rotaciones, de las cuales solo `ORIENT_CW` y `ORIENT_CCW` no espejan la imagen.
`orient` existia solo en la consola y en la web; la pantalla CONFIG no lo exponia.

L4 (evidencia): no hay placa conectada en esta sesion. La verificacion posible es
la compilacion con `arduino-cli` (Windows, FQBN de INSTALL.md), los tests de host
con g++ y `verify_assets.py`. El comportamiento real (gesto fisico, ruido del
sensor, migracion de NVS en un equipo ya usado) queda pendiente de hardware.

L5 (verificacion, host): los 17 `tools/*_test.cpp` compilan y pasan con g++
(`spotify_core_test` sigue necesitando ArduinoJson y no toca este cambio);
`rotation_policy_test` es nuevo y cubre el mapa aprendido, el equipo plano, el rol
de timon, el tiempo de asentamiento, el wobble, el espejado de ejes y el ciclo de
modos. `python3 tools/pack_web.py` + `verify_assets.py` dan 0 problemas.

L6 (verificacion, compilacion): `arduino-cli compile` con el FQBN de INSTALL.md
sale 0, 0 warnings, 1412086 bytes (44% de programa) y 54740 bytes de globales.

L7 (verificacion, navegador): el panel se probo en Chrome headless contra
`tools/mock_gadget_api.py` en 127.0.0.1:8799: el `<select id="rotationMode">`
trae los tres modos y arranca en `auto`; cambiarlo a `inverted` hace POST
`{"rotationMode":"inverted"}` y `/api/status` pasa a `orientation: 2`; el
interruptor manda `rotationLocked: true`; tras recargar, la pagina restaura los dos
valores desde `/api/status`.

L8 (hardware, autorizado por el dueno: "Si, flashea y verifica por consola"): subido a
**COM5** con `arduino-cli upload`, `Hash of data verified`, hard reset por RTS. La
secuencia `tools/probes/rotation.txt` (via `python tools\gadget_probe.py` con el
Python de Windows) mostro, en orden:

- `status` al arrancar: `orient 1 (auto)` — el modo automatico es el default y la
  imagen guardada se respeta.
- `rot`: `mode=auto (4) lock=off image=1 sign=1`.
- `rot lock on`; `rot` de nuevo: `lock=on`, y el bloqueo **sobrevivio** al reset
  del puerto (la segunda corrida lo encuentra en `on`).
- `orient 2`: `rot mode=inverted (2) lock=on image=2` — fijar una imagen apaga la
automatica.
- `rot auto`: vuelve a `mode=auto` sin saltar de imagen.
- `dump 4` en CONFIG: titulo, y las **cuatro filas** en sus bandas: la barra del
  LCD, la del LED, `ROTACION` (8 glifos) y `BLOQUEO` (7 glifos), con el pie debajo.
- `/api/status` en el equipo real: `display: {orientation, rotationMode,
  rotationLocked}` presentes y coherentes con lo que dijeron los comandos.

L9 (hardware, restaurado): la prueba dejo el equipo con la rotacion fijada, asi
que se devolvio al estado de arranque con `tools/probes/rotation_restore.txt`:
`rot lock off`, `orient 1`, `rot auto` -> `rot mode=auto (4) lock=off image=1` y
`/api/status` con `rotationMode: auto`, `rotationLocked: false`,
`orientation: 1`. El unico valor que toco la prueba fue el brillo del LED trasero
(un toque de mas en la fila `BRILLO LED`, ya estaba en 0%); el brillo del LCD no
lo cambio la prueba, lo movio el dueno desde el panel mientras corria.

L10 (pendiente): el giro fisico real de 180 grados y el umbral de 0.35 g no se
pudieron observar: el equipo estaba quieto sobre la mesa y nadie lo giro durante
la prueba. Esa es la parte que queda para una prueba manual del dueno.

L11 (cierre): el dueno confirmo el giro fisico en el equipo ("Sip, parece que
funciona") y pidio meter todo en master. `master` estaba en 29c64dc y `HEAD` lo
tenia como ancestro, asi que la integracion fue **fast-forward** a 8a82677: los 13
commits de la cadena (buddy squircle, screen-settings y esta feature), arbol
limpio, sin merge commit. No hay remoto configurado, asi que no se publico nada.

L12 (auditoria del dueno, post-merge): "la rotacion automatica no funciona bien, o
ni anda a veces". Tres defectos reales encontrados leyendo el codigo:

1. La politica miraba **un solo eje** (el largo) y exigia que dominara 1.5x sobre
   el corto. Sostenido de costado, que es como se lee la imagen 320x172 sobre el
   vidrio vertical, la gravedad cae sobre el eje **corto**: `magAlong <
   ROT_MIN_G` siempre y la imagen nunca giraba. Solo funcionaba en el agarre
   parado.
2. Cada muestra sin componente en el plano (el equipo pasa por "acostado"
   mientras lo giras, o se pierde una lectura del I2C) ponia `candidate = 0` y
   reiniciaba la ventana de asentamiento: en hardware con lecturas perdidas la
   ventana de 450 ms podia no completarse nunca.
3. La referencia se aprendia **una sola vez**. Si el dueno cambiaba el agarre ~90
   grados, o encendia el equipo en otra pose, `dot = 0` para las dos imagenes y la
   politica quedaba ciega: ningun giro posterior se detectaba.

Tambien se corrigio `rotationInPlaneSign()`: comparaba contra `s_policy.applied`,
que no se actualiza cuando un modo fijado pone otra imagen en el vidrio, asi que
el juego y la cara de Sofia espejaban para el lado equivocado tras fijar la
rotacion.

Nueva politica (`rotation_policy.h`): decision por la **direccion** de la gravedad
en el plano, con histeresis (`ROT_FLIP_DOT` -0.5 / `ROT_BACK_DOT` +0.5), las
muestras sin plano se **abstienen** en vez de reiniciar la ventana, y
`ROT_REANCHOR_MS` (2 s) reaprende la referencia cuando el dueno mantiene una pose
sin que haya decision (un giro real decide en 450 ms, muy por debajo).

L13 (verificacion, host): `python3 tools/run_tests.py` -> 27/27. Las reglas nuevas
se vieron en RED antes del arreglo: giro con la gravedad en el eje corto, muestra
plana en medio del giro, y cambio de agarre seguido de un giro de 180 (esta ultima
tambien en RED contra una copia del header con `ROT_REANCHOR_MS` enorme).
`arduino-cli compile --warnings all` con el FQBN de INSTALL.md: 0 errores, un
warning preexistente ajeno a la rotacion (`buddy_link.h`, switch sin
`BUDDY_LINK_OFFLINE`).

L14 (hardware, bloqueado primero): el dueno autorizo flashear y correr
`tools/probes/rot_auto_check.txt` (que ahora cubre el agarre parado y el de
costado, y verifica que un rolido de timon no mueva la imagen). La primera subida
fallo dos veces con "Failed to connect to ESP32-S3: No serial data received"
(arduino-cli y tambien `python -m esptool --before usb-reset`) y abrir el puerto
no devolvia ni el banner: el equipo no respondia, ni la app ni el bootloader.
Hizo falta un RESET fisico del dueno; sin eso no se puede flashear.

L15 (hardware, verificado): con el equipo despierto, `arduino-cli upload -p COM5`
con el FQBN de INSTALL.md escribio 1455552 bytes y respondio `Hash of data
verified`. `tools/probes/rot_auto_check.txt` sobre el firmware nuevo:

```
rot mode=auto (4) lock=off image=1 sign=1      <- estado inicial
imufake  1.00  0.00  0.00  -> image=1 sign=1   agarre parado, sin cambio
imufake -1.00  0.00  0.00  -> image=2 sign=-1  giro 180: cambia
imufake  0.05  0.70  0.71  -> image=2 sign=-1  agarre de costado: sin cambio
imufake -0.05 -0.70  0.71  -> image=1 sign=1   giro 180 en ese agarre: CAMBIA
imufake -0.10 -0.50  0.86  -> image=1 sign=1   rolido de timon: sin cambio
imufake -0.05 -0.70  0.71  -> image=1 sign=1   vuelve al agarre: sin cambio
imureal                    -> image=2 sign=-1  sigue la pose real del equipo
```

Lo importante es la cuarta linea: con la politica anterior esa pose (gravedad
sobre el eje corto) no decidia nunca. `sign` acompania a la imagen en todas las
lineas, que es la correccion de `rotationInPlaneSign()`.

L16 (hardware, restaurado): `tools/probes/rotation_restore.txt` -> `rot
mode=auto (4) lock=off image=1 sign=1` y `/api/status` con `orientation: 1`,
`rotationMode: "auto"`, `rotationLocked: false`. Unico efecto lateral de la
prueba: el AP se apago solo a los 30 s de enlace de casa probado, que es su
comportamiento documentado.

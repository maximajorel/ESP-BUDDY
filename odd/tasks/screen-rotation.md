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
| T1 | `rotation_policy.h` (puro: politica, espejado, ciclo de modos) + `tools/rotation_policy_test.cpp` primero | S1,S2,S5,S6 | inline, test-first | |
| T2 | Claves NVS `rotMode`/`rotLock` en `settings.*`; modulo `rotation.*` (aplicar + tick + hold); arranque y `loop()` en `.ino`; comandos `rot` y `orient` | S1,S2,S4,S8 | inline | |
| T3 | Compensacion: `rotationInPlaneSign()`/`rotationMirrorInPlane()` usados por `screen_game.cpp` (readTilt + hold) y `screen_buddy.cpp` | S5,S7 | inline | |
| T4 | Pantalla CONFIG: cuatro filas con las dos nuevas, sin romper las dos de brillo | S3 | inline | |
| T5 | Panel web: `/api/status` y `POST /api/settings`, `index.html`, `app.js`, mock, `web_assets.h` repackado | S3,S4 | inline | |
| T6 | Verificacion: tests g++ de todo `tools/`, `verify_assets.py`, compilacion `arduino-cli` con el FQBN de INSTALL.md | all | inline | |

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

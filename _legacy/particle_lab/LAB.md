# particle_lab — simulador de partículas con sensores

Dos vistas sobre la pantalla de 172×320, conmutables con el botón **BOOT**
(GPIO0). La gravedad sale del acelerómetro QMI8658, así que mover la placa
mueve las partículas.

| Vista | Física | Qué se ve |
|---|---|---|
| **Arena** | autómata de granos que caen | miles de granos apilándose con talud de ~45°; al inclinar, avalancha hacia ese lado |
| **Agua** | modelo de masa por celda | tanque que nivela a superficie plana; al inclinar, el agua se acomoda contra la pared baja |

## Controles

- **BOOT**: cambia de vista (arena ↔ agua). Al cambiar, el mundo se regenera.
- **Serie** (115200):

| Comando | Efecto |
|---|---|
| `m` | cambia de vista (igual que BOOT) |
| `r` | regenera el mundo actual |
| `s` | estadísticas: fps, partículas, centroide, gravedad |
| `d` | mapa ASCII del mundo (para depurar sin mirar la pantalla) |
| `t` | ejercita el antirrebote con un contacto sintético |
| `p` | lleva GPIO0 a nivel bajo y deja que el camino real del botón cambie la vista |
| `g <x> <y>` | fuerza una dirección de gravedad, ej. `g -1 0` |
| `g auto` | vuelve al acelerómetro |
| `axis <sx> <sy>` | invierte los ejes del sensor (`-1` o `1`) |
| `?` | ayuda |

El centroide es la prueba objetiva de que la física responde: está en 0..1, y
se mueve en la dirección de la gravedad forzada.

## Herramienta de consola

```powershell
cd C:\Users\Máximo Majorel\workspace\esp32\tools
python lab_probe.py COM5 s d "g -1 0" wait:3 d
```

Espera el banner de arranque antes de mandar comandos (abrir el puerto resetea
el chip y USB CDC descarta lo que se escriba antes de que el firmware lea).

## Cómo está armado

```
particle_lab.ino   lazo principal: sensor -> física -> render -> push
Display_ST7789.*   driver del panel (Waveshare, con dos arreglos propios)
imu.*              QMI8658 mínimo: solo acelerómetro
sim.*              el mundo: grilla, arena, agua, paletas, diagnóstico
```

Grilla lógica de 86×160 celdas, cada celda 2×2 píxeles de la pantalla. El
framebuffer completo (110 KB) vive en PSRAM y se manda al panel en una sola
transferencia SPI a 80 MHz.

### Por qué las dos físicas son distintas

**Arena** es un autómata de granos: cada grano intenta los ocho vecinos en el
orden en que mejor siguen a la gravedad. Eso produce montículos con talud, no
un nivel plano — es lo correcto para un sólido granular.

**Agua** es un modelo de masa: cada celda guarda 0..255 de agua. Por frame se
mide cuánta agua tiene cada línea perpendicular a la gravedad, se transfiere
de las líneas llenas a las vacías hasta igualarlas, y se reescribe cada celda
compactando contra el lado de la gravedad. Un líquido en reposo tiene la misma
cantidad en cada línea, o sea superficie perpendicular a la gravedad. Las
transferencias son siempre entre líneas, así que el volumen se conserva exacto
(verificado: 6192 celdas de agua antes y después de inclinar la placa).

La primera versión del agua difundía localmente celda a celda y tardaba más de
16 segundos en nivelar, dejando una superficie diagonal. Está documentado en
`sim.cpp`.

## Si la placa deja de responder

Síntoma: el dispositivo sigue apareciendo en Windows (COM5 presente, VID_303A),
pero no llega ni un byte por el puerto y esptool responde
`No serial data received` incluso con `--before usb-reset`.

Es el CDC de la placa que quedó colgado, no el firmware ni el toolchain.
Recuperación, en orden:

1. Apretá **RESET** (el botón al lado de BOOT).
2. Si sigue igual, desconectá el USB-C y volvé a conectarlo.
3. Si aun así no aparece, mantené **BOOT** apretado mientras reconectás: entra
   en modo descarga y esptool conecta seguro.

Después, reflashear:

```powershell
& $CLI upload -b $FQBN -p COM5 $SKETCH
```

### Causa conocida

Una causa reproducida: cerrar de golpe (matar) el proceso que tenía el puerto
abierto mientras el firmware estaba escribiendo. El `Serial.print` del
heartbeat se queda esperando un host que ya no está y bloquea el lazo
principal. Por eso `setup()` llama a `Serial.setTxTimeoutMs(0)`: si nadie lee,
la escritura se descarta en lugar de colgar la placa.

## Botón BOOT

GPIO0, activo bajo, pull-up externo de 10K a 3V3 (R10 en el esquemático) y
100nF de antirrebote (C25). El firmware lo lee con la máquina de estados de
`button_debounce.h` y acepta una pulsación tras 30 ms estable en bajo.

Como el pulsador es un contacto mecánico, no hay forma de verificarlo por
software. Lo que sí está cubierto:

1. la lógica de antirrebote, con tests en el host;
2. el camino completo en la placa, con el comando `t`, que inyecta un contacto
   sintético y comprueba que la vista cambia;
3. el estado en reposo (`boot=1`), que confirma que el pull-up está presente;
4. el mapeo pin/red, contra el esquemático oficial.

**Pulsación real observada.** Con `tools\boot_watch.py` escuchando, mover la
placa y apretar BOOT produjo `mode -> water`. Nota útil para depurar: el campo
`boot=` de las estadísticas muestrea cada 4 s, así que una pulsación corta
nunca aparece ahí aunque haya funcionado; lo que la delata es el cambio de
vista.

El comando `p` sirve para separar firmware de hardware cuando algo falla: pone
GPIO0 como salida en bajo
(0,3 mA contra el pull-up de 10K, inofensivo) y deja que `pollButton()` lea el
pin de verdad, así que el cambio de vista pasa por el mismo código que una
pulsación. Verificado en ambos sentidos: `p` → `mode -> water`, `p` →
`mode -> sand`, y `boot=1` al restaurar la entrada.

## Evidencia de que funciona

Medido por puerto serie con `g` en modo override:

| Prueba | Resultado |
|---|---|
| arena, gravedad abajo | superficie plana en el fondo |
| arena, gravedad izquierda | cuña contra la pared izquierda, talud ~45° |
| arena, gravedad arriba | la arena sube y se apila arriba |
| agua, gravedad abajo | superficie horizontal |
| agua, gravedad izquierda | interfaz **vertical** en x≈39, plana |
| agua, gravedad arriba | superficie horizontal arriba |
| agua, volumen | 6192 → 6192 celdas tras inclinar |
| `axis -1 1` | la gravedad se invierte y el centroide pasa de x=0.22 a x=0.78 |
| antirrebote | 6/6 casos en `tools\button_debounce_test.cpp` (compilado con g++) |
| camino del botón (lógica) | `t` imprime `press accepted` y cambia de vista |
| camino del botón (pad real) | `p` lleva GPIO0 a bajo y el `pollButton()` real cambia de vista, dos veces, en ambos sentidos |
| pin BOOT en reposo | `boot=1` en las estadísticas |
| mapeo del botón | esquemático oficial: pull-up `R10` 10K de IO0 a 3V3, red `BOOT` con `C25` 100nF a GND y `R30` 0R; coincide con `BOOT_KEY_PIN 0` del driver de Waveshare |
| **pulsación física de BOOT** | vigilante serie: `mode -> water` tras mover la placa (aceleración `(-0.30,0.05,-0.96)` → `(-0.49,-0.65,0.19)` → `(-0.36,-0.11,0.84)`) |
| las partículas siguen el movimiento | en esas mismas muestras el centroide de arena pasó de `(0.57, 0.71)` a `(0.52, 0.72)` |
| fps | 29-33 en arena, 38-42 en agua |

## Si el movimiento va para el lado equivocado

El eje del sensor no necesariamente coincide con el de la pantalla. Sin
recompilar:

```
axis -1 1     # invierte solo X
axis 1 -1     # invierte solo Y
axis -1 -1    # invierte ambos
```

Probalo en serie hasta que inclinar la placa haga caer la arena hacia donde
apunta la gravedad real. Cuando lo tengas, fijá los valores en `s_axisX` y
`s_axisY` al principio de `particle_lab.ino`.

## Rango de configuración

En `sim.h`: `SIM_CELL` (2 px por celda; 3 da granos más grandes y menos
partículas). En `sim.cpp`: `simReset()` define cuánta arena/agua llena la
pantalla, y `s_sandSw` / `s_waterSw` son las paletas.

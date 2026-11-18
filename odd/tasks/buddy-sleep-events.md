# Buddy: sueño local y `/events`

Implementa el contrato nuevo del bridge (`buddy-08-suenio-y-eventos.md`) en el
firmware. La parte térmica (§7, `temperatureRead()`) queda **fuera**: el repo ya
documentó que ladrilló dos placas (`esp32_gadget/console.cpp:175`).

## Specs

S1. `"El **estado** lo define el bridge. La **paciencia y el cuerpo** los define la placa."`
S2. `"`bored` **salió del contrato** y el sueño pasó a ser 100 % local en el firmware."`
S3. `"silencio_servidor = sleep_hint / silencio_local = now - ultimo_estimulo_imu / inactividad = MIN(silencio_servidor, silencio_local)"`
S4. `"Cualquier estimulo del IMU **resetea** `silencio_local` a 0 y despierta **inmediatamente**, sin esperar al poll."`
S5. `"un `sleep_hint` alto NO puede volver a dormir a la placa si el IMU registró movimiento hace <umbral."`
S6. Sueño leve a los `60 s`, profundo a los `180 s`, con `#define` y no hardcodeado en la lógica.
S7. `"`GET /events?since=<ev_seq>&wait=25`"` con `Header: X-Buddy-Token`. `wait` máx 25; timeout del socket `> wait` (`wait + 10 s`).
S8. `"Si `/events` falla o tarda**, caé a poll de `/state` cada 1 s."` Un solo `/events` abierto a la vez.
S9. `"`[]` no es un error"`: es timeout normal o `seq` viejo. `404` = bridge viejo, sólo reintentar el probe cada 60 s.
S10. `"Cursor: arrancá con el `ev_seq` de `/state`"`; si el `seq` pedido es más nuevo que el último, `[]` y reponerse con `/state`.
S11. Eventos recibidos: `state` (con `prev`), `label` y `ask.accepted` (con `button`).
S12. `"`prev` distingue `tool→done` de `thinking→done`"`; `"`ask.accepted` produce micro-reacción antes del primer `thinking`"`.
S13. `"`active` `-1` = el bridge no pudo leer → cara neutra, nunca de error"`.

## Tasks

| ID | Specs | Ruta | Qué |
|---|---|---|---|
| T1 | S1 S2 S3 S4 S5 S6 | inline | `buddy_sleep.h` puro + sacar `bored` del contrato y del anim + reset por IMU |
| T2 | S10 S11 S13 | inline | parseo de `sleep_hint` / `ev_seq` / `prev` en `/state` |
| T3 | S7 S9 S11 | inline | `buddy_event.h` puro (parser de `/events`) + GET con cliente propio y timeout de long-poll |
| T4 | S7 S8 S9 S10 | inline | máquina `EVENTS`/`POLL` en el task de red, fallback y probe 404 |
| T5 | S12 | inline | reacciones: `prev` graduado y micro-reacción de `ask.accepted` |
| T6 | — | inline | docs, consola y verificación final |

## Log

L1 (Maxi, 2026-11-18): "Ok entonces vamos con el tema del buddy nomas" —
autoriza implementar el contenido del documento del buddy, dejando afuera la
parte térmica.
L2: verificado sin token que el bridge responde `401` en `/events` (y `404` en
una ruta inexistente), así que el endpoint existe en producción.
L3 (cierre, 2026-11-18): T1–T5 quedaron en **un solo commit** `077393d` sobre
`feat/buddy-sleep-events`. Fue atómico a propósito: el renombre del vocabulario
(`bored` → `BUDDY_ASLEEP`) y su único consumidor viven en `screen_buddy.cpp`, así
que cualquier corte deja un commit que no compila. La rama arranca desde
`feat/web-inicial`, que ya venía con 7 commits de diferencia contra `main`: el
contador del hook mide esa herencia, no esta feature.
L4: `console.cpp` e `INSTALL.md` quedaron **sin commitear** a propósito: ya
estaban modificados en el árbol por el trabajo de rotación/AP en curso, y mezclar
eso en este commit habría juntado dos cambios sin relación. Las líneas del buddy
(`buddysleep`, la nota en INSTALL) están en el árbol, esperando ese slice.

L5 (hardware, 2026-11-18): flasheado en COM5 (`Hash of data verified`, 1.503.502
bytes) y probado con `tools/probes/buddy_sleep_events.txt` +
`tools/probes/buddy_sleep_shake.txt`:

- `sleep stage=light idle=81303ms hint=81.5s cursor=5 asleep=1` antes de tocar
  nada: el reloj local decidió el sueño y la cara estaba en la escalera.
- Sacudida con `imufake 2.4 0 0 300 0 0` → `idle=49ms`, `asleep=0` en la lectura
  inmediata: el despertar por IMU es inmediato y el servidor no lo pisa.
- `hint=` llegó con número (1409.5 s, 1602.6 s, 81.5 s) y bajó a `0.3s` después de
  un run, como corresponde al silencio de Hermes.
- Run de punta a punta: `api POST /api/buddy/ask` → `HTTP 200`, `cursor` 2 → 4 → 5,
  estado `done` con `phase=ok`. `ageMs` siguió en 10 s entre dos status, o sea que
  el equipo **no** está poleteando cada segundo: está colgado del long-poll.

Dos cosas que el probe enseñó y quedaron corregidas:
1. `buddyface` devolvía antes del bloque de sueño, así que pinear una cara
   congelaba el reloj y la lectura mentía. El pin ahora también resetea con el IMU.
2. `wakeUntil` dura 4 s: una lectura tomada más de 4 s después de la sacudida
   muestra el silencio creciendo y parece que no despertó. El probe nuevo lee
   inmediato, sin `sleep` en el medio.

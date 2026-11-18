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

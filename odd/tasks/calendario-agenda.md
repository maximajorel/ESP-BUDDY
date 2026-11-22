# Calendario completo en el popover: agenda del día

El popover mostraba sólo la próxima cita porque el firmware no publica la lista.
Los eventos ya están en la RAM del equipo (`s_events[CALENDAR_MAX_EVENTS]`, 12,
en `esp32_gadget/calendar.cpp`): lo que falta es que los publique y que la app
los dibuje con espacio suficiente para leerlos.

## Specs

S1. Pedido del dueño, verbatim:
"Respecto al calendario, no lo podemos poner completo? o escalara la app para que
podamos usarlo y ver los eventos con la estetica prevista?"

S2. Alcance decidido, verbatim: "Agenda del día ahora, mes después". El mes
completo queda **fuera** de esta feature: hoy la consulta a Google pide de -1 h a
+26 h con `maxResults=12` (`kPastSeconds`/`kFutureSeconds` en `calendar.cpp`), así
que ver un mes exige cambiar la ventana de sync y retener muchos más eventos en
un equipo con poca RAM. Se documenta y no se implementa.

S3. Endpoint nuevo `GET /api/calendar/events`, protegido por sesión por caer bajo
el prefijo `/api/calendar` (`adminRequiresSession`), que devuelve los eventos que
el equipo ya tiene en memoria:

```json
{ "ok": true, "tz": -3, "count": 2,
  "events": [
    { "summary": "Reunión", "location": "Zoom", "startEpoch": 1764000000,
      "endEpoch": 1764003600, "leadMin": 10, "allDay": false } ] }
```

`tz` es el offset horario del equipo (el mismo de `/api/status`), para que la app
pueda formatear horas locales sin una segunda llamada. Las horas viajan como
epoch UTC: el equipo no manda texto formateado y la app no inventa zonas.

S4. La tarjeta de calendario pasa de "próxima cita" a **agenda del día**: la
próxima cita sigue arriba como hasta ahora y abajo va la lista de eventos con
hora local, título y lugar. Un evento de todo el día se rotula "todo el día" en
vez de una hora. La lista respeta el orden que manda el equipo.

S5. El popover crece a pedido, verbatim: "El popover crece a pedido". Un control
en la tarjeta de calendario agranda la ventana de 388×660 a 520×820 y la vuelve
a achicar; el tamaño elegido se recuerda entre arranques en el store local. Al
agrandar, la esquina inferior derecha —donde vive el pico que apunta al icono de
la bandeja— **no se mueve**: la ventana crece hacia arriba y hacia la izquierda.

S6. Sin datos no se inventa nada: si la sesión del panel falta, si el equipo no
está autorizado en Google o si no hay eventos, la tarjeta lo dice con las mismas
palabras que hoy.

## Tasks

| ID | Spec | Ruta | Commit |
|----|------|------|--------|
| C1 | S3 | worker (firmware): accesor de eventos + `calendar_events.cpp` + handler en `net.cpp` + test de host | `feat(calendar): publicar los eventos del día por HTTP` |
| C2 | S4, S5, S6 | worker (app): comando de tamaño, flag en el store y la tarjeta con la agenda | `feat(tray): agenda del día y popover que crece` |

## Log

L1. Pedido del dueño (verbatim en S1) y las dos decisiones del cuestionario:
alcance "Agenda del día ahora, mes después" y ventana "El popover crece a pedido".

L2. Relevamiento que hace barato el endpoint: `calendar.cpp` ya guarda 12
`CalendarEvent` completos en `s_events`, y `net.cpp` ya los recorre para calcular
la próxima cita. El costo es serializar, no pedirle memoria al equipo.

L3. Contrato congelado antes de repartir: los dos workers no se ven entre sí, así
que el JSON de S3 es la única interfaz y ninguno de los dos puede cambiarlo sin
romper al otro.

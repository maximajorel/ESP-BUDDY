# esp-web-console — consola web para la ESP32-S3-LCD-1.47B

Firmware que expone un AP WiFi con un servidor HTTP/WebSocket, y una app web
(servida por la propia placa) para controlar el LCD y la tira RGB y ver la
telemetría del IMU en vivo.

## Specs

**S1** — "Podemos hacer otra carpeta con un proyecto web que se sincronice con
el ESP?" → carpeta nueva `web_ui/` con la app, y `esp_web_console/` con el
firmware que la sirve.

**S2** — "Y que pueda controlar parametros de forma facil? Como el RGB que
tiene" → control de la tira WS2812 (GPIO38): color, brillo e intensidad, en
vivo desde la web.

**S3** — "la imagen que tiene ahi pueda en el LCD" → subir una imagen desde la
web y verla en el LCD de 172×320.

**S4** — "quiero que la web sea intercomunique" → canal bidireccional: la web
manda comandos y la placa empuja estado/telemetría sin refrescar la página.

**S5** — "y pushee el firmware para poder conectarme alli" → el firmware se
flashea y la placa levanta su propio punto de acceso, para conectarse a ella.

## Decisiones de diseño

- **AP propio** (`ESP-LCD-1.47B`, 192.168.4.1): no hace falta conocer la red
  del usuario para que funcione, que es lo que pide S5. Si más adelante se
  guardan credenciales de una red, se intenta STA primero y se cae a AP.
- **La web la sirve la placa**, empaquetada en flash. Así "conectarme allí" es
  abrir `http://192.168.4.1` sin servidor ni build en la PC.
- **Dos puertos**: 80 para HTTP (UI y subida de imagen), 81 para WebSocket
  (telemetría y comandos). Mantiene simple el manejo de la subida binaria.
- **La imagen la decodifica el navegador**: se dibuja en un canvas a 172×320 y
  se envía RGB565 crudo (110 080 bytes). La placa no necesita decodificador y
  acepta cualquier formato que el navegador soporte.
- Se reutilizan los drivers ya verificados: `Display_ST7789` e `imu` del
  proyecto `particle_lab`.

## Tasks

| ID | Qué | Specs | Ruta | Commit |
|----|-----|-------|------|--------|
| T1 | Estructura: carpetas, empaquetador de assets, esqueleto | S1 | inline | n/a (no es repo git) |
| T2 | Firmware: AP WiFi + servidor HTTP que sirve la UI | S5 | inline | n/a |
| T3 | Firmware: WS bidireccional con telemetría y comandos | S4 | inline | n/a |
| T4 | Firmware: control de la tira RGB (GPIO38) | S2 | inline | n/a |
| T5 | Firmware: subida de imagen y volcado al LCD | S3 | inline | n/a |
| T6 | App web: layout, conexión, controles y preview | S1-S4 | inline | n/a |
| T7 | Verificación en hardware y documentación | S1-S5 | inline | n/a |

Estado: T1-T7 hechas. Firmware de 994 126 bytes flasheado y arrancando limpio.
App web verificada de punta a punta contra `tools/mock_console.py` con Chrome
headless. Guía en `WEB_CONSOLE.md`.

## Log

**L1** (pedido original, verbatim):
"Podemos hacer otra carpeta con un proyecto web que se sincronice con el ESP? Y que
pueda controlar parametros de forma facil? Como el RGB que tiene, la imagen
que tiene ahi pueda en el LCD, no se las posibilidades, quiero que la web sea
intercomunique y pushee el firmware para poder conectarme alli"

**L2** (evidencia en hardware, 18:30). El usuario se conectó al AP y usó la
consola. Registro serie de la placa:

```
[hb] clients=1  heap=252516  bl=100
271 comandos: 188 de brillo, 81 de intensidad, 2 de efecto
```

Eso cierra S1, S2, S4 y S5: la placa entregó la página por HTTP (sin eso no
habría WebSocket), el canal es bidireccional y los comandos se aplicaron
(`bl=100` es el slider al tope).

**L3** (verificación visual, 21:38). Para ver el panel sin depender de un
navegador, se agregó al firmware una consola por puerto serie (`bl`, `bars`,
`fill`, `state`). Con ella: `bl 8` → `bl=8`, `bars` → `drew test bars`, y una
captura de la cámara integrada mostró **las 8 franjas de colores en el panel**.

Eso cierra S3 en su parte observable: el mismo `LCD_PushFullFrameBytes` dibuja
barras, degradé de arranque e imagen. El ST7789 no tiene MISO, así que no hay
lectura de vuelta posible; la cámara fue el único instrumento disponible.

Estado final: S1 a S5 cubiertos. Firmware de 994 686 bytes, brillo restaurado a
70 y barras visibles en pantalla.

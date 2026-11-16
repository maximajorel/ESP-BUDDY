# Consola web para la ESP32-S3-LCD-1.47B

La placa levanta su propio punto de acceso y sirve una app web desde la flash.
No hay nada que instalar ni compilar en la PC.

## Conectarse

1. En el celular o la PC, unirse a la red WiFi **`ESP-LCD-1.47B`**
   (contraseña `lcdlab1234`).
2. Abrir **http://192.168.4.1**

El LED RGB de la placa arranca en azul tenue como señal de vida. La pantalla
muestra un degradé oscuro hasta que algo se dibuja.

## Qué controla

| Sección | Acciones |
|---|---|
| **Pantalla** | brillo (backlight PWM), pintar un color, barras de test, enviar una imagen |
| **Tira RGB** | color, intensidad, efectos sólido / arcoíris / apagado |
| **Telemetría** | acelerómetro y giroscopio en vivo, uptime, heap, clientes |
| **Red** | guardar credenciales de otra WiFi y reiniciar |

La app es bidireccional: manda comandos por WebSocket y recibe el estado a 5 Hz
sin refrescar la página.

## Cómo está armado

```
web_ui/                    la app (HTML + CSS + JS, sin build, sin dependencias)
├── index.html
├── styles.css
└── app.js

esp_web_console/           el firmware
├── esp_web_console.ino    AP + HTTP + WebSocket + comandos
├── Display_ST7789.*       driver del panel (compartido con particle_lab)
├── imu.*                  QMI8658: acelerómetro y giroscopio
├── rgb.*                  bead WS2812 en GPIO38
└── web_assets.h           generado, no editar

tools/pack_web.py          empaqueta web_ui/ dentro del firmware
tools/mock_console.py      servidor de reemplazo para probar la UI sin la placa
```

La app **se sirve desde la placa**: `pack_web.py` convierte los archivos en
arrays en flash y el firmware los entrega con `send_P`. Así "conectarme allí"
no depende de ningún servidor local.

### Por qué la imagen la procesa el navegador

Subir un JPEG o PNG y decodificarlo en el ESP32 costaría un decodificador y
mucha RAM. En cambio el navegador dibuja la imagen en un canvas de 172×320 (con
recorte tipo *cover*), la convierte a RGB565 y manda 110 080 bytes crudos por
`POST /api/image`. La placa solo copia a PSRAM y vuelca al panel.

El orden de bytes importa: el ST7789 espera el byte alto primero, y el
navegador lo emite así (`f8 00` para rojo puro).

## Protocolo

**HTTP (puerto 80)**

| Ruta | Método | Qué hace |
|---|---|---|
| `/` | GET | la app |
| `/api/state` | GET | estado en JSON, útil para probar con curl |
| `/api/image` | POST | 172×320 RGB565 crudo, 110 080 bytes |
| `/api/wifi` | POST | `{"ssid":"…","pass":"…"}`, guarda en NVS y reinicia |

**WebSocket (puerto 81)**

La placa empuja `{"t":"state", …}` cada 200 ms y `{"t":"hello",…}` al conectar.
La web manda:

```json
{"t":"rgb","r":255,"g":136,"b":0}
{"t":"level","v":64}
{"t":"effect","v":"solid|rainbow|off"}
{"t":"bl","v":80}
{"t":"fill","c":"#1b2a6b"}
{"t":"bars"}
```

## Bugs encontrados y corregidos

1. **Colores equivocados al pintar la pantalla.** `parseHexColor` truncaba el
   valor de 24 bits a 16 (`#00ff00` → `0xFF00`) en vez de convertir RGB888 a
   RGB565. Un verde salía celeste. Ahora cada canal se recorta a su ancho:
   `00ff00 → 0x07E0`, `ff0000 → 0xF800`, `0000ff → 0x001F`.
   El comando serie `hex <rrggbb>` imprime lo que el firmware calcula, para
   verificarlo sin mirar el panel.
2. **La tira RGB cambiaba rojo por verde.** El bead de esta placa es de orden
   RGB, no GRB. Con `NEO_GRB` un pedido de rojo encendía verde.
3. **La imagen no hacía nada con fotos de celular.** No había manejo de errores:
   si el navegador no podía decodificar el archivo (típico con **HEIC**), la
   promesa se rechazaba en silencio. Ahora hay fallback por `<img>` y un mensaje
   explícito con la causa.
4. **"La placa rechazó la imagen".** El navegador mandaba el cuerpo como
   `application/octet-stream`, y el `WebServer` del ESP32 **solo entrega al
   callback de subida los cuerpos `multipart/form-data`**. Sin multipart, el
   cuerpo va al parser de argumentos, el callback nunca corre, `s_uploaded`
   queda en 0 y el firmware responde `400` con `"got":0`. Ahora la web envía un
   `FormData`. El servidor de prueba también rechaza lo que no sea multipart,
   para que este error no vuelva a pasar desapercibido.

## Consola por puerto serie

El firmware también acepta unos pocos comandos por USB (115200), pensados para
depurar el panel sin un navegador conectado:

| Comando | Efecto |
|---|---|
| `bl <0-100>` | brillo del backlight |
| `bars` | dibuja las 8 franjas de test |
| `fill <rrggbb>` | pinta la pantalla de un color |
| `rgb <rrggbb>` | color del bead, sin pasar por la web |
| `hex <rrggbb>` | imprime el RGB565 que calcula el firmware |
| `state` | brillo, clientes WS y heap |
| `?` | ayuda |

Es especialmente útil para bajar el brillo cuando se quiere fotografiar el
panel: al 100 la cámara satura y no se distingue el contenido.

## Editar la app y reflashear

El flujo completo, desde `workspace\esp32`:

```powershell
$CLI = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$FQBN = "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240"

python tools\pack_web.py web_ui esp_web_console\web_assets.h
& $CLI compile -b $FQBN "C:\Users\Máximo Majorel\workspace\esp32\esp_web_console"
& $CLI upload  -b $FQBN -p COM5 "C:\Users\Máximo Majorel\workspace\esp32\esp_web_console"
```

**No te olvides del paso `pack_web.py`**: si editás la web y solo compilás, la
placa sigue sirviendo la versión vieja. Para confirmar que el header reproduce
los archivos byte a byte, corré el round-trip:

```powershell
python tools\verify_assets.py web_ui esp_web_console\web_assets.h
```

Decodifica los arrays del header y los compara con `web_ui/`; si algo se truncó
o se escapó mal, lo dice con el índice exacto.

## Probar la UI sin la placa

`tools/mock_console.py` sirve `web_ui/` en `http://127.0.0.1:8787` y habla el
mismo protocolo en `ws://127.0.0.1:8788`. La página acepta `?ws=` para apuntar
el socket a otro lado:

```
http://127.0.0.1:8787/index.html?ws=ws://127.0.0.1:8788
```

Los comandos recibidos quedan en `tools/mock_commands.log` y la última subida en
`tools/mock_upload.log` (ruta, tamaño y primeros bytes). Es lo que se usó para
verificar la app de punta a punta.

## Verificación hecha

| Qué | Evidencia |
|---|---|
| Firmware arranca | serie: `AP 'ESP-LCD-1.47B' up, ip=192.168.4.1`, `http://192.168.4.1`, `ws://192.168.4.1:81`, `web assets packed: 3` |
| La app conecta y recibe telemetría | Chrome headless: estado `conectado`, aceleración y giroscopio actualizándose, `1 cliente`, `250 KB libres` |
| Los botones mandan lo correcto | `mock_commands.log`: `{"t":"effect","v":"rainbow"}` y `{"t":"bars"}` |
| El pipeline de imagen funciona | `mock_upload.log`: `length=110080 first=f800f800...` |
| Sin errores de JS | consola de Chrome limpia (solo el `favicon.ico`, ya silenciado) |
| **El panel dibuja lo que manda el firmware** | foto del dispositivo mostrando las 8 franjas de `bars`, dibujadas con `LCD_PushFullFrameBytes` (la misma función que usa la imagen) |
| Consola serie | `bl 8` → `bl=8`, `bars` → `drew test bars`, `state` → `bl=70 clients=0 heap=255832` |
| Tamaño del firmware | 994 686 bytes (31% de la partición de 3 MB) |

El ST7789 de esta placa no tiene MISO conectado (`EXAMPLE_PIN_NUM_MISO = -1`),
así que no existe lectura de vuelta: la única forma de comprobar el panel es
mirarlo. Por eso la consola serie y las barras existen.

Lo que **no** pude verificar yo: que la placa entregue la página por HTTP a un
cliente real. Eso necesita un dispositivo unido a su punto de acceso, y la PC
estaba en otra red. Es exactamente lo que se comprueba la primera vez que te
conectás.

## Si algo falla

- **No aparece la red `ESP-LCD-1.47B`**: revisá el monitor serie a 115200. Si el
  log dice `STA failed, falling back to the access point` es que hay
  credenciales viejas guardadas; borralas guardando un SSID vacío o reflasheando.
- **Conecta pero no carga**: probá `http://192.168.4.1/api/state`; si responde
  JSON, la red está bien y el problema es la app (mirá la consola del navegador).
- **La imagen no llega**: el `POST` necesita exactamente 110 080 bytes. El log
  serie imprime `[img] upload start/end` con los bytes recibidos.
- **Se queda sin responder**: RESET, o reconectar el USB-C. El firmware ya llama
  a `Serial.setTxTimeoutMs(0)`, así que no se cuelga si cerrás el monitor serie.

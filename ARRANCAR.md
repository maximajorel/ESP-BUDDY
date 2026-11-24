# Waveshare ESP32-S3-LCD-1.47B — cómo arrancar

Todo vive en Windows. WSL solo se usó como terminal para orquestar; no hay
nada instalado en Linux.

## La placa

| Dato | Valor |
|---|---|
| MCU | ESP32-S3R8, dual-core LX7 @240 MHz |
| Memoria | 512 KB SRAM, **8 MB PSRAM octal**, **16 MB flash** |
| Pantalla | TFT ST7789, **172×320**, 262K colores, SPI @80 MHz |
| USB | Serial/JTAG nativo (`VID_303A:PID_1001`) → aparece como **COM5** |
| IMU | QMI8658 (I2C), device ID `0x7c` |
| RGB | Tira WS2812 en GPIO38 |
| Extra | slot TF, carga de batería 3.7 V |

### Pines del LCD

| Señal | GPIO |
|---|---|
| MOSI | 45 |
| SCLK | 40 |
| CS | 42 |
| DC | 41 |
| RST | 39 |
| BL (backlight) | 46 |

## Configuración de la placa (FQBN)

Esta es la cadena exacta que funciona. Guardala, es la clave de todo:

```
esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240
```

En la IDE, equivale a:

- Placa: **ESP32S3 Dev Module**
- **USB CDC On Boot: Enabled** ← sin esto `Serial` no sale por USB
- Flash Size: **16MB (128Mb)**
- PSRAM: **OPI PSRAM** (el S3R8 es octal, no QSPI)
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- Flash Mode: QIO 80MHz, Upload Speed: 921600

## Rutas

```
C:\Users\Máximo Majorel\workspace\esp32\
├── esp32_gadget\           ← EL firmware. Se trabaja solo acá.
├── tools\                  ← probe serie, mock de la API, empaquetadores
├── odd\tasks\              ← especificación y bitácora del proyecto
├── _legacy\                ← prototipos viejos y demos (solo referencia)
│   ├── hello_lcd\          ← primer sketch propio (ST7789 crudo)
│   ├── particle_lab\       ← simulador de partículas
│   ├── esp_web_console\    ← consola web anterior
│   └── web_ui\             ← UI de la consola web vieja
└── ARRANCAR.md             ← este archivo

C:\Users\Máximo Majorel\AppData\Local\Arduino15\      ← core esp32:esp32@3.3.12
C:\Users\Máximo Majorel\Documents\Arduino\libraries\  ← lvgl 8.3.10, PNGdec, OneButton
```

## Con la IDE (recomendado para aprender a mano)

Abrí Arduino IDE 2.x, y para cada sketch:

1. Seleccioná **ESP32S3 Dev Module** en el selector de placa.
2. Configurá las opciones del apartado anterior.
3. Elegí el puerto **COM5**.
4. Botón de compilar (✓) y subir (→). El monitor serie está arriba a la derecha.

## Con arduino-cli (para automatizar)

```powershell
$CLI = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$FQBN = "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240"
$SKETCH = "C:\Users\Máximo Majorel\workspace\esp32\esp32_gadget"

& $CLI compile -b $FQBN $SKETCH
& $CLI upload  -b $FQBN -p COM5 $SKETCH
& $CLI monitor -p COM5 -c baudrate=115200
```

## Qué está flasheado ahora

**`esp32_gadget` v1.0.0** — reloj, tragamonedas, juego ASCII y pantalla de red,
con panel de administración web. Verificado por serie (`tools/gadget_probe.log`).
Ver [`esp32_gadget/INSTALL.md`](esp32_gadget/INSTALL.md).

El firmware de test precompilado de Waveshare se flashea así si querés volver
al estado de fábrica:

```powershell
python -m esptool --port COM5 -b 921600 write-flash 0x0 "_legacy\waveshare-demo\Firmware\ESP32-S3-LCD-1.47B-Test.bin"
```

> `_legacy\waveshare-demo\` **no está en el repo**: son ~264 MB de SDK upstream
> (ESP-IDF, LVGL, binarios) que `.gitignore` excluye a propósito. Si necesitás
> los ejemplos o el firmware de fábrica, bajá el demo desde la página del
> producto (Waveshare ESP32-S3-LCD-1.47B) y descomprimilo en `_legacy\`.

(El binario va en `0x0` porque es una imagen completa: bootloader, tabla de
particiones y app juntos.)

## Cosas que te van a morder

**1. El puerto serie parece muerto.**
En esta placa el USB es nativo, no un chip puente. Si `USB CDC On Boot` está
en `Disabled`, `Serial.print()` no llega a ningún lado. Y aunque esté
habilitado, lo que imprimís en `setup()` se pierde si nadie está mirando:
USB CDC descarta salida si el host no tiene el puerto abierto. Poné un
print en `loop()` si querés evidencia de que vive.

**2. El monitor serie se corta al abrirlo.**
Abrir el puerto reinicia el chip. Es normal; mirá los mensajes de arranque.

**3. La SD siempre falla.**
`sdmmc_init_ocr: send_op_cond (1) returned 0x107` es "no hay tarjeta". No es
un error de la placa.

**4. Ojo con los bufferes de la pantalla.**
El driver de Waveshare usa `LCD_addWindow(x0, y0, x1, y1, ptr)`: el área
declarada tiene que caber en el buffer que le pasás. Si pedís 40 filas con un
buffer de 16, escribís fuera del array y el chip entra en panic loop
(me pasó, está documentado en `_legacy\hello_lcd\hello_lcd.ino`).

**5. PSRAM mal configurada = crashes raros.**
Si el proyecto pide PSRAM y la placa está en `QSPI PSRAM` en vez de `OPI
PSRAM`, arranca igual pero revienta cuando asigna memoria grande.

## Por dónde seguir

1. **El firmware**: `esp32_gadget\` — empezá por `README.md` e `INSTALL.md`, y
   la especificación en `odd\tasks\esp32-gadget.md`.
2. **Entender el driver**: leé `esp32_gadget\display.cpp` y `gfx.cpp`; el
   ancestro crudo está en `_legacy\hello_lcd\hello_lcd.ino`. Ahí está todo:
   comandos SPI, ventana de escritura, backlight por PWM.
3. **LVGL**: el demo oficial trae `Arduino\examples\LVGL_Arduino\
   LVGL_Example.cpp` con widgets, charts y animaciones. Viene en el paquete de
   Waveshare (ver la nota de `waveshare-demo\` arriba), no en este repo.
4. **IMU**: `esp32_gadget\imu.cpp` es el driver del QMI8658; el demo completo
   (`...\Gyro_QMI8658.cpp`) viene en ese mismo paquete.
5. **ESP-IDF**: `ESP-IDF\ESP32-S3-LCD-1.47B-Test` del paquete es el mismo demo
   en C puro. Requiere la extensión ESP-IDF de VS Code (~2 GB más).

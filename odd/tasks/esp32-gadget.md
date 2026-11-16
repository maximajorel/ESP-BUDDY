# ESP32 Gadget — multi-screen device firmware

Single Arduino firmware for the Waveshare ESP32-S3-LCD-1.47B that turns the board
into a small gadget with four screens: clock, slot machine, ASCII dodge game and
a network screen backed by a web admin panel. One physical button (BOOT / GPIO0)
drives everything: short press interacts, 3 s hold changes screen.

## Specs

S1. Clock screen — verbatim: "Una pantalla que me de la hora actual, recorda que
estoy en GMT-3 Tucuman Argentina, el reloj debe ser digital, fondo negro texto
blanco". Digital clock, UTC-3 (no DST), black background, white text.

S2. Slot machine — verbatim: "Un juego de "slot machine" que se juega con el
boton fisico, al ganar tiene que reaccionar el rgb que está atras, busquemos
colores tipicos del casino para implementar y que quede con una buena estetica".
Played with the physical button; on a win the on-board RGB LED must react;
casino colour palette; must look good.

S3. Dodge game — verbatim: "Un juego en el cual usemos el giroscopio que tiene el
dispositivo, algo que sea de esquivar cosas, se va poniendo mas dificil al pasar
el tiempo de juego y va guardando el maximo high score, puede ser por ejemplo una
nave espacial que esquiva metioritos o algo asi, el boton de encendido capaz
puede hacer que la nave dispare, quiero que la estetica de este juego sea ASCII".
Gyroscope-driven steering, difficulty ramps over time, high score persisted
across power cycles, BOOT button fires, ASCII-art aesthetics.

S4. Network screen + web panel — verbatim: "Una pantalla con los datos para
conexion de red, el nombre del wifi y la contraseña y junto a esto un panel de
administracion web para modificar este dato y gestionar otras cosas del
dispositivo, que sea amigable con un consumidor final". On-device screen shows
the network connection data (SSID and password); a web admin panel edits that
data and manages other device settings; consumer-friendly.

S5. Button model — verbatim: "para pasar de pantalla hay que mantener apretado 3
segundos boot, y si no nos sirve para interactuar, por ejemplo para poner start
en el juego, disparar, para el slot machine". Hold 3 s = next screen; short press
= screen action (start, fire, spin).

S6. Deliverable — verbatim: "crea una carpeta con todo este proyecto y luego
documentacion sobre como instalarlo". One self-contained project folder plus
installation documentation.

S7. Landscape orientation — verbatim: "otra aclaracion, me gustaria usar en
formato horizontal la pantalla!". The UI runs on a 320x172 landscape canvas.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S7 | inline: landscape framebuffer, transpose blit, orientation selector | — |
| T2 | S1,S7 | inline: gfx canvas, 5x7 font, 7-segment digits, clock screen | — |
| T3 | S2,S5 | inline: slot screen, paytable, RGB win celebration | — |
| T4 | S3,S5 | inline: ASCII dodge game, gyro steering, NVS high score | — |
| T5 | S4,S5 | inline: network screen, AP+STA, HTTP admin API | — |
| T6 | S4 | delegated: consumer-friendly web admin UI | — |
| T7 | S6 | inline: INSTALL.md + README.md | — |
| T8 | all | inline: on-hardware verification (serial harness + panel photo) | — |

## Log

L1 (verbatim): "Quiero para mi esp32 lo siguiente:

1.- Una pantalla que me de la hora actual, recorda que estoy en GMT-3 Tucuman Argentina, el reloj debe ser digital, fondo negro texto blanco
2.- Un juego de "slot machine" que se juega con el boton fisico, al ganar tiene que reaccionar el rgb que está atras, busquemos colores tipicos del casino para implementar y que quede con una buena estetica 
3.- Un juego en el cual usemos el giroscopio que tiene el dispositivo, algo que sea de esquivar cosas, se va poniendo mas dificil al pasar el tiempo de juego y va guardando el maximo high score, puede ser por ejemplo una nave espacial que esquiva metioritos o algo asi, el boton de encendido capaz puede hacer que la nave dispare, quiero que la estetica de este juego sea ASCII 
4.- Una pantalla con los datos para conexion de red, el nombre del wifi y la contraseña y junto a esto un panel de administracion web para modificar este dato y gestionar otras cosas del dispositivo, que sea amigable con un consumidor final

Entiend oque el boton de boot es el unico que podemos usar ya que el de reset reinicia el dispositivo esp, no podemos darle otro uso (CREO), por lo que para pasar de pantalla hay que mantener apretado 3 segundos boot, y si no nos sirve para interactuar, por ejemplo para poner start en el juego, disparar, para el slot machine. Podes investigar en internet proyectos similares, referencias, consejos. crea una carpeta con todo este proyecto y luego documentacion sobre como instalarlo..."

L2 (verbatim): "otra aclaracion, me gustaria usar en formato horizontal la pantalla!"

L3: Change of requirement (S7). Promotes the whole UI to a 320x172 landscape
canvas. Because the panel is physically a 172x320 strip, landscape needs a
transposed blit; the exact rotation is selected empirically on hardware with the
`orient` serial command rather than guessed.

L4: Hardware facts carried over from earlier sessions (board = Waveshare
ESP32-S3-LCD-1.47B): ST7789 172x320 on MOSI 45 / SCLK 40 / CS 42 / DC 41 / RST 39
/ BL 46; QMI8658 IMU on I2C SDA 48 / SCL 47 at 0x6B; WS2812 bead on GPIO38 with
NEO_RGB channel order; BOOT on GPIO0 with a 10K pull-up. Working FQBN:
esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240

L5: Toolchain lives on Windows (arduino-cli 1.6.0, core esp32 3.3.12); WSL drives
it through powershell.exe interop. The board enumerates as COM5. Windows exe
files reject /mnt/c paths, so Windows-style paths are passed instead.

L6 (verbatim): "Oka deja solo ese porfa! Y trabajaremos sobre ese firmware
nomas!" Workspace consolidated onto this firmware. `hello_lcd`, `particle_lab`,
`esp_web_console`, `waveshare-demo`, the old root `web_ui/`, `WEB_CONSOLE.md`,
`odd/tasks/esp-web-console.md` and the four legacy-only tools
(`lab_probe.py`, `mock_console.py`, `button_debounce_test.cpp`, plus the logs
`serial.log`, `mock_upload.log`, `watch.log`) moved to `_legacy/` by an
unforced-commit decision because the workspace is not a git repository — the
move is reversible with an inverse `mv`, deletion would not have been. No source
file, tool or documentation of this firmware changed. `ARRANCAR.md` was
repointed at the new layout and its "what is flashed now" section corrected from
the Waveshare LVGL demo to this firmware. `pack_web.py` and `verify_assets.py`
examples now name `esp32_gadget/web_ui`; `verify_assets.py` still passes, so
`web_assets.h` matches the UI sources.

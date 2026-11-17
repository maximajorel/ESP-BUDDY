# ESP32 Gadget — cómo instalarlo

Un solo firmware para la **Waveshare ESP32-S3-LCD-1.47B** que convierte la placa
en un equipo con seis pantallas y un panel de administración web:

| Pantalla | Qué hace |
|---|---|
| **Reloj** | Hora digital (GMT-3, Tucumán, Argentina) en fondo negro, dígitos blancos de siete segmentos. |
| **Tragamonedas** | Juego de casino con botón físico. Al ganar, la luz RGB de atrás hace una celebración. |
| **Meteoritos** | Juego ASCII de naves y meteoritos, con movimiento libre en dos ejes por sensor. Guarda el récord. |
| **Red** | Muestra el nombre y la contraseña del WiFi del equipo, y la dirección del panel web. |
| **Música** | Qué estás escuchando en Spotify: carátula, tema, artista y progreso. Necesita que conectes el equipo a tu WiFi y autorices tu cuenta una vez. |

Todo se maneja con **un solo botón**: el **BOOT**.

La interfaz del equipo y del panel está **en español**.

- **Toque corto** → interactúa con la pantalla actual (girar el tragamonedas, disparar, cambiar el reloj a 12/24 h, reintentar el WiFi).
- **Mantener 1 segundo** → abre el **menú de pantallas**, con el nombre grande y centrado.
  Dentro del menú: **un toque** pasa a la siguiente, **mantener 1 segundo** entra a la que está
  seleccionada. Una barra dorada abajo muestra cuánto falta para que se abra (o para entrar).
  Los nombres y el orden de ese menú se cambian desde la sección **Pantalla** del panel web.

> El botón **RESET** no se usa: reinicia el chip. El único botón de control es BOOT.

---

## 1. Qué necesitás

**En la mano**

- La placa Waveshare ESP32-S3-LCD-1.47B (pantalla 172×320, sensor QMI8658, LED RGB, 16 MB de flash, 8 MB de PSRAM).
- Un cable USB-C **de datos** (muchos cables de carga no sirven).
- Una red WiFi de 2,4 GHz, si querés que el reloj se ponga en hora solo.

**En la computadora**

- Windows con el driver de USB ya resuelto (la placa aparece como puerto COM).
- Arduino IDE 2.x **o** `arduino-cli`. El proyecto está probado con `arduino-cli 1.6.0` y el core `esp32:esp32 3.3.12`.

---

## 2. Preparar el entorno

### 2.1 Core de ESP32

En Arduino IDE: **Boards Manager** → buscá `esp32` de *Espressif Systems* →
instalá la versión **3.3.12** (cualquier 3.3.x sirve).

Con `arduino-cli`:

```powershell
arduino-cli core install esp32:esp32@3.3.12
```

### 2.2 Librerías

Sólo hacen falta dos, y las dos están en el Library Manager:

| Librería | Para qué |
|---|---|
| **Adafruit NeoPixel** | Manejar el LED RGB WS2812. |
| **ArduinoJson** (7.x) | Leer y escribir el JSON del panel web. |
| **JPEGDEC** (Larry Bank) | Decodificar la carátula de Spotify en la pantalla Música. |

Por consola:

```powershell
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli lib install "ArduinoJson"
```

> En el panel no se usa ninguna librería gráfica: el firmware dibuja el framebuffer
> a mano sobre la pantalla ST7789.

---

## 3. Configuración de la placa (esto es lo crítico)

En Arduino IDE, **Herramientas**, dejá exactamente esto:

| Opción | Valor |
|---|---|
| Placa | **ESP32S3 Dev Module** |
| **USB CDC On Boot** | **Enabled** ← sin esto el monitor serie no muestra nada |
| Flash Size | **16MB (128Mb)** |
| **PSRAM** | **OPI PSRAM** ← el ESP32-S3R8 es octal; si no, no arranca |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| Flash Mode | QIO 80MHz |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz |

Con `arduino-cli`, esa misma configuración es esta cadena (guardala):

```
esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240
```

---

## 4. Compilar y flashear

### Con Arduino IDE

1. Abrí la carpeta `esp32_gadget/` (Arduino te muestra `esp32_gadget.ino`).
2. Elegí el puerto en **Herramientas → Puerto** (por ejemplo `COM5`).
3. **Subir** (la flecha →).

### Con `arduino-cli`

```powershell
# compilar
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240" esp32_gadget

# flashear (cambiá COM5 por tu puerto)
arduino-cli upload -p COM5 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240" esp32_gadget
```

> ¿Cambiaste algo en `esp32_gadget/web_ui/`? Antes de compilar hay que volver a
> empaquetar el panel en el firmware:
>
> ```powershell
> python tools\pack_web.py esp32_gadget\web_ui esp32_gadget\web_assets.h
> ```
>
> Si te olvidás de este paso, el equipo sigue sirviendo la versión vieja del panel.

---

## 5. El primer arranque

Al encender vas a ver la pantalla de bienvenida con el nombre de la red que crea el
equipo, del estilo **`ESP32-GADGET-A1B2C3`** (los últimos tres bytes del MAC).

La contraseña de fábrica es **`12345678`**.

| Dato | Valor de fábrica |
|---|---|
| Red que crea el equipo | `ESP32-GADGET-XXXXXX` |
| Contraseña | `12345678` |
| Red del equipo | la que crea él mismo, no tu WiFi de casa |
| Panel web | `http://192.168.4.1` |
| Zona horaria | `-3` (Tucumán, sin horario de verano) |
| Récords | vacíos |

Estos datos se ven siempre en la **pantalla RED**, y también se pueden cambiar
desde el panel.

---

## 6. Conectar el equipo a tu WiFi de casa

El reloj sólo puede ponerse en hora solo si el equipo entra a internet.

1. En el celular o la notebook, conectate a la red **`ESP32-GADGET-XXXXXX`** con la
   contraseña **`12345678`**.

   > **Ojo acá, es el paso donde casi todos se traban.** Esa red **no tiene
   > internet** (el equipo no es un router), así que el celular te va a avisar
   > "esta red no tiene acceso a Internet" y va a querer volver a tus datos
   > móviles.
   >
   > - **iPhone**: tocá **Continuar** / **Usar sin Internet**. Si te pregunta si
   >   querés mantener la conexión, decí que sí.
   > - **Android**: elegí **Mantener conexión** o **Usar igual**. Si tenés
   >   "Cambio inteligente de red" o "Wi-Fi Assistant" activo, apagalo mientras
   >   configurás.
   >
   > Si el teléfono vuelve solo a tus datos, la página del panel no va a cargar.

2. Abrí **`http://192.168.4.1`** en el navegador. Escribí la dirección; no hace
   falta que la busques en Google.
3. En **WiFi de casa**, tocá **Buscar redes**, elegí la tuya y poné la contraseña.
4. **Guardar y conectar**.

En unos segundos la pantalla RED pasa a `EN LINEA` y el reloj toma la hora por
NTP. El firmware reinicia el pedido de hora en el momento exacto en que el WiFi
conecta, así que la hora aparece en segundos y no hay que esperar ni reiniciar.
El equipo queda conectado a las dos redes a la vez: la suya (para el panel) y la de
casa (para internet).

### Cómo entrar al panel una vez conectado a tu WiFi

Este es el punto que más confunde: **no tenés que desconectarte de tu WiFi de
casa para usar el panel**. El equipo atiende por los dos lados al mismo tiempo,
así que tenés dos direcciones y las dos funcionan:

| Estás conectado a… | Abrí |
|---|---|
| **Tu WiFi de casa** (lo normal) | la dirección que muestra la pantalla RED arriba, en grande: `http://<ip que te dio el router>`, por ejemplo `http://192.168.100.75` |
| **La red del equipo** (el AP `ESP32-GADGET-XXXXXX`) | `http://192.168.4.1` |

La primera es la cómoda: seguís con internet y con el panel a la vez. La segunda
sirve cuando estás en un lugar sin tu red, o si el equipo todavía no tiene WiFi
de casa configurado.

Esa IP se la asigna tu router por DHCP, así que **puede cambiar** si reiniciás el
router o el equipo queda mucho tiempo apagado. Cuando cambie, mirá la pantalla RED
otra vez, o pedile a tu router que le reserve siempre la misma dirección a la MAC
`80:45:6B:35:DA:38` (esa MAC también aparece en el panel, en **Estado**).

---

## 7. Cómo se usa cada pantalla

### Reloj

- Dígitos de siete segmentos, fondo negro, texto blanco.
- Sin encabezado ni barra de segundos: sólo la hora, grande, y la fecha debajo.
- Los segmentos apagados no se dibujan; no hay marco ni adornos alrededor.
- **Toque corto**: alterna 12 h / 24 h.
- El punto de los dos puntos parpadea una vez por segundo para mostrar que el reloj está vivo.
- Si todavía no hay hora, muestra `SIN HORA TODAVIA` en vez de una hora falsa.

### Tragamonedas

- **Toque corto**: gira. Cada tirada cuesta 10 créditos; arrancás con 100.
- Si te quedás sin créditos, un toque te suma 50 (la máquina nunca se traba).
- Tres iguales pagan según el símbolo (el 7 paga 500); dos iguales pagan 20.
- Al ganar, la luz RGB de atrás hace una celebración con los colores del casino, y
  el marco del carrete parpadea en pantalla. Tres sietes es **JACKPOT**.
- El mejor premio se guarda aunque apagues el equipo.

### Meteoritos

- **Toque corto**: disparar; en la pantalla de inicio o de fin, empezar o volver a jugar.
- **Dos toques rápidos** (inicio o fin de partida): abren el cartel **INICIAR CALIBRACION** con **SI / NO**. Un toque cambia la opción y **mantener 1 segundo** confirma. La calibración es guiada y se confirma con toques: centro, izquierda, derecha, hacia vos y alejando.
- Incliná el equipo para mover la nave por todo el espacio: izquierda/derecha y arriba/abajo; los meteoritos caen de arriba.
- Cada 10 segundos sube el nivel: los meteoritos caen más rápido y aparecen más seguido.
- **Cazas enemigos**: aparece uno cada tanto (el primero a los 6 segundos), se mueven de costado por la franja de arriba y **disparan hacia abajo**. Sólo los destruye **tu disparo** (150 puntos); sus balas y su cuerpo terminan la partida.
- Los cazas tienen **barra de vida**: en el nivel 1 caen de un disparo y cada dos niveles necesitan uno más, hasta 6. La barra aparece arriba del caza y se va vaciando con cada impacto.
- Cuanto más alto el nivel, los cazas **disparan más seguido** y sus balas **caen más rápido**.
- Sumás puntos por sobrevivir y 20–50 por cada meteorito destruido.
- Al chocar termina la partida y se guarda el **récord**.

### Sofía

- Una cara procedural: ojos, cejas y boca; parpadea y mira.
- Reacciona al estado del bridge de Hermes: `EN CALMA`, `PENSANDO`, `TRABAJANDO`, `ESPERANDO`, `ERROR`, `LISTO`. Si el bridge no responde: `SIN SEÑAL`; si falta configurar: `CONFIGURAME`.
- **Toque corto**: la saludás (cara contenta y destello).
- La luz RGB acompaña el estado. Por defecto **no entra** en el apagado automático.
- Se configura en el panel: **Sofía → URL del bridge + token**. El equipo nunca guarda la clave de Hermes.

### Red

- Nombre y contraseña del WiFi del equipo, en grande, para poder leerlos y conectarte.
- Dirección del panel: `http://192.168.4.1`.
- Estado del WiFi de casa, IP, señal y cantidad de clientes conectados.
- **Toque corto**: reintenta la conexión al WiFi de casa.

---

## 8. Panel de administración web

Conectate a la red del equipo y abrí **`http://192.168.4.1`**. El panel está en
español y pensado para usarse con el dedo, desde el celular.

| Sección | Para qué sirve |
|---|---|
| **Estado** | Los mismos datos de conexión que muestra la pantalla RED, más hora, tiempo encendido, memoria libre y qué pantalla está activa. Con el WiFi de casa conectado, la dirección del panel aparece con la IP que te asignó el router. |
| **WiFi de casa** | Busca redes, elegís la tuya, ponés la clave y guardás. Con eso el reloj se pone en hora solo. Si te equivocaste de clave, **Olvidar red** borra las credenciales sin tocar los récords. |
| **Punto de acceso** | Cambia el nombre y la clave de la red que crea el equipo (lo que se ve en la pantalla RED). Mínimo 8 caracteres. |
| **Pantalla** | Cambia de pantalla sin tocar el botón (incluida **Música**), configura el **apagado automático** de la pantalla y la luz, **renombra y reordena el menú**: escribís el nombre de cada pantalla (hasta 12 letras, se ve igual en la lista y en el equipo) y las movés con las flechas —ese orden es el que recorre el botón—; ambos cambios quedan guardados. También ajusta el brillo, elige 12/24 h, **corrige la orientación** si ves la imagen al revés, y tiene **Usar la hora de este teléfono** para poner el reloj en hora sin internet. |
| **Luz RGB** | Color, intensidad y modo (fijo, arcoíris, apagada) de la luz de atrás. |
| **Juegos** | Récords, borrarlos, y **calibrar los controles** del juego de meteoritos. |
| **Sofía** | URL y token del bridge local, estado en vivo del buddy y un campo para pedirle algo al agente: el pedido se ve en la cara. |
| **Spotify** | Los pasos para conectar tu cuenta (crear la app, el Redirect URI, el Client ID) y el estado en vivo: si falta el Client ID, si falta autorizar, o qué está sonando. |
| **Mantenimiento** | Reiniciar (no borra nada) o restablecer de fábrica (borra WiFi y récords). Al final hay **Probar colores de la pantalla**: la pone roja, verde, azul y blanca para verificar el panel. |

### Apagado automático

Sin tocar el equipo, la pantalla y la luz de atrás se apagan solas después del
tiempo elegido en **Pantalla → Apagado automático**. Cada pantalla tiene su
casilla: por defecto se apagan **Reloj** y **Red**; **Tragamonedas**,
**Meteoritos** y **Música** quedan siempre encendidas. En Meteoritos, mientras
estás jugando, calibrando o confirmando la calibración no se apaga aunque la
incluyas.

La pantalla **Sofía** arranca excluida del apagado: si querés que también duerma,
marcala en la lista. El menú de pantallas sigue la regla de la pantalla desde la
que lo abriste: si esa pantalla está incluida, el menú también se apaga.

Un **toque en BOOT** la vuelve a prender, y ese toque no dispara, no gira ni
cambia de pantalla. Al prenderla, el brillo y el color de la luz quedan como los
tenías.

### La pantalla del equipo

El panel también sirve para manejar el equipo a distancia: los botones
*Simular toque* y *Simular mantener 3s* hacen lo mismo que el botón BOOT.

## 9. Orientación y controles del juego

Las dos cosas que dependen de cómo apoyás el equipo se ajustan **desde el panel**,
sin recompilar ni usar la consola.

### Si la imagen se ve girada

En **Pantalla → Orientación de la pantalla** elegí *Horizontal* o
*Horizontal invertida*. Queda guardado.

### Si la nave no se mueve como esperás

El juego mide los ejes del sensor, porque según cómo sostengas la placa la
gravedad puede caer en cualquiera de los tres ejes y los dos ejes del plano
pueden quedar cruzados. La calibración es guiada y se confirma con toques, sin apuro ni relojes:

1. **Dejalo en el centro**, como para jugar, y **tocá el botón** para confirmar.
2. **Incliná hacia la izquierda**, volvé al centro y **tocá** para confirmar.
3. Lo mismo hacia la **derecha**.
4. Lo mismo con el borde de arriba **hacia vos**.
5. Lo mismo **alejando** el borde de arriba.

El cartel te va guiando: `AHORA IZQUIERDA` → `VOLVE AL CENTRO` → `TOCA PARA
CONFIRMAR`. El toque sólo cuenta si hubo una inclinación de verdad y el equipo ya
volvió al centro; si te apurás, no pasa nada y el cartel te sigue pidiendo. Los
cuatro puntos muestran lo que ya quedó confirmado. Al terminar, el equipo deduce
solo qué eje es horizontal, cuál es vertical y para qué lado va cada uno.

Para cancelar a mitad de camino y quedarte con la calibración anterior, **mantené
el botón** un segundo.

La primera vez que tocás la pantalla METEORITOS te pide el gesto directamente;
después, para recalibrar, usá **dos toques rápidos** o el botón del panel.

Después de eso, si algo no cierra, en **Juegos** tenés:

| Botón | Cuándo usarlo |
|---|---|
| **Recalibrar controles** | Vuelve a hacer la calibración guiada de cinco pasos. |
| **Cambiar eje del control** | Si la nave responde al revés de lo que esperás en los costados, sin repetir la calibración. |
| **Invertir izquierda/derecha** | Si la nave va para el lado contrario al que inclinás a los costados. |
| **Invertir arriba/abajo** | Si al inclinar el equipo hacia adelante la nave sube cuando querés que baje (o al revés). |

## 10. Conectar Spotify (opcional)

La pantalla **Música** muestra lo que estás escuchando en tu cuenta de Spotify.
Necesita dos cosas: que el equipo esté conectado a tu WiFi de casa (§6) y un
permiso que se da una sola vez.

> El equipo **no reproduce audio**. Solo muestra qué suena. No hace falta Spotify
> Premium.

### 10.1 Crear la app en Spotify

1. Entrá a [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard)
   con tu cuenta y creá una app. El nombre da igual.
2. Dentro de la app, agregá como **Redirect URI** esta dirección, copiada tal
   cual:

   ```
   http://127.0.0.1:8888/callback
   ```

3. Copiá el **Client ID** que muestra la app.

No hace falta el *Client Secret*, y no lo pongas en el equipo: el flujo que usa
este firmware (PKCE) no lo necesita.

### 10.2 Autorizar el equipo

1. En el panel web, sección **Spotify**, pegá el **Client ID** y tocá
   **Guardar Client ID**.
2. Tocá **Autorizar** y abrí el enlace que aparece, en la computadora.
3. Entrá con tu cuenta y tocá **Aceptar**.
4. El navegador va a intentar volver a `127.0.0.1` y va a mostrar un **error de
   conexión**. Es esperado: esa dirección vive en tu computadora, no en el
   equipo.
5. Sin cerrar esa pestaña, copiá **toda la dirección** de la barra (empieza con
   `http://127.0.0.1:8888/callback?code=...`). También sirve copiar sólo lo que
   está después de `code=` y antes del próximo `&`: el equipo entiende las dos
   formas y saca el código solo.
6. Pegalo en **Código de autorización** y tocá **Enviar código**.

Listo. El equipo guarda el permiso y lo renueva solo. Si alguna vez lo revocás
desde tu cuenta de Spotify, tocá **Olvidar autorización** y repetí el paso 2.

### 10.3 Si no ves la canción

La pantalla **Música** te dice qué falta, en lugar de quedarse en blanco:

| Lo que dice | Qué significa |
|---|---|
| **SIN CONFIGURAR** | Falta el Client ID. Pegalo en el panel. |
| **FALTA AUTORIZAR** | Está el Client ID pero no el permiso. Paso 2 de §10.2. |
| **SIN WIFI** | El equipo no está conectado a tu red (§6), así que no tiene internet. |
| **NADA SONANDO** | Está todo bien, pero Spotify no está reproduciendo nada. |
| **ERROR** | El texto de abajo dice qué pasó (por ejemplo, el límite de consultas de Spotify). |

### 10.4 Dependencia nueva

La carátula se decodifica en el equipo, así que además de *Adafruit NeoPixel* y
*ArduinoJson* (§2.2) hace falta:

```powershell
arduino-cli lib install JPEGDEC
```

## 11. Consola serie (referencia rápida)

El firmware trae una consola pensada para verificar el equipo sin depender del
panel web. Abrí el monitor serie a **115200** baudios y escribí `help`.

| Comando | Qué hace |
|---|---|
| `status` | Estado completo: pantalla, orientación, WiFi, hora, ejes del juego, récords, memoria. |
| `screen 0..4` | Salta a una pantalla (0 reloj, 1 tragamonedas, 2 meteoritos, 3 red, 4 música). |
| `press [ms]` | Simula un toque corto. |
| `doubletap` | Dos toques rápidos seguidos: abre el cartel de calibración del juego. |
| `tap` | Toque corto sin la temporización del botón (para probar la lógica de la pantalla). |
| `longpress` | Mantener 1 segundo sin la temporización del botón. |
| `hold` | Simula mantener 1 segundo: abre el menú, o entra a la pantalla seleccionada. |
| `order [a b c d e]` | Muestra o fija el orden del menú (ids de pantalla). |
| `orient 0..3` | Rotación de la imagen al panel. |
| `steer <lat> <up> <sign> <signY> <gain>` | Fija a mano los ejes del juego (0=x, 1=y, 2=z) y el sentido de cada dirección. |
| `steercal` | Borra la calibración: el próximo toque en METEORITOS calibra de nuevo. |
| `imu` | Una lectura del acelerómetro y del giroscopio. |
| `imufake <ax> <ay> <az> <gx> <gy> <gz>` | Reemplaza el sensor por valores fijos (para probar el juego sin mover la placa). |
| `imureal` | Vuelve al sensor real. |
| `shippos` | Estado y posición de la nave en la grilla (columna y fila). |
| `settime <epoch>` | Fija el reloj (sirve para probar la esfera sin WiFi). |
| `tz <horas>` | Zona horaria, por ejemplo `tz -3`. |
| `bl <0-100>` | Brillo de la pantalla. |
| `colors` | Prueba de panel a pedido: pantalla entera roja, verde, azul y blanca (ya no corre al encender). |
| `rgb <r> <g> <b> [nivel]` | Color del LED. |
| `dump [paso]` | **Dibuja el framebuffer en ASCII por la consola.** Es la forma de ver qué hay en pantalla sin cámara. |
| `api GET /api/status` | Llama al panel web sin necesidad de red. |
| `api POST /api/scan` | Inicia un barrido de redes; `api GET /api/scan` lee el resultado. |
| `spotify` | Estado de Spotify: client id, permiso, última consulta y qué suena. |
| `spotify id <client_id>` | Guarda el Client ID de tu app (lo mismo que el panel). |
| `spotify url` | Imprime la URL de autorización para abrir en la computadora. |
| `spotify code <code o URL>` | Canjea el código. Pegá la dirección completa o sólo el `code=`: lo parsea el equipo. |
| `spotify poll` | Fuerza una consulta a Spotify ahora. |
| `spotify forget [all]` | Borra el permiso (o también el Client ID con `all`). |
| `reboot` | Reinicia. |

## 11. Problemas comunes

**El monitor serie no muestra nada.**
`USB CDC On Boot` tiene que estar en **Enabled**. Además, el firmware descarta las
escrituras si no hay nadie leyendo: abrí el monitor y apretá RESET.

**No aparece ningún puerto COM.**
Cable USB-C de datos y, si hace falta, instalá el driver. La placa se identifica
como `VID_303A:PID_1001`.

**Arranca en bucle de reinicios.**
Casi siempre es **PSRAM**: tiene que ser **OPI PSRAM**, no QSPI. Revisá también que
el Flash Size sea 16MB.

**La pantalla se ve girada.**
`orient 2` por consola.

**El WiFi dice conectado pero el reloj sigue en `SIN HORA TODAVIA`.**
Algunos routers bloquean el puerto 123 (NTP) o no resuelven nombres, y entonces
la hora no llega nunca. No hace falta tocar el router: en el panel, **Pantalla →
Usar la hora de este teléfono** le pasa la hora del celular con un toque. El
reloj queda en hora hasta que lo desenchufes; si más adelante NTP funciona, se
corrige solo.

**El reloj no toma la hora.**
Falta configurar el WiFi de casa desde el panel, o el router está bloqueando el
puerto 123 (NTP). Mientras tanto el reloj avisa que no tiene hora en vez de mentir.

**El LED RGB muestra otro color.**
Este LED es `NEO_RGB`, no `NEO_GRB`. Está fijado en `rgb.cpp`; si tu revisión de
placa lo cambió, es la única línea a tocar.

**El equipo no conecta al WiFi de casa y no sé por qué.**
En el panel, **WiFi de casa → ¿Por qué no conecta?**. El equipo lee el motivo real
del rechazo y te lo traduce:

| Motivo | Qué significa |
|---|---|
| `15` / `204` / `202` / `2` | El router aceptó la conexión pero falló el intercambio de claves: **la contraseña es incorrecta**. |
| `8` / `203` | El router **rechazó al equipo**: casi siempre filtrado por MAC. Agregá la MAC que muestra *Estado* a la lista blanca del router. |
| `201` / `210` / `211` | No se encontró la red: revisá el nombre o que no sea solo de 5 GHz. |
| `200` | Se perdió la señal: acercate al router. |

**Me equivoqué de contraseña del WiFi de casa.**
En el panel, **WiFi de casa → Olvidar red**. Borra las credenciales y hace que el
equipo deje de reintentar, sin tocar los récords ni el nombre del punto de
acceso. Después cargás la clave correcta.

**No puedo entrar a `http://192.168.4.1`.**
Casi siempre es que el teléfono volvió solo a los datos móviles porque la red del
equipo no tiene internet. Ver el aviso del paso 6: elegí *Mantener conexión* /
*Usar sin Internet* y reintentá. Fijate también que la barra de WiFi del teléfono
siga mostrando `ESP32-GADGET-XXXXXX` y no tu red de casa.

**La nave se mueve con el eje equivocado.**
En el panel, **Juegos → Cambiar eje del control**. Si igual no responde, tocá
**Recalibrar controles** y repetí el gesto subiendo y bajando los extremos
izquierdo y derecho, no el borde de adelante.

**La imagen de la pantalla se ve girada.**
En el panel, **Pantalla → Orientación de la pantalla**. O por consola: `orient 2`.

**Cambié el panel web y no se ve.**
Faltó volver a correr `tools\pack_web.py` antes de compilar (ver paso 4).

---

## 12. Volver al firmware original

El equipo no se puede "brickear": el bootloader de ROM y el botón BOOT siempre
permiten volver a flashear. Para dejar la placa como vino, descargá el firmware de
prueba de Waveshare y flashealo como imagen completa en `0x0`.

---

## Estructura del proyecto

```
esp32_gadget/
├── README.md               resumen del proyecto
├── INSTALL.md              este documento
├── esp32_gadget.ino        arranque, botón, consola serie
├── display.h/.cpp          driver ST7789 + giro a horizontal
├── gfx.h/.cpp              framebuffer, formas, texto, dígitos de 7 segmentos
├── font5x7.h               fuente (generada por tools/gen_font.py)
├── button.h                máquina de estados del botón (toque / 3 s)
├── settings.h/.cpp         credenciales y ajustes en NVS
├── rgb.h/.cpp              LED WS2812 y celebración de casino
├── imu.h/.cpp              QMI8658 (acelerómetro + giroscopio)
├── screens.h/.cpp          registro de pantallas y marco común
├── screen_name.h           reglas de los nombres elegidos por el dueño
├── screen_clock.cpp        pantalla reloj
├── screen_slot.cpp         pantalla tragamonedas
├── screen_game.cpp         pantalla meteoritos (ASCII)
├── screen_net.cpp          pantalla de red
├── net.h/.cpp              WiFi, NTP, API y servidor web
├── web_assets.h            panel web empaquetado (generado)
└── web_ui/                 panel web: index.html, styles.css, app.js
```

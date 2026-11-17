# ESP32 Gadget — cómo instalarlo

Un solo firmware para la **Waveshare ESP32-S3-LCD-1.47B** que convierte la placa
en un equipo con siete pantallas y un panel de administración web:

| Pantalla | Qué hace |
|---|---|
| **Reloj** | Hora digital (GMT-3, Tucumán, Argentina) en fondo negro, dígitos blancos de siete segmentos. |
| **Tragamonedas** | Juego de casino con botón físico. Al ganar, la luz RGB de atrás hace una celebración. |
| **Meteoritos** | Juego ASCII de naves y meteoritos, con movimiento libre en dos ejes por sensor. Guarda el récord. |
| **Red** | Muestra el nombre y la contraseña del WiFi del equipo, y la dirección del panel web. |
| **Música** | Qué estás escuchando en Spotify: carátula, tema, artista y progreso. Necesita que conectes el equipo a tu WiFi y autorices tu cuenta una vez. |
| **Sofía** | Buddy companion: una cara procedural que reacciona a lo que hace Hermes (pensando, trabajando, esperando, error, listo) a través de su bridge local, y al giroscopio si la sacudís. |
| **Config** | Brillo del LCD, brillo del LED trasero, rotación de la imagen y bloqueo, sin tocar el panel web. Los valores quedan guardados en el equipo. |

Todo se maneja con **un solo botón**: el **BOOT**.

La interfaz del equipo y del panel está **en español**.

- **Toque corto** → interactúa con la pantalla actual (girar el tragamonedas, disparar, cambiar el reloj a 12/24 h, reintentar el WiFi).
- **Mantener 1 segundo** → abre el **menú de pantallas**, con el nombre grande y centrado.
  Dentro del menú: **un toque** pasa a la siguiente, **mantener 1 segundo** entra a la que está
  seleccionada. Una barra dorada abajo muestra cuánto falta para que se abra (o para entrar).
  Los nombres y el orden de ese menú se cambian desde **Aplicaciones → Nombres y orden** en el panel web.

La pantalla **Config** es la única con dos gestos de toque. Cada fila de brillo tiene un
**−** y un **+** al lado de la barra: **dos toques rápidos** mueven la marca al control
siguiente (`−` del LCD, `+` del LCD, `−` del LED, `+` del LED, **ROTACION**, **BLOQUEO**, y
vuelve al principio) y **un toque** aprieta el control marcado. La rotación se recorre con
un toque (automática, horizontal, invertida) y el bloqueo se prende y apaga igual. Un `+`
en el tope se queda ahí y un `−` en el piso también: el valor nunca salta al otro extremo,
así que no hay forma de dejar la pantalla casi apagada sin querer. Como el toque tiene que
esperar a ver si viene el segundo, el cambio se aplica cuando termina esa espera (menos de
medio segundo). Mantener 1 segundo sigue abriendo el menú de pantallas, así que esa
pantalla nunca te deja encerrado.

La rotación se puede cambiar desde la pantalla sin salir de ella: la imagen se vuelve a
mandar al panel y la fila marcada no se pierde.

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

### Antes de compilar: tu usuario y contraseña del panel

El panel de administración pide usuario y contraseña. La primera vez salen de un
archivo `.env` en la raíz del repositorio: copiá `.env.example` a `.env` y elegí
los valores.

```powershell
copy .env.example .env      # después editá .env con tu usuario y contraseña
python tools\gen_admin_defaults.py
```

Ese comando genera `esp32_gadget/admin_seed.h` (ignorado por git) con una **sal y
un hash**: la contraseña en texto plano nunca entra al firmware, ni a la memoria
del equipo, ni al repositorio. Si te olvidás de este paso, el equipo compila igual
con `admin` / `admin` y el panel te obliga a cambiarla en el primer ingreso.

Para cambiar el usuario y la contraseña **más adelante no hace falta recompilar**:
se hace desde el panel, en la sección **Cuenta**. Editá el `.env` sólo si querés
cambiar con qué credenciales arranca un equipo recién flasheado o restablecido de
fábrica.

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
| Panel web | `http://192.168.4.1/admin` |
| Usuario del panel | el `ADMIN_USER` del `.env` (o `admin` si compilaste sin `.env`) |
| Contraseña del panel | el `ADMIN_PASS` del `.env` (o `admin`, que el panel obliga a cambiar) |
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
- **Calibración de la inclinación**: se hace desde **CONFIG → CALIBRAR** (§9). El juego ya no la pide; si nunca calibraste, la pantalla de inicio muestra el aviso y el juego arranca con los ejes guardados (o los de fábrica).
- Incliná el equipo para mover la nave por todo el espacio: izquierda/derecha y arriba/abajo; los meteoritos caen de arriba.
- Cada 10 segundos sube el nivel: los meteoritos caen más rápido y aparecen más seguido.
- **Cazas enemigos**: aparece uno cada tanto (el primero a los 6 segundos), se mueven de costado por la franja de arriba y **disparan hacia abajo**. Sólo los destruye **tu disparo** (150 puntos); sus balas y su cuerpo terminan la partida.
- Los cazas tienen **barra de vida**: en el nivel 1 caen de un disparo y cada dos niveles necesitan uno más, hasta 6. La barra aparece arriba del caza y se va vaciando con cada impacto.
- Cuanto más alto el nivel, los cazas **disparan más seguido** y sus balas **caen más rápido**.
- Sumás puntos por sobrevivir y 20–50 por cada meteorito destruido.
- Al chocar termina la partida y se guarda el **récord**.

### Sofía

- Una cara procedural: ojos, cejas y boca; parpadea y mira.
- Reacciona al estado del bridge de Hermes: `EN CALMA`, `PENSANDO`, `TRABAJANDO`, `ESPERANDO`, `ERROR`, `LISTO`.
- Si el bridge no contesta, en lugar de los ojos aparece el **ícono de WiFi con un `!`** que late: la cara no tiene nada que mostrar y unos ojos apagados se leían como aburrimiento. En el panel, **Sofía → Enlace** dice el motivo: sin hora sincronizada, certificado rechazado, bloqueado por Cloudflare, token rechazado o sin respuesta.
- Si todavía no configuraste la URL o el token, la cara se queda en calma y atenuada: no es una falla, es que falta emparejarla.
- **Toque corto**: la saludás (cara contenta y destello).
- La luz RGB acompaña el estado. Por defecto **no entra** en el apagado automático.
- Se configura en el panel: **Sofía → URL del bridge + token**. El equipo nunca guarda la clave de Hermes.
- La URL puede ser la de tu LAN (`http://192.168.100.64:8787`) o una pública con **HTTPS** (`https://bridge.tu-dominio.com`). Con HTTPS el equipo valida el certificado solo, contra las autoridades que ya trae el firmware: no hay que subir ningún archivo.

#### Bridge por HTTPS

Publicar el bridge con un túnel (por ejemplo `cloudflared`) es suficiente: el
panel muestra **Enlace: Conectado** sin configurar nada más. Hacen falta dos
cosas del lado del equipo:

- **WiFi de casa conectado**, porque validar el certificado necesita DNS y la
  hora en hora. Si el reloj todavía no se sincronizó, el panel dice **Enlace: Sin
  hora sincronizada** y no intenta el pedido.
- **Cloudflare sin desafíos sobre ese hostname.** Bot Fight Mode y Cloudflare
  Access responden con un desafío de JavaScript o un login, cosas que este
  firmware no puede resolver. Si eso pasa, el panel muestra **Enlace: Bloqueado
  por Cloudflare**: excluí el hostname de esas funciones, o hacé que el bridge
  valide un token en lugar de pasar por Access.

El token del buddy ahora viaja por internet: usá uno largo y cambialo si se
filtró. Es lo único que separa a cualquiera de pedirle cosas al agente.

### Red

- Nombre y contraseña del WiFi del equipo, en grande, para poder leerlos y conectarte.
- Dirección del panel: `http://192.168.4.1`.
- Estado del WiFi de casa, IP, señal y cantidad de clientes conectados.
- **Toque corto**: reintenta la conexión al WiFi de casa.

---

## 8. Panel de administración web

### Las dos entradas: `/` y `/admin`

| Dirección | Qué hay | ¿Pide contraseña? |
|---|---|---|
| `http://192.168.4.1/` | La entrada pública. Ahí va a vivir el **selector de pantalla** y los juegos que hacen reaccionar al buddy. Hoy avisa que está en camino. | No |
| `http://192.168.4.1/admin` | **Todas las configuraciones**: pantalla, luz, juegos, música, red, Sofía, sistema y cuenta. | Sí |
| `http://192.168.4.1/login` | La puerta del panel. | — |

Al abrir `/admin` sin sesión, el equipo te manda a `/login` y después te devuelve
a la página que pediste. La sesión vive doce horas y se guarda en una cookie que
el navegador no deja leer a los scripts; si el equipo se reinicia, hay que entrar
de nuevo.

### Usuario y contraseña

- Vienen del `.env` con el que se compiló el firmware (ver paso 4).
- Se cambian desde **Cuenta**, en el panel, pidiendo la contraseña actual. El
  equipo guarda un hash con sal: no hay forma de leer la contraseña desde el panel
  ni desde la consola serie.
- Cambiarlas cierra la sesión de los demás dispositivos que tengan el panel
  abierto, no la tuya.
- **Salir** está arriba a la derecha y también en la tarjeta **Cuenta**.

> **Ojo con la red.** El panel viaja por HTTP simple dentro de la red del equipo:
> no hay TLS, porque el equipo no tiene un certificado válido. Sirve para la red
> del propio gadget o tu casa; no lo expongas a internet. Las credenciales nunca
> viajan en la URL ni quedan guardadas en el navegador más allá de la cookie de
> sesión.

### Adentro del panel

El panel está en español y pensado para usarse con el dedo, desde el celular. En
el celular las secciones se abren desde el botón **☰** de arriba; en la
computadora el menú queda fijo a la izquierda.

| Sección | Para qué sirve |
|---|---|
| **Aplicaciones** (primera) | Elegís qué se ve en la pantalla del equipo tocando una tarjeta; la activa queda marcada como **Ahora**. Debajo, plegado, está **Nombres y orden del menú**: escribís el nombre de cada pantalla (hasta 12 letras, se ve igual en la lista y en el equipo) y las movés con las flechas — ese orden es el que recorre el botón—; ambos cambios quedan guardados. |
| **Pantalla** | Brillo, color de la interfaz, rotación automática (o fija), bloqueo de la rotación, reloj en 12/24 h, **Usar la hora de este teléfono** para ponerlo en hora sin internet y el **apagado automático** de la pantalla y la luz. |
| **Luz RGB** | Intensidad en porcentaje (0 a 100, queda guardada en el equipo), color y modo (fijo, arcoíris, apagada) de la luz de atrás. Si **Sofía** está configurada, ella usa esa luz como luz de estado: la tarjeta lo dice y deja solo la intensidad, porque el color y el modo los decide el bridge. |
| **Juegos** | Récords, borrarlos, **recalibrar los controles** del juego de meteoritos (te lleva a CONFIG y abre el wizard) y corregir los ejes si la nave va al revés. |
| **Música** | Los pasos para conectar Spotify (crear la app, el Redirect URI, el Client ID) y el estado en vivo: si falta el Client ID, si falta autorizar, o qué está sonando. |
| **Red** | **WiFi de casa**: busca redes, elegís la tuya, ponés la clave y guardás; con eso el reloj se pone en hora solo. Si te equivocaste de clave, **Olvidar red** borra las credenciales sin tocar los récords. **Punto de acceso**: cambia el nombre y la clave de la red que crea el equipo (lo que se ve en la pantalla RED); mínimo 8 caracteres. |
| **Sofía** | URL del bridge (tu LAN por `http://` o una URL pública por `https://`) y token, estado en vivo del buddy con el motivo del enlace cuando falla, color de los ojos (elegilo o volvé al cyan original; queda guardado en el equipo) y un campo para pedirle algo al agente: el pedido se ve en la cara. |
| **Sistema** (última) | **Estado del dispositivo**: los mismos datos de conexión que muestra la pantalla RED, más hora, tiempo encendido, memoria libre y qué pantalla está activa. **Mantenimiento**: reiniciar (no borra nada) o restablecer de fábrica (borra WiFi y récords); **Probar el equipo** con los botones que simulan el BOOT y **Probar colores de la pantalla**, que la pone roja, verde, azul y blanca para verificar el panel. |

### Apagado automático

Sin tocar el equipo, la pantalla y la luz de atrás se apagan solas después del
tiempo elegido en **Pantalla → Apagado automático**. Cada pantalla tiene su
casilla: por defecto se apagan **Reloj** y **Red**; **Tragamonedas**,
**Meteoritos** y **Música** quedan siempre encendidas. En Meteoritos, mientras
estás jugando no se apaga aunque la incluyas; en CONFIG, el wizard de calibración
también mantiene la pantalla encendida y la rotación bloqueada mientras medís.

La pantalla **Sofía** arranca excluida del apagado: si querés que también duerma,
marcala en la lista. **Config** también arranca excluida, porque es la pantalla en la
que estás mirando el brillo. El menú de pantallas sigue la regla de la pantalla desde la
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

El equipo **gira solo** según cómo lo apoyes: el acelerómetro elige entre las dos
orientaciones horizontales y la imagen las sigue sin que toques nada. Tres cosas
lo pueden detener:

- **Automática / Horizontal / Horizontal invertida** en el panel, o la fila
  `ROTACION` de la pantalla Config: si elegís una de las dos horizontales, la
  imagen queda fija ahí y el sensor no opina más.
- **Bloquear la rotación actual** en el panel, o la fila `BLOQUEO` de Config: se
  congela lo que hay en pantalla y se libera cuando lo apagás.
- Un juego en curso: mientras jugás, la imagen nunca cambia sola, porque estás
  inclinando el equipo a propósito.

Un límite honesto: **acostado de espaldas sobre la mesa, el sensor no puede notar
un giro de 180 grados** (la gravedad queda perpendicular al plano y no hay nada en
el plano que mirar). En ese caso el equipo se queda con la última orientación en
vez de adivinar; si lo querés siempre igual, elegí Horizontal o bloqueá la
rotación. Todo queda guardado y sobrevive a un corte de luz.

### Si la nave no se mueve como esperás

El juego mide los ejes del sensor, porque según cómo sostengas la placa la
gravedad puede caer en cualquiera de los tres ejes y los dos ejes del plano
pueden quedar cruzados. La calibración es guiada, se confirma con toques y vive
en **CONFIG**, no en el juego:

1. En **CONFIG**, con dos toques movés el foco hasta la fila **CALIBRAR** y con
   un toque la abrís. El wizard ocupa la pantalla entera.
2. **Dejalo en el centro**, como para jugar, y **tocá el botón** para confirmar.
3. **Incliná hacia la izquierda**, volvé al centro y **tocá** para confirmar.
4. Lo mismo hacia la **derecha**.
5. Lo mismo con el borde de arriba **hacia vos**.
6. Lo mismo **alejando** el borde de arriba.

El cartel te va guiando: `AHORA IZQUIERDA` → `VOLVE AL CENTRO` → `TOCA PARA
CONFIRMAR`. El toque sólo cuenta si hubo una inclinación de verdad y el equipo ya
volvió al centro; si te apurás, no pasa nada y el cartel te sigue pidiendo. Los
cuatro puntos muestran lo que ya quedó confirmado. Al terminar, el equipo deduce
solo qué eje es horizontal, cuál es vertical y para qué lado va cada uno, lo
guarda y te devuelve a CONFIG con la fila en **HECHA**.

Para cancelar a mitad de camino y quedarte con la calibración anterior, **mantené
el botón** un segundo.

Desde el panel, **Juegos → Recalibrar controles** borra la anterior, te lleva a
CONFIG y abre el wizard directamente.

Después de eso, si algo no cierra, en **Juegos** tenés:

| Botón | Cuándo usarlo |
|---|---|
| **Recalibrar controles** | Abre el wizard de cinco pasos en CONFIG (borra la calibración anterior). |
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

1. En el panel web, sección **Música**, pegá el **Client ID** y tocá
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
| `screen 0..6` | Salta a una pantalla (0 reloj, 1 tragamonedas, 2 meteoritos, 3 red, 4 música, 5 Sofía, 6 Config). |
| `press [ms]` | Simula un toque corto. |
| `doubletap` | Dos toques rápidos por la ruta de la pantalla actual (en CONFIG mueve el foco). |
| `tap` | Toque corto sin la temporización del botón (para probar la lógica de la pantalla). |
| `longpress` | Mantener 1 segundo sin la temporización del botón. |
| `hold` | Simula mantener 1 segundo: abre el menú, o entra a la pantalla seleccionada. |
| `order [a b c d e f g]` | Muestra o fija el orden del menú (los siete ids de pantalla). |
| `orient 0..3` | Rotación de la imagen al panel. Queda **fija**: apaga la rotación automática. |
| `rot` | Muestra el modo de rotación (automática o fija), el bloqueo y la imagen actual. |
| `rot auto` | Vuelve a la rotación automática por acelerómetro. |
| `rot 0..3` | Fija la imagen, igual que `orient`. |
| `rot lock on\|off` | Bloquea o libera la rotación actual. |
| `steer <lat> <up> <sign> <signY> <gain>` | Fija a mano los ejes del juego (0=x, 1=y, 2=z) y el sentido de cada dirección. |
| `steercal` | Borra la calibración: se rehace desde CONFIG, fila CALIBRAR. |
| `imu` | Una lectura del acelerómetro y del giroscopio. |
| `imufake <ax> <ay> <az> <gx> <gy> <gz>` | Reemplaza el sensor por valores fijos (para probar el juego sin mover la placa). |
| `imureal` | Vuelve al sensor real. |
| `ball [on\|off]` | La pelotita roja del estado idle de Sofía: el mismo interruptor que el doble toque. |
| `shippos` | Estado y posición de la nave en la grilla (columna y fila). |
| `settime <epoch>` | Fija el reloj (sirve para probar la esfera sin WiFi). |
| `tz <horas>` | Zona horaria, por ejemplo `tz -3`. |
| `bl <0-100>` | Brillo de la pantalla. Queda guardado, igual que desde el panel o desde la pantalla Config. |
| `colors` | Prueba de panel a pedido: pantalla entera roja, verde, azul y blanca (ya no corre al encender). |
| `rgb <r> <g> <b> [nivel]` | Color del LED. El `nivel` (0-255) queda guardado en el equipo. |
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

## 12. Problemas comunes

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
Elegí *Horizontal invertida* en **Pantalla → Rotación de la pantalla**, o dejala en
*Automática* para que el acelerómetro decida. Por consola: `orient 2`.

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

**Olvidé el usuario o la contraseña del panel.**
No hay forma de recuperarla: el equipo guarda un hash, no la contraseña. En el
panel, **Sistema → Restablecer de fábrica** (o `factory` por consola serie) vuelve
a las credenciales del `.env` con el que se compiló el firmware —o a `admin` /
`admin` si se compiló sin `.env`—, que el panel vuelve a pedir cambiar. El reset
también borra el WiFi de casa y los récords.

> Si además perdiste el acceso al panel, el botón BOOT no borra nada: el reset de
> fábrica se hace desde el panel o por consola serie, y para llegar a la consola
> alcanza con el cable USB.

**No puedo entrar a `http://192.168.4.1`.**
Casi siempre es que el teléfono volvió solo a los datos móviles porque la red del
equipo no tiene internet. Ver el aviso del paso 6: elegí *Mantener conexión* /
*Usar sin Internet* y reintentá. Fijate también que la barra de WiFi del teléfono
siga mostrando `ESP32-GADGET-XXXXXX` y no tu red de casa.

**La nave se mueve con el eje equivocado.**
En el panel, **Juegos → Cambiar eje del control**. Si igual no responde, tocá
**Recalibrar controles** (te abre el wizard en CONFIG) y repetí el gesto
inclinando y volviendo al centro en cada dirección, sin apuro.

**La imagen de la pantalla se ve girada.**
En el panel, **Pantalla → Rotación de la pantalla**: *Automática*, *Horizontal* o
*Horizontal invertida*. Bloquearla deja fija la que esté en pantalla. Por consola:
`orient 2`, o `rot auto` para volver al sensor.

**Cambié el panel web y no se ve.**
Faltó volver a correr `tools\pack_web.py` antes de compilar (ver paso 4).

---

## 13. Volver al firmware original

El equipo no se puede "brickear": el bootloader de ROM y el botón BOOT siempre
permiten volver a flashear. Para dejar la placa como vino, descargá el firmware de
prueba de Waveshare y flashealo como imagen completa en `0x0`.

---

## Estructura del proyecto

```
esp32_gadget/
├── README.md               resumen del proyecto
├── INSTALL.md              este documento
├── esp32_gadget.ino        arranque, botón y lazo principal
├── console.h/.cpp          consola serie: comandos, volcados y diagnóstico
├── display.h/.cpp          driver ST7789 + giro a horizontal
├── gfx.h/.cpp              framebuffer, formas, texto, dígitos de 7 segmentos
├── font5x7.h               fuente (generada por tools/gen_font.py)
├── button.h                máquina de estados del botón (toque / 1 s)
├── settings.h/.cpp         credenciales y ajustes en NVS
├── rgb.h/.cpp              LED WS2812 y celebración de casino
├── rotation.h/.cpp         rotación automática, bloqueo y compensación del sensor
├── rotation_policy.h       cuándo girar la imagen y cuándo quedarse quieto (puro, test en host)
├── imu.h/.cpp              QMI8658 (acelerómetro + giroscopio)
├── screens.h/.cpp          registro de pantallas y marco común
├── config_value.h          pasos, topes y mapeo de brillo (puro, test en host)
├── screen_name.h           reglas de los nombres elegidos por el dueño
├── screen_order.h          validación del orden del menú (puro, test en host)
├── screen_clock.cpp        pantalla reloj
├── screen_slot.cpp         pantalla tragamonedas
├── screen_game.cpp         pantalla meteoritos (ASCII)
├── steer_solve.h           ejes y sentidos a partir de cuatro inclinaciones (puro, test en host)
├── steer_wizard.h          wizard de calibración: centro + cuatro direcciones (puro, test en host)
├── calibration_gate.h      cuándo vale confirmar un paso de la calibración (puro, test en host)
├── screen_net.cpp          pantalla de red
├── screen_music.cpp        pantalla de música (Spotify)
├── screen_buddy.cpp        pantalla Sofía (cara del buddy)
├── screen_config.cpp       pantalla Config (brillo, rotación y bloqueo)
├── buddy_*.h               estados, fases, expresiones y reacciones de Sofía
├── spotify_*.h/.cpp        OAuth PKCE, consulta, parser y carátula
├── ui_color.h, theme.h     paleta compartida
├── admin_auth.h/.cpp       credenciales, sesiones y qué rutas piden sesión (puro, test en host)
├── net.h/.cpp              WiFi, NTP, API y servidor web
├── admin_seed.h            credenciales del .env: sal y hash (generado, no versionado)
├── web_assets.h            panel web empaquetado (generado)
└── web_ui/                 panel web: index.html (entrada), admin.html (panel),
                            login.html + login.js (entrada), styles.css, app.js
```

La arquitectura, las convenciones y el paso a paso para agregar una pantalla, un
comando o un endpoint están en [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md).
Para correr todas las pruebas de host: `python tools/run_tests.py`.

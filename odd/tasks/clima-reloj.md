# Clima en el reloj: 24 h fijas, toque que muestra el clima e integración Open-Meteo

El reloj del vidrio pasa a 24 h fijas y el toque corto deja de alternar el formato
para mostrar el clima de la ubicación configurada en el panel. La ubicación se
resuelve desde `/admin` (Integraciones → Clima) con un botón que la detecta en el
equipo y un buscador de ciudad para fijarla con precisión. El clima lo baja la
placa por su cuenta con Open-Meteo, que no pide credenciales.

## Specs

S1. Reloj siempre en 24 h — verbatim: "quiero que sea siempre 24hs". La cara del
reloj no dibuja AM/PM ni forma de 12 h bajo ninguna condición: la hora se compone
siempre de `tm_hour` 0..23. El pie ya no ofrece cambiar el formato.

S2. Se depreca el cambio de formato — verbatim: "el boton cuando lo tocas una vez,
no cambie el tipo de formato de hora (justamente vamos a deprecar esa
funcionalidad)". El toque corto no toca `clock24h`; el panel pierde el interruptor
"Reloj en formato 24 horas"; `/api/settings` deja de aceptar `clock24h` y
`/api/status` deja de publicarlo. El campo sigue existiendo como marcador
deprecado en el blob `Settings` para no cambiar su tamaño: el blob se migra por
tamaño y quitar el campo borraría las credenciales guardadas.

S3. El toque muestra el clima — verbatim: "sino que quiero que me muestre el clima
de mi ubicacion". En la pantalla RELOJ un toque alterna la cara entre reloj y
clima; otro toque vuelve al reloj. La cara de clima muestra temperatura actual,
sensación térmica, máxima/mínima del día, humedad, el estado en palabras y la
ciudad. Si todavía no hay datos, dice que no hay lectura en vez de quedar en
blanco. Mientras corre Concentración, el temporizador sigue mandando sobre el
reloj.

S4. La ubicación se configura en la web — verbatim: "La ubicacion se configura en
la WEB GUI y debería tener un boton para detectar mi localizacion automaticamente."
En Integraciones → Clima: el botón "Detectar mi ubicación" deja que el equipo la
resuelva por IP (precisión de ciudad; funciona aunque el panel se abra por HTTP
plano, donde el navegador bloquea `navigator.geolocation`), y un buscador de
ciudad (geocoding de Open-Meteo) permite fijarla con precisión. Se guardan
latitud, longitud y nombre; el panel los muestra y permite olvidarlos.

S5. La integración vive en Integraciones — verbatim: "recorda que en /admin la
config de la api iria en "Integraciones"". La configuración de la API y de la
ubicación está en la sub-pestaña Clima de Integraciones, no en Pantalla ni en
Sistema.

S6. API precisa y suficiente para una sola persona — verbatim: "decime cual podemos
usar que sea precisa y suficiente para el uso de una sola persona". Se usa
Open-Meteo (`api.open-meteo.com`): gratuita, sin API key, sin registro, con
modelos ECMWF/ICON/GFS y ~10 000 pedidos por día. El equipo hace HTTPS con el
bundle de CAs del core, como ya hace con Google Calendar. No hay clave que
guardar.

S7. Regla pura y testeable — el mapeo de códigos WMO a texto e ícono, el parseo de
la respuesta `current`/`daily`, el parseo del geocoding y la construcción de la
URL son funciones sin Arduino, fijadas por `tools/weather_core_test.cpp` y
compiladas por `python tools/run_tests.py`.

S8. El mock no miente — `tools/mock_gadget_api.py` refleja la misma validación y
las mismas respuestas que el firmware para los endpoints de clima, para que la
prueba del panel sin placa siga siendo honesta.

S9. Nada de lo ganado se pierde — el reloj sigue a 24 h, el temporizador de
Concentración sigue mandando, el apagado automático y el sueño no cambian, y un
clima que no llega se informa con un mensaje claro en vez de romper el `loop()`:
la consulta corre en su propia tarea y el vidrio sólo lee el último snapshot.

S10. La ubicación es privada — el lugar guardado (nombre y coordenadas) sale por
`/api/weather` y sus rutas derivadas, así que piden sesión incluso para leer, con
la misma regla que el calendario. `/api/status` (público) sólo lleva el resumen
que el panel ya usa, y tampoco publica las coordenadas.

## Tasks

| Task | Specs | Ruta | Commit |
| --- | --- | --- | --- |
| T1: documento de la feature, espejo en Engram y tablero | — | inline | — |
| T2: reloj 24 h fijo y deprecación del formato | S1, S2 | inline | sin commit (ver L4) |
| T3: RED de la regla pura del clima (códigos, parseo, geocoding, URL) | S7 | inline | sin commit (ver L4) |
| T4: módulos puros `weather_model/code/parse/geocode/url` | S7 | inline | sin commit (ver L4) |
| T5: almacenamiento NVS y transporte HTTPS del clima | S4, S6 | inline | sin commit (ver L4) |
| T6: servicio `weather.*` (tarea propia, snapshot, detección y búsqueda) | S4, S6, S9 | inline | sin commit (ver L4) |
| T7: cara de clima y alternancia por toque en el reloj | S1, S3, S9 | inline | sin commit (ver L4) |
| T8: endpoints `/api/weather*` y resumen en `/api/status` | S4, S5 | inline | sin commit (ver L4) |
| T9: sub-pestaña Clima en Integraciones y empaquetado del panel | S4, S5 | worker (panel) | sin commit (ver L4) |
| T10: mock, espejo de diseño, docs y verificación completa | S8, S9 | worker (mock/docs) + inline | sin commit (ver L4) |

## Log

L1 — Pedido del dueño, verbatim: "En el modulo del reloj del LCD, quiero que sea
siempre 24hs, y que ademas, el boton cuando lo tocas una vez, no cambie el tipo de
formato de hora (justamente vamos a deprecar esa funcionalidad), si no que quiero
que me muestre el clima de mi ubicacion. La ubicacion se configura en la WEB GUI y
debería tener un boton para detectar mi localizacion automaticamente. Ademas de
eso, supongo que necesitaría una integracion de API, si queres decime cual podemos
usar que sea precisa y suficiente para el uso de una sola persona, si es
integracion recorda que en /admin la config de la api iria en "Integraciones""

L2 — Decisiones del dueño: (a) API Open-Meteo sin API key; (b) el toque alterna
reloj ↔ clima y se queda hasta el próximo toque; (c) ubicación con botón de
detección por IP en el equipo más buscador de ciudad.

L3 — Restricción técnica registrada: el panel se sirve por HTTP plano
(`http://192.168.4.1/admin`), así que `navigator.geolocation` queda bloqueado por
el navegador (sólo funciona en contexto seguro, p. ej. localhost). Por eso la
detección automática la hace el equipo por IP; el buscador de ciudad cubre la
precisión.

L4 — Cierre de la implementación. Evidencia observada:

- `python3 tools/run_tests.py`: 41 tests verdes, incluido `weather_core_test`
  (códigos WMO, parseo de pronóstico, geocoding, IP y rutas exactas) y el
  round-trip de `web_assets.h`.
- `make build` con el FQBN de INSTALL.md §3: compila, 50 % de flash y 22 % de RAM
  estática, sin warnings.
- El panel `/admin` tiene la cuarta sub-pestaña Clima; `node --check app.js` y el
  cruce de ids `app.js`↔`admin.html` pasan. `web_assets.h` reempaquetado.
- El mock (`tools/mock_gadget_api.py`) reproduce los siete endpoints y la misma
  validación; `py_compile` verde y curls de prueba sin y con sesión.
- `docs/ARCHITECTURE.md`, `README.md` y el espejo `design/admin-espejo` quedaron
  actualizados (build.py regenerado).

L5 — Commits diferidos a propósito. El árbol de trabajo ya tenía cambios sin
commitear ajenos a esta feature (diseño, buddy, landing). Commitear por archivo
arrastraría ese WIP a los commits del clima; se deja todo en el árbol para que el
dueño revise y decida el corte. Ningún cambio se revirtió.

L6 — No se flasheó: la autorización de esta sesión es implementar y verificar;
el dueño decide el flasheo (skill `esp32-gadget-flash`).

L7 — Verificación en placa (COM5, autorización explícita del dueño). Evidencia
del probe `tools/probes/clima_reloj.txt`:

- Upload: `Hash of data verified`.
- `api GET /api/status`: STA conectado (`VICTORWIFI`, 192.168.100.75), hora
  sincronizada (14:31:54), `weather.kind:"unconfigured"` al arrancar.
- `api POST /api/weather/detect`: `place.name:"San Miguel de Tucuman, AR"`,
  lat -26.81601, lon -65.21051 (ipwho.is por HTTPS en el equipo).
- `api GET /api/weather` a los 6 s: `kind:"ready"`, `hasData:true`, 22.4 °C,
  sensación 23.2, humedad 75 %, viento 13 km/h, máx 23.7, mín 20.2, código 3
  "Nublado" (Open-Meteo por HTTPS en el equipo).
- `dump 4` con `screen 0`: reloj 24 h, sin AM/PM.
- `tap` + `dump 4`: cara de clima con la temperatura grande, el ícono y las tres
  líneas de sensación/máx-mín/humedad-viento.
- Segundo `tap` + `dump 4`: vuelve al reloj. La pantalla quedó restaurada a SOFIA.

L8 — Defecto de privacidad encontrado al verificar por HTTP, arreglado y
reflasheado: `GET /api/weather` respondía **200 sin sesión** y publicaba el nombre
y las coordenadas del lugar. La tabla de `adminRequiresSession` protege el
calendario pero no conocía el clima. Se agregó la regla de prefijo
`/api/weather` (lecturas incluidas) y dos casos a `tools/admin_auth_test.cpp`:
RED (123 checks, 2 fallos), luego GREEN en la suite completa. Verificado en placa
tras reflashear: `GET /api/weather` sin sesión → 401; `/api/status` sin sesión →
200; login del dueño → 200; `GET /api/weather` con sesión → 200, `kind:"ready"`,
`place.name:"San Miguel de Tucuman, AR"`.

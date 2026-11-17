# Endurecer el repositorio: practicas, redundancias, pruebas y documentacion

El firmware funciona y la funcionalidad no cambia. Lo que falta es lo que hace a
un repositorio profesional: una unica forma de correr todas las pruebas, menos
duplicacion, archivos con una sola responsabilidad, artefactos de trabajo fuera
del control de versiones y documentacion que alcance para mantener y escalar el
proyecto sin la memoria de quien lo escribio.

Branch: `refactor/codebase-hardening` (desde `master`).

## Specs

S1. Analisis del codebase completo: estructura, convenciones, duplicaciones,
codigo muerto y deuda de mantenibilidad, con evidencia `path:line`. El resultado
queda escrito, no solo reportado en el chat.

S2. Congelamiento de comportamiento: ningun cambio de esta rama altera lo que el
dueno ve o puede hacer. La evidencia es la misma suite de host verde y la misma
compilacion de firmware verde antes y despues.

S3. Una sola verificacion: `python tools/run_tests.py` compila y corre **todas**
las pruebas de host (incluidas las que necesitan fuentes acompanantes y
ArduinoJson), verifica el round-trip de `web_assets.h` y devuelve exit != 0 si
algo falla. Documentado en el README.

S4. Redundancias eliminadas: solo se unifica lo que es duplicacion comprobada
(misma logica repetida, claves y constantes repetidas, helpers copiados). Cada
extraccion conserva el comportamiento y las firmas publicas que otros modulos
consumen.

S5. Archivos con una responsabilidad: el `.ino` queda como arranque y lazo
principal; la consola serie (comandos, ayuda, volcados) vive en su propio modulo.

S6. Higiene del repositorio: los scripts de sondeo del equipo quedan agrupados y
nombrados; los `.txt` de trabajo sueltos que no son fixtures no se versionan.

S7. Documentacion: un README en la raiz como puerta de entrada (que es, como se
compila, como se prueba, como se flashea, mapa del repo), un documento de
arquitectura con las convenciones y decisiones que un contribuyente necesita, y
`esp32_gadget/README.md` / `INSTALL.md` sin datos viejos (siete pantallas, la
consola, los modulos nuevos).

S8. Escalabilidad dicha en el lugar correcto: donde agregar una pantalla, donde
agregar un comando de consola y donde agregar un endpoint, con el paso a paso y
el archivo exacto.

## Tasks

| ID | Task | Specs | Route | Commit |
| --- | --- | --- | --- | --- |
| T1 | Documento de analisis (`docs/ARCHITECTURE.md` + hallazgos) | S1 | inline | |
| T2 | `tools/run_tests.py`: corredor unico de pruebas + README | S3 | inline | |
| T3 | Extraer la consola serie del `.ino` a `console.*` | S5 | inline | |
| T4 | Unificar duplicaciones comprobadas (helpers, claves, constantes) | S4 | inline | |
| T5 | Reorganizar `tools/`: `probes/`, limpiar artefactos | S6 | inline | |
| T6 | README raiz + repaso de README/INSTALL del firmware | S7,S8 | inline | |
| T7 | Verificacion final: `run_tests.py` + compilacion de firmware | S2 | inline | |

Limites de rebanada: cada commit es una unidad revisable por si sola y ninguno
cambia comportamiento. La rama no se pushea ni se mergea sin decision del dueno.

## Log

L1 (verbatim, pedido del dueno):

```
Analiza todo el codebase y aplica las mejores practicas de codigo, elimina redundancias que hayan, la idea es dejar un repositorio profesional con buenas practicas, patrones de diseños y pensado para escalabilidad, facil de mantener y codigo orientado para lectura humana, ahora el firmware anda relativamente bien, asi que tambien corramos pruebas para no romper lo que esta funcionando bien ahora! y actualicemos las documentaciones
```

L2 (evidencia, linea base antes de tocar nada): los 18 `tools/*_test.cpp`
compilan y pasan con g++ (17 con solo `-I esp32_gadget`; `spotify_core_test`
necesita `spotify_parse.cpp`, `spotify_pkce.cpp` y el include de ArduinoJson).
`python tools/verify_assets.py esp32_gadget/web_ui esp32_gadget/web_assets.h`
da 0 problemas. La compilacion `arduino-cli` con el FQBN de INSTALL.md se
registra en T7 antes y despues.

# Architecture dedup

Primera pasada de la auditoría de arquitectura (P0/P1/P3 del informe): red de
seguridad, códecs genéricos fuera de los módulos de feature, y un solo dueño de
los límites de brillo. La parte estructural grande (partir `net.cpp`, funciones
god, mock compartido) queda para una feature posterior.

## Specs

- **S1** El usuario pidió, textual: «quiero evitar redundancias de codigo, aplicar
  correctamente patrones de diseño».
- **S2** El usuario pidió, textual: «Necesito un repositorio ordenado, legible
  para humanos y bien documentado».
- **S3** Alcance elegido por el usuario, textual: «P0 + P1 + P3 (red de seguridad
  y dedup de bajo riesgo)», descrito al ofrecerlo como: «CI, Makefile, extraer
  sha256/url_codec/base64url, y un solo dueño de los límites de brillo. Todo con
  los 35 tests en verde como red. Cambios chicos, commits revisables, cero riesgo
  de romper la placa.»
- **S4** El comportamiento observable de la API y del firmware no cambia: los
  rangos que el panel ya acepta siguen aceptándose con el mismo resultado, y los
  códecs siguen produciendo exactamente los mismos bytes.
- **S5** Ningún archivo puro nuevo queda sin su prueba de host: `sha256` y
  `url_codec` se fijan contra vectores publicados (FIPS 180-4 y RFC 3986), y
  `cfgClampLcd` contra los bordes de `CFG_LCD_MIN/MAX`.

## Tasks

| ID | Spec | Route | Commit | Tarea |
| --- | --- | --- | --- | --- |
| T1 | S2, S3 | inline | `chore(build): ...` | `Makefile` con `test`/`build`/`flash`/`pack` + workflow de CI que corre la suite |
| T2 | S1, S5 | inline | `refactor(crypto): ...` | Extraer `sha256.{h,cpp}`; `admin_auth` deja de depender de `spotify_pkce` |
| T3 | S1, S4, S5 | inline | `refactor(codec): ...` | Extraer `url_codec.{h,cpp}`; borrar `spotifyUrlEncode` y `calendarUrlEncode` |
| T4 | S1, S4, S5 | inline | `refactor(settings): ...` | `cfgClampLcd` en `config_value.h` como único dueño del rango; sacar el 0..255 de `net.cpp` |
| T5 | — | — | — | **descartada**, ver L5 |
| T6 | S2 | inline | `docs: ...` | Actualizar `ARCHITECTURE.md` (módulos nuevos, regla de límites, verificación) y `README.md` |

## Evidencia

- Línea base de host: `python3 tools/run_tests.py` → **35 passed**.
- Host después de T1..T4: **37 passed** (dos pruebas nuevas: `sha256_test` y
  `url_codec_test`).
- Prueba diferencial de `urlEncode` contra las dos implementaciones viejas:
  idéntico en cada byte 1..255 (capacidades 1..5) y en catorce cadenas
  representativas para toda capacidad entre 1 y 3n+2.
- Firmware: `arduino-cli 1.6.0` + core `esp32:esp32 3.3.12` con el FQBN de
  INSTALL.md §3. Línea base antes de tocar código: exit 0, 49% flash, 22% RAM.
- `verify_assets.py`: 0 problemas.

## Log

- **L1** «Necesito que analicemos todo el repo y hagamos una auditoría rigurosa de
  la arquitectura y codigo, quiero evitar redundancias de codigo, aplicar
  correctamente patrones de diseño. Necesito un repositorio ordenado, legible
  para humanos y bien documentado»
- **L2** Decisión de alcance: P0+P1+P3. P4 (`net.cpp` en módulos por dominio),
  P5 (funciones god) y P6 quedan fuera de esta feature a propósito.
- **L3** Hallazgo de entorno: no hay `arduino-cli` en WSL, pero sí el de Windows
  (`/mnt/c/Program Files/Arduino CLI/arduino-cli.exe`, 1.6.0) con el core
  `esp32:esp32 3.3.12` instalado, así que la compilación del firmware es
  verificable en esta máquina.
- **L4** Corte de slice en 486 líneas (T1+T2) sobre `refactor/architecture-dedup`.
  La entrega es la rama con commits por unidad; el PR y su tamaño quedan a
  decisión del dueño.
- **L5** T5 (`apiBody` para los 26 pares de parseo de `net.cpp`) se descarta. El
  par son dos líneas; colapsarlo a una no arregla el problema real —26 handlers de
  cinco dominios en el mismo archivo— y mete 26 ediciones sin prueba posible en un
  archivo de 1696 líneas que no se compila en el host. Ese código se reescribe
  cuando `net.cpp` se parta por dominio (P4 del informe), donde el helper sale
  gratis y sobre una estructura ya sana. Reversible: si el dueño lo quiere ahora,
  se hace.
- **L6** Corte de slice en 767 líneas (T3).
- **L7** Verificación de T2/T3/T4: una sola compilación limpia del firmware con el
  FQBN real después de las tres unidades, más la suite de host y la prueba
  diferencial del códec. Resultado: exit 0, 49% flash, 22% RAM, y el binario quedó
  308 bytes más chico que la línea base por la deduplicación de SHA-256.
- **L8** Verificador independiente (`gentle-ai-verify`), read-only y con pruebas
  diferenciales propias: los cuatro puntos del diff **se sostienen**.
  - SHA-256: cuerpo idéntico y 46/46 digests de `adminHashDerive` iguales contra
    la línea base; longitudes 0..700 comparadas.
  - `urlEncode`: 255 bytes × capacidad 0..8 y 9 cadenas × capacidad 0..3n+2, cero
    diferencias contra las dos implementaciones viejas; los constructores de
    Google siguen abortando.
  - Brillo: idéntico de INT_MIN a INT_MAX a través de la cadena vieja.
  - Makefile/CI: FQBN idéntico a INSTALL.md §3, assets viejos fallan sin
    reempaquetar, `flash` sin PORT se niega.
  - Suite diferencial: línea base 35 passed, rama 37 passed, y la salida de cada
    test preexistente es byte a byte igual.
- **L9** Correcciones de la verificación, aplicadas en T7:
  1. `console.cpp` todavía tenía un tercer rango (0..100) para `bl`: ahora usa
     `cfgClampLcd`, así que "un solo rango" pasa a ser literal.
  2. `make flash` sin PORT compilaba el firmware entero antes de negarse: el guard
     es ahora un prerequisito (`check-port`).
  3. El workflow podía quedar verde con tests salteados: el runner suma
     `--fail-on-skip` y la CI lo usa.
  4. El README exageraba el disparador de la CI (es push a `main` y PR).
  5. Cobertura que el recorte de T3 había quitado: `calendar_core_test` vuelve a
     fijar que los constructores rechazan un id que no entra, comprobado por
     mutación (si el constructor ignorara el rechazo, la prueba falla). Se suman
     los bordes de bloque de SHA-256 (119/120/128), el barrido de los 255 bytes en
     `url_codec_test` y los extremos `INT_MIN`/`INT_MAX` del clamp.
- **L10** Precisión sobre el mensaje de T2: el cuerpo de SHA-256 se movió tal cual
  salvo un guard agregado (`if (!out) return;`). Ningún llamador pasa null y los
  digests son idénticos, pero el commit no debía decir "tal cual" sin nombrarlo.
- **L11** La rama queda en seis commits y 1017 líneas de autor, muy por encima del
  slice de ~400: los cortes en 486 (L4) y 767 (L6) marcan dónde termina cada
  unidad revisable. Se entrega como rama con commits por unidad; si el dueño
  quiere PRs, el corte natural es `3417a9d..816d989` (build + códecs) y
  `fcda01f..HEAD` (límites + correcciones de verificación).

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
| T5 | S1 | inline | `refactor(net): ...` | Helper `apiBody` para el boilerplate de parseo de los handlers |
| T6 | S2 | inline | `docs: ...` | Actualizar `ARCHITECTURE.md` (módulos nuevos, regla de límites, verificación) y `README.md` |

## Evidencia

- Línea base de host: `python3 tools/run_tests.py` → **35 passed**.
- Línea base de firmware: `arduino-cli compile` con el FQBN de INSTALL.md §3
  (arduino-cli 1.6.0, core esp32 3.3.12) → ver tarea T1.

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

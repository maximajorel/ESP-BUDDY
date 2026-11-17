---
name: esp32-gadget-flash
description: "Trigger: subir el firmware, flashear la placa, upload to the device, flash the ESP32, arduino-cli upload, probar por consola serie, dump the screen. Build, flash and verify this project's firmware on the Waveshare ESP32-S3-LCD-1.47B."
license: Apache-2.0
metadata:
  author: gentleman-programming
  version: "1.0"
---

## Activation Contract

Activate when the task is to build, compile, flash, reflash, or verify on hardware the firmware in `esp32_gadget/`.

Do not activate for host-only work (g++ tests in `tools/`, editing `web_ui/`).

## Hard Rules

- Flash only with explicit authorization in the current session. Never flash from a plan, a "test if it works" hunch, or after a failed compile.
- Run the pre-flight gate first: `g++` host tests, and if `web_ui/` changed, `python3 tools/pack_web.py esp32_gadget/web_ui esp32_gadget/web_assets.h` then `python3 tools/verify_assets.py esp32_gadget/web_ui esp32_gadget/web_assets.h`. A stale `web_assets.h` ships an old panel.
- The FQBN is fixed. `PSRAM=opi` and `CDCOnBoot=cdc`: wrong PSRAM does not boot, wrong CDC leaves the console silent.
- One process per COM port. Never upload while a monitor, probe, or IDE holds it.
- Never invent the port. Re-read it from `board list` every time.
- Report the compile exit code, the upload hash verification, and the post-flash evidence. Do not claim success without them.

## Decision Gates

| Situation | Action |
| --- | --- |
| Board not listed | Ask the owner to plug it in; do not guess a COM number |
| Compile fails | Stop. Fix code. No flashing |
| Upload fails mid-write | Re-check the port and whether another process holds it; retry once, then report |
| Need pixel or state proof | Use the serial probe (`dump`, `api GET /api/status`), not a guess |
| Only docs or web_ui changed | Repack assets if needed; flashing is unnecessary unless asked |

## Execution Steps

1. Read `esp32_gadget/INSTALL.md` §3 for the board options. Use its FQBN verbatim.
2. Pre-flight gate above. Fix anything red.
3. Find the port: `"/mnt/c/Program Files/Arduino CLI/arduino-cli.exe" board list`.
4. Compile and upload. From WSL the Windows CLI works; keep the cache in a `.build-*` directory (gitignored):
   `arduino-cli.exe compile -b "<FQBN>" --build-path "C:\...\esp32\.build-config" --warnings all "C:\...\esp32\esp32_gadget"`
   then `upload -p COM<N> -b "<FQBN>" --build-path ... <same sketch path>`. Pass Windows paths to the Windows binary.
5. Confirm `Hash of data verified` in the upload output.
6. Verify on the device. Serial console goes through Windows Python, because the CDC port is not visible in WSL:
   `powershell.exe -NoProfile -Command "cd 'C:\<repo>'; python tools\gadget_probe.py '@tools\config_cmds.txt'"`
   Opening the port resets the board. First command file line `boot 4` waits for the banner.
7. Prove the change: `dump 4` for pixels, `api GET /api/status` for state, plus the firmware's own serial log lines. Paste the evidence.
8. Write the probe sequence into `tools/*_cmds.txt` so the check is reproducible, and record the result in the feature document's Log.

## Output Contract

Return: commit or branch flashed, port, compile result, upload hash result, the exact probe commands run, the observed evidence (values or ASCII dump excerpt), and anything that could not be verified on hardware.

## References

- `../../../esp32_gadget/INSTALL.md` — board configuration, console commands, common failures
- `../../../ARRANCAR.md` — Windows-side setup and the traps that look like bugs
- `../../../tools/gadget_probe.py` — serial driver used for verification
- `../../../tools/config_cmds.txt` — example probe sequence

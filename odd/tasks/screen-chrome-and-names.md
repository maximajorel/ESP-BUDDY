# Screen chrome and user-named screens

UI polish on the existing `esp32_gadget` firmware: drop the header band and the
clock's seconds trace, and let the owner rename every screen from the web panel
with the name persisted in NVS. Builds on `odd/tasks/esp32-gadget.md` (S1-S7) and
`odd/tasks/spotify-now-playing.md`; no hardware, pin or build change.

## Specs

S8. Minimal clock — verbatim: "En la vista de la hora la podemos hacer mas
minimalista? saquemos la barra de segundos...". Remove the seconds progress bar
from the clock face and keep the face minimal (time + date), white on black.

S9. No header band — verbatim: "Luego veo que en todas las pantallas tenemos
como un header, por ejemplo la de reloj tenemos "reloj utc -3, en "Esquivar"
tenemos titulo y modo ascii, me parece info al pedo..". Every screen loses the
top title/status band; its space goes back to content. Status worth keeping
(game over, calibrating, network state) stays in the body or footer.

S10. Rename + reorder from the panel — verbatim: "Ademas me gustaria desde la
web UI cambiar el nombre de las pantallas aparte de reordenar. Es posible? Y que
persista el cambio". The web panel edits each screen name next to the existing
reorder controls, applies both together, shows the custom name in the panel, and
the name survives a power cycle. Factory reset restores the shipped names.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S10 | inline: pure `screen_name.h` cleaning rules + host test | — |
| T2 | S10 | inline: NVS keys, `screens` registry, `/api/screens/names`, factory reset | — |
| T3 | S9 | inline: remove `chromeHeader` from every screen and reclaim the band | — |
| T4 | S8 | inline: clock face without seconds bar, resized and recentred | — |
| T5 | S10 | inline: panel edits names with the order, labels come from the device | — |
| T6 | S8,S9,S10 | inline: docs + mock API, pack assets, host tests, firmware compile | — |
| T7 | all | inline: flash COM5 and verify by serial console (`dump`, `api`) | — |

Commits are empty for this topic: the repository had no baseline commit yet, so
there was no diff to slice work units against. The initial import commit now
holds the whole workspace; later work slices from it.

## Log

L1 (verbatim): "conecte el esp. En la vista de la hora la podemos hacer mas
minimalista? saquemos la barra de segundos...

Luego veo que en todas las pantallas tenemos como un header, por ejemplo la de
reloj tenemos "reloj utc -3, en "Esquivar" tenemos titulo y modo ascii, me parece
info al pedo.. Ademas me gustaria desde la web UI cambiar el nombre de las
pantallas aparte de reordenar. Es posible? Y que persista el cambio"

L2: Decisions taken while implementing, so a later session does not re-litigate
them. Header band removed entirely, including the picker's "PANTALLAS" title;
footers stay because they carry the button gestures. Clock: seven-segment face
grows from 56x90 to 64x104 per digit and the date sits under it; the seconds
trace is gone. Screen names: 1-12 stored characters, outer whitespace trimmed,
ASCII control characters rejected, UTF-8 cut only at a character boundary, and
the panel draws them through `textFoldAscii` so "Música" reads "MUSICA" on the
glass while the API and panel keep the accents. Defaults stay the shipped five
uppercase names.

L3: Verification evidence for T1-T7, all on 2026-11-16. Host tests: `screen_name_test` PASSED (0 failures, 27 checks), `screen_order_test` and `text_fold_test` still PASSED. Panel assets repacked and `verify_assets.py` PASSED (3 files, 60652 bytes). Firmware compiled with arduino-cli 1.6.0 / esp32 3.3.12: 1314338 bytes (41%) flash, 53380 bytes (16%) RAM. Flashed to COM5, "Hash of data verified". Real device at 192.168.100.75 on the new firmware: `GET /api/status` reports the new `names` array; `POST /api/screens/names` with `["MI RELOJ","MÚSICA","ESQUIVAR","WIFILOCAL","TRAGAMONEDAS"]` echoed accents back; an empty name answered HTTP 400 and left the stored names unchanged; a 13+ character name was cut to 12 at a character boundary; `POST /api/restart` followed by a fresh `GET /api/status` (uptime 13.4 s) returned the same custom names, so the rename survives a power cycle. The picker dump (`hold` + `dump 3`) showed the custom name "MI RELOJ" in scale 3 with no header band, and the clock dump (`screen 0` + `dump 3`) showed the digits starting at y≈18 with no header row, no header rule and no seconds bar under the date. The panel served by the device rendered five editable name inputs (maxlength 12), the tiles with the stored names, the "Guardar cambios" and "Nombres y orden originales" buttons, and no console errors. Device left with the shipped names and its previous order `[0,4,2,1,3]`.

# Screen and LED auto-off

The glass and the RGB bead stay lit forever today. This adds an inactivity
timeout that turns both off, configurable from the web panel, with the screens
that matter kept awake (Meteoritos while playing, slot, music) and any tap on
BOOT bringing it back.

## Specs

S18. Auto-off, verbatim: "Tambien necesito que la pantalla se apague cada x
cantidad de tiempo, al igual que las luces ... el tema es que en algunas
pantallas no, como el juego del meteoro cuando estoy jugando o el casino.... el
de la musica tampoco, pero lo que es hora y red si, deberia poder configurar en
la UI que pantallas estan incluidas en el timeout para apagar la screen, esta se
prendera con un toque en el boton...". After the configured idle time the display
backlight and the RGB bead turn off, but only on the screens included in a
per-screen mask configured from the web UI. Meteoritos does not sleep while a run
is in progress. A tap on BOOT wakes the screen without triggering the screen's
action. Factory defaults: 120 s, clock and network included; slot, game and music
excluded.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S18 | inline: pure `screen_sleep.h` + host test | — |
| T2 | S18 | inline: settings keys, display awake, RGB sleep/wake | — |
| T3 | S18 | inline: screens activity timer, wake on tap, game busy rule | — |
| T4 | S18 | inline: API + panel timeout selector and per-screen toggles | — |
| T5 | S18 | inline: docs, mock, compile, flash, hardware verification | — |

Commits are empty for this topic: the whole project landed in the initial
import commit. Feature work from here on commits per unit.

## Log

L1 (verbatim): "Tambien necesito que la pantalla se apague cada x cantidad de
tiempo, al igual que las luces, porque si no es un embole, el tema es que en
algunas pantallas no, como el juego del meteoro cuando estoy jugando o el
casino.... el de la musica tampoco, pero lo que es hora y red si, deberia poder
configurar en la UI que pantallas estan incluidas en el timeout para apagar la
screen, esta se prendera con un toque en el boton..."

L2: Decisions. The timeout and the mask live under their own NVS keys so the
settings blob (credentials) keeps its layout. The timer is per current screen: an
excluded screen never sleeps, and switching screens restarts the countdown. The
picker counts as activity. Wake consumes the gesture: the tap that lights the
screen does not also shoot/spin. The game reports "busy" (playing, calibrating,
confirming) and refuses to sleep then; ready/over may sleep. The configured
brightness and RGB colour are kept, not overwritten, so waking restores exactly
what the owner had.

L3: Evidence (2026-11-16). Host: `screen_sleep_test` PASSED (mask bits, zero
means never, boundary, millis wrap); all other host tests still PASSED; panel
assets repacked and `verify_assets.py` PASSED. Firmware compiled (1325706 bytes,
42% flash; 53660 bytes, 16% RAM) and flashed to COM5, `Hash of data verified`.
Device checks over HTTP on the real board: with `timeoutSec=8` and mask 0x01 the
clock went dark (display.awake=false, rgb.asleep=true) after 10 s; `POST
/api/press` brought it back (awake=true, rgb.asleep=false); with mask 0x04 the
slot screen stayed awake for 10 s while the game's READY screen slept after the
same 8 s; the `steer`-mask test proved the per-screen bit while the busy-game rule
is enforced by `gameWantsScreenOn()`. Toggling "MUSICA" in the panel posted mask
25 and the device accepted it; the device was left with the defaults (120 s, mask
0x09) and the panel renders the selector plus five checkboxes with the device
names and no console errors. The RGB bead is off while asleep and the configured
colour/level/effect are kept for wake.

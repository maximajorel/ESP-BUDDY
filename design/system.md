# ESP32 Gadget design system

Two surfaces, two palettes. The web panel is opened on a phone joined to the
device's own access point; the glass is a 320x172 strip. They no longer share a
palette: the web is paper and ink (the owner's `DESIGN.md` reference), the glass
stays black with the cyan eyes, because the panel's face was removed and the
face now lives only on the board.

Every value below was read from a source file; the source is named next to each
group. Nothing here is invented.

Sources: `esp32_gadget/web_ui/styles.css` is the stylesheet the firmware ships
(`web_assets.h` is packed from `web_ui/`). `_legacy/web_ui/styles.css` is the
retired single-page copy: it is not a source of truth and nothing here comes
from it. The glass values come from the firmware headers, named per group.

## 1. Colour — web panel

Source: `esp32_gadget/web_ui/styles.css`, `:root` (lines 18-51). Reference and
rationale: `odd/tasks/web-redesign.md` (S1) and the owner's
`Downloads/DESIGN.md` (Slush).

| Token | Value | Usage |
| --- | --- | --- |
| `--paper` | `#ffffff` | Cards, inputs, pill fills, marquee text |
| `--sky` | `#dceeff` | Page background, hero and status bar band |
| `--gray` | `#cccccc` | Secondary section band (games) and stat cells |
| `--mist` | `#e9e9e9` | Hover fills, secondary buttons, neutral card tint |
| `--ink` | `#000000` | All text, every 1px outline, filled CTAs, marquee band |
| `--blue` | `#4da2ff` | Sticker fill only (decorative, never a CTA or link) |
| `--mint` | `#55db9c` | Sticker fill; success lives on the `.pill.online` fill |
| `--lav` | `#e9ccff` | Sticker fill, Spotify tile, `.say.buddy` |
| `--ember` | `#fb4903` | Alert sticker: offline pill, banner, `.btn.danger`, errors |
| `--sun` | `#ffd731` | Active sticker: current screen, active difficulty, logo |
| `--violet` | `#5c4ade` | The games card fill and the focus outline |
| `--ok-ink` | `#0f7a45` | Success text on `--mist` |
| `--bad-ink` | `#b32b00` | Danger text on `--mist` |

Rules from the reference, kept verbatim: no gradients and no `box-shadow`
anywhere; elevation is colour plus a black outline. Black text on any sticker
fill; white text only on `--ink` and `--violet`. `--blue` is decorative, never an
action colour.

Two deliberate deviations, both forced by where this runs (S1b): system font
stacks instead of the commercial Lateral / Aeonik Pro, and flat SVG stickers
instead of the 3D ribbons, since the board serves no fonts and stores no raster
art.

## 2. Glass palette — the device screen

Sources: `esp32_gadget/buddy_face.h` (owner and hot colours, glow pad),
`esp32_gadget/screen_buddy.cpp` (glow shades), `esp32_gadget/gfx.h`
(`GFX_BLACK`, `GFX_WHITE`), `esp32_gadget/esp32_gadget.ino` (splash colours).

| Token | Value | Usage |
| --- | --- | --- |
| `glass-black` | `#000000` | Every screen clears to this; the face has no background other than black |
| `glass-white` | `#ffffff` | Splash title, AP name |
| `glass-cyan` | `#00b4ff` | `BUDDY_EYE_COLOR_DEFAULT`: the solid eye |
| `glass-hot` | `#ff3c1e` | `BUDDY_EYE_HOT_COLOR`: hot tint, the offline `!` mark |
| `glass-gold` | `#ffd700` | Splash second line, splash hold hint |
| `glass-grey` | `#8a8a8a` | Splash screen list |
| `glass-blue-grey` | `#7a8ca0` | Splash "connect to this network" line |
| `glass-rule` | `#333333` | Splash divider |

Glow, three nested layers behind each eye (`screen_buddy.cpp:666-668`):
`buddyEyeScale(colour, 32)` at `pad + 6`, `17` at `pad + 4`, `7` at `pad + 6`
outermost. `pad` is `BUDDY_GLOW_PAD = 6`.

Dim face (no bridge): `buddyEyeScale(colour, 32)` (`buddy_face.h`).

## 3. Geometry

| Token | Value | Source |
| --- | --- | --- |
| Glass canvas | `320 x 172` CSS px | `display.h` (`DISPLAY_W/H`) |
| Panel, native | `172 x 320` | `display.h` (`PANEL_W/H`) |
| Panel column offset | `34` | `display.h` |
| Eye centres | `x = 105, 215`, `y = 88` | `screen_buddy.cpp` `drawFace` |
| Eye half-extent | `48 x 48` (`rx`, `ry` default) | `buddy_anim.h` `BuddyPose` |
| Corner ratio | `0.38` of the short half-axis | `buddy_face.h` |
| Glow pad | `6` px | `buddy_face.h` |
| Blink bar height | `4` px half-extent | `screen_buddy.cpp` `drawFace` |
| Minimum eye gap | `BUDDY_EYE_GAP_MIN` | `screen_buddy.cpp` |
| Pose easing | `100 ms` time constant | `buddy_face.h` `BUDDY_EASE_TAU_MS` |
| Face frame rate | `20 fps` (one state poll per 50 ms tick) | `screen_buddy.cpp` |

### El vidrio mientras hay Concentración

Sources: `esp32_gadget/buddy_focus.h` (all constants), `esp32_gadget/screen_buddy.cpp`
(`drawFace` in compact mode, `drawFocusBand`).

While a session runs, the timer owns the glass and the face shrinks into the top
band. Nothing below may overlap; `tools/buddy_focus_test.cpp` pins the
arithmetic.

| Token | Value | Usage |
| --- | --- | --- |
| `BUDDY_FOCUS_EYE_CY` / `_CX` | `34` / `±22` from centre | small eye centres; the pair keeps the full face's 14 px gap (12 px here) |
| `BUDDY_FOCUS_EYE_RX` / `_RY` | `16 x 12` | small eye half extents (full-size default `48 x 48`) |
| `BUDDY_FOCUS_EYE_SLACK_X` / `_Y` | `10` / `4` px | how far gaze, bob or a nod may push a small eye |
| `BUDDY_FOCUS_BAR_Y` / `_H` / `_W` | `56` / `5` / `200` | slim progress mark between the eyes and the number |
| `BUDDY_FOCUS_PLATE_Y` | `64` | black plate under the eyes; the number never shares a pixel with the face |
| `BUDDY_FOCUS_BAND_Y` | `66` | top of the digits |
| `BUDDY_FOCUS_DIGIT_W` / `_H` | `68 x 104` | one seven-segment digit; 60% of the 172 px height |
| `BUDDY_FOCUS_DIGIT_THICK` | `13` | segment stroke; heavier than the clock screen's `10` |
| `BUDDY_FOCUS_COLON_W` / gaps | `18`; `5` digit, `8` colon | four digits plus colon = `316` of `320` px |
| `BUDDY_FOCUS_DONE_SCALE` | `6` | the achievement word (`LISTO`) in the 5x7 font |
| `BUDDY_FOCUS_LAST_FIVE_MS` | `300000` | under five minutes the glance at the number doubles its rhythm (`4500` ms instead of `9000`) |
| `BUDDY_FOCUS_FINAL_WINDOW_MS` | `900` | the last-minute window: two blinks 300 ms apart and a look up at the owner |

Rules: while the timer is up, the eyes' gaze and bob are clamped, and the ball,
the bug and the sleeping letters are not drawn — anything that could cover the
number steps aside. The digits and the progress mark both wear `eyeRgb`, the
colour the eyes resolved for this frame (owner colour, dim without a bridge, hot
when angry); unlit segments are not drawn, which is the clock screen's rule.

Rule: lids are black bands cut out of the eye plus its glow, and a band must
always cover the rotated extent plus the glow pad. A tinted sliver outside the
band is a bug.

## 4. Typography — web panel

Source: `esp32_gadget/web_ui/styles.css` (`--font`, `.body`, `.display`, `.nav-item`).

- Family: system stack, `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, …`.
  No web fonts: the panel is served from the device with no internet.
- Body: `16px / 1.5`.
- Display: weight `900`, `line-height` `0.8`, `letter-spacing` `-0.05em`,
  uppercase, `clamp(72px, 27vw, 118px)` for the landing wordmark. This is the
  crushed-display gesture of the reference, done with weight and tracking instead
  of a display face.
- Section titles: `clamp(30px, 8vw, 40px)`, weight `900`, `-0.04em`.
- Card titles: `17px` / `15px`, weight `700`, `-0.01em`.
- Labels, tab strip, chips, pills: `10–12px`, weight `700`, uppercase,
  `letter-spacing: 0.032em`.
- `--mono` (`ui-monospace…`) only for numbers that must not shift.

The glass has no proportional text: labels use the built-in 5x7 bitmap font at
`scale` 1, 2 or 3 (`gfxText`, `font5x7.h`). The face itself never shows text.
The one exception is the study timer: its number is seven-segment, `64 x 100`
digits with a `10` px stroke, using the same `gfxSegmentDigit` renderer as the
clock screen (`screen_clock.cpp`). A countdown never renders in the 5x7 font:
at that size nobody reads it from across the room.

## 5. Spacing, radius, elevation — web panel

Source: `esp32_gadget/web_ui/styles.css`.

- Radius: `--r-in` `16` (inputs, tiles, stats), `--r-card` `20` (cards),
  `--r-lg` `40` (hero game card), `--r-pill` `1600` (tabs, buttons, chips,
  pills, dots). Nothing below `16`.
- Padding: `16px` (cards), `14px` (top bar, tab strip), `10–13px` (buttons and
  pills), `18px` (hero), `16px 14px 40px` (admin main), `16px 16px 40px`
  (launcher).
- Gaps: `6`, `8`, `10`, `12`, `14` px.
- Touch target: `--tap: 44px`.
- Page width: `--page: 1080px` for the admin, `720px` for the centred landing at
  `>=620px`.
- Elevation: no `box-shadow` anywhere; hierarchy comes from the 1px `--ink`
  outline and the `--paper` / `--mist` / `--sky` surface steps.

## 6. Component rules

Sources: `esp32_gadget/web_ui/styles.css` (`:root` and the component blocks),
`esp32_gadget/web_ui/index.html` and `admin.html` for the markup each class
serves.

- Pills (`.pill`, `.live`) are `10px` uppercase weight `700`, radius `1600`,
  `1px` `--ink` border, `--paper` fill, with a bordered dot. `.online`/`.live`
  fill `--mint`, `.offline` fills `--ember`; the dot goes white in both.
- Cards (`.card`) are `--paper` with a `1px` `--ink` border and `--r-card`.
  Tints are opt-in (`.tint-lav`, `.tint-mist`, `.tint-sky`), never automatic.
- The admin navigation is a single `role="tablist"` strip: one pill per
  section, `--paper` normally, `--ink` fill with `--paper` text when active.
  Roving tabindex, Arrow/Home/End, and the hash is the active section.
- Buttons (`.btn`) are `--ink` fill with `--paper` text, or `.ghost` (`--paper`
  fill, `--ink` text). Never `--blue`. Danger is `--ember` with `--ink` text.
- Focus is always visible: `:focus-visible` gets a `2px --violet` outline with a
  `2px` offset; `--violet` is the only outline colour.
- The landing has no face. Its hero is the display wordmark plus three flat SVG
  stickers, and the only continuous motion is the black marquee.
- The concentration card (`.focus-band` > `.focus-card`, `styles.css:824-853`) is
  a `--mint` sticker, `--r-lg` radius, `18px` padding, `14px` gap, sitting on a
  `--sky` band. Inside: `.chips` of durations (active chip `--ink` fill), a
  `.focus-row` with the number fields (`.focus-field`: `10px` uppercase label,
  `--r-in` input, `96px` wide) plus the two `.btn`s, and `.focus-now`
  (`12px` weight `700`) reporting the countdown in `mm:ss`. The page never runs
  its own clock: it starts a session and reads the device's state back.
- The face on the glass: eyes only. Solid squircle, owner colour, three glow
  layers, expressions only from lids, gaze, rotation, grow, hot tint, breath and
  blink. No brows, no mouth, no text, no images.

## 7. Motion

- Panel: the marquee loops continuously (`26s` linear, duplicated track);
  everything else is short CSS transitions. `scroll-behavior: smooth` on `html`.
- Glass: everything is a pure function of `millis()`; no timers, no `delay`.
  Blinks and saccades are a deterministic hash of the clock, not a metronome.
- Any new artboard must respect `prefers-reduced-motion`; the stylesheet stops
  the marquee and every transition under it.

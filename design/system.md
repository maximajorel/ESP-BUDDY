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

### Tema oscuro — the same system, inverted paper

Source: `styles.css:1329-1360` (`[data-theme="dark"]`). The attribute lives on
`<html>`; the admin sets it from the header switch and remembers the choice in
`localStorage` (`espbuddy-theme`). The landing and the login stay light.

| Token | Light | Dark | Usage |
| --- | --- | --- | --- |
| `--paper` | `#ffffff` | `#16171a` | Cards, inputs, pill fills |
| `--sky` | `#dceeff` | `#0e0f12` | Page background |
| `--gray` | `#cccccc` | `#23252a` | Secondary band |
| `--mist` | `#e9e9e9` | `#22242a` | Stat cells, hover fills, fields |
| `--ink` | `#000000` | `#f1f1f3` | Text, every 1px outline |
| `--ok-ink` | `#0f7a45` | `#58d99b` | Success text |
| `--bad-ink` | `#b32b00` | `#ff8f66` | Danger text |

Rules: the sticker accents (`--blue`, `--mint`, `--lav`, `--ember`, `--sun`,
`--violet`) **keep their value in both themes** — they are the system, not a
tint. Anything painted on a sticker keeps `#000000` text and strokes
(`:is(.now, .pill.online, .pill.offline, .warn, .sp-state.*, .sp-panel,
.btn.danger, .toast.ok, .toast.err, .tile.active, .skip)`), so contrast is the
same in both themes. `color-scheme: dark` follows, so native pickers, ranges and
checkboxes render dark. Input placeholders invert to `rgba(255,255,255,0.42)`.
No page sets a theme the owner did not ask for.

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
| `glass-rule` | `#333333` | Splash divider; also the unfilled part of a progress mark |
| `glass-event` | `#aa50ff` | `buddy_phase.h` `BUDDY_PHASE_EVENT` default LED: the calendar's event face and its progress marks |

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

### El vidrio partido — evento del calendario y Concentración

Sources: `esp32_gadget/buddy_split.h`, `esp32_gadget/buddy_focus.h`,
`esp32_gadget/screen_buddy.cpp` (`drawEyes`, `drawSplitFrame`,
`paintCalendarPane`, `paintFocusPane`).

Both screens share one component: the real face in the left pane, a one-pixel
rule at `x=159`, and a data column on the right. The face is not a lookalike —
it goes through the same renderer as the full-screen face, at half scale, so
lids, grow, turn, perspective, tilt, colour, dim/hot, blink and the error X all
keep working. `tools/buddy_split_test.cpp` pins that the glow of every real
expression stops before the divider; `tools/buddy_focus_test.cpp` pins the
timer's column.

| Token | Value | Usage |
| --- | --- | --- |
| `BUDDY_SPLIT_DIVIDER_X` | `159` | the one-pixel rule between the panes |
| `BUDDY_SPLIT_FACE_CX` / `_CY` | `79` / `88` | centre of the face frame |
| `BUDDY_SPLIT_EYE_HALF_GAP` | `32` | pane centre to each eye centre |
| `BUDDY_SPLIT_FACE_SCALE` | `0.50` | eye size and every pose offset scale together |
| `BUDDY_SPLIT_CONTENT_X` / `_W` | `176` / `136` | the data column, both screens |
| `BUDDY_FOCUS_LABEL_Y` | `12` | `CONCENTRACION`, 5x7 at scale 1 |
| `BUDDY_FOCUS_DIGIT_Y` | `48` | top of the timer's number |
| `BUDDY_FOCUS_DIGIT_W` / `_H` | `27 x 64` | one seven-segment digit, in the column |
| `BUDDY_FOCUS_DIGIT_THICK` | `6` | segment stroke; the clock screen uses `10` on a bigger digit |
| `BUDDY_FOCUS_COLON_W` / gaps | `9`; `2` digit, `3` colon | four digits plus colon = `127` of the `136` px column |
| `BUDDY_FOCUS_DONE_SCALE` | `3` | `LISTO` in the number's box, 5x7 at 3x |
| `BUDDY_FOCUS_LAST_FIVE_MS` | `300000` | under five minutes the glance at the number doubles its rhythm (`4500` ms instead of `9000`) |
| `BUDDY_FOCUS_FINAL_WINDOW_MS` | `900` | the last-minute window: two blinks 300 ms apart and a look up at the owner |

Rules: the pair is confined to the left pane by one symmetric shift, so a wide
tilt or a saccade can never paint on the divider. While a split screen is up the
ball, the bug, the sleeping letters and the corner HUD are not drawn: the glass
belongs to the face and the column. The timer wears `eyeRgb`, the colour the
face resolved for this frame (owner colour, dim without a bridge, hot when
angry); unlit segments are not drawn, which is the clock screen's rule. The
timer has no progress bar: label, number and `LISTO`.

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
The one exception is the study timer: its number is seven-segment, `27 x 64`
digits with a `6` px stroke, using the same `gfxSegmentDigit` renderer as the
clock screen (`screen_clock.cpp`), sized to the split frame's right column. A
countdown never renders in the 5x7 font: at that size nobody reads it from
across the room.

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
- The admin shell is a two-column grid: `max-content` for the navigation aside
  (as wide as its longest label, `INTEGRACIONES`) and the rest for `<main>`
  (`styles.css:186-190`). Below `760px` the grid collapses to one column and the
  navigation becomes the horizontal strip again.
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
- The admin navigation is a left aside, not a strip: one pill per section in a
  column, every `nav-item` at `width: 100%` so all of them share the width of the
  longest label, `--paper` normally, `--ink` fill with `--paper` text when active,
  and a 1px hairline `tab-sep` between groups. Roving tabindex with
  Arrow/Home/End (both axis pairs, because the narrow strip scrolls sideways),
  and the hash is the active section.
- The theme switch is the only icon-only control in the panel: a round 34px
  button (`#themeBtn`) in the top bar, moon when light and sun when dark,
  `aria-pressed` carrying the state.
- The admin mark is the buddy's eyes: two black squircles (38% of the half-axis)
  on the `--sun` sticker, no glow, in both themes.
- Fields (`.field` > `.field-label` + `.field-input` + `.field-count`): the box is
  part of the design, not the browser's — `--mist` fill, ink outline, `--r-in`,
  the value in `--mono`, and an optional counter such as `32/63`. Colour fields
  use `.color-row`: the swatch plus its hex in `--mono`, so it is never alone.
- Sub-tabs (`.subtabs` > `.subtab`) split one section into several cards, one at a
  time: same pill as the navigation, `--ink` fill when selected, the card carries
  `data-subpanel`. Used by Integraciones and Red.
- Chips (`.chip`) are single-choice stickers inside a `.chips` group; the chosen
  one fills with `--ink`. The landing's duration chips keep their 10px size; only
  `.region .chip` grows to a touch target.
- The screen order in Aplicaciones is reordered by dragging the grip
  (`span.grip`, `draggable`), never by arrows: the row moves, the position number
  follows, and the dragged row is `--sun` while it travels.
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

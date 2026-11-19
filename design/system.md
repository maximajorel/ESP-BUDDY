# ESP32 Gadget design system

Two surfaces, two palettes. The web panel is opened on a phone joined to the
device's own access point; the glass is a 320x172 strip. They share one palette
deliberately, so the panel's face mirrors what the board draws.

Every value below was read from a source file; the source is named next to each
group. Nothing here is invented.

## 1. Colour — web panel

Source: `esp32_gadget/web_ui/styles.css`, `:root` (lines 6-25).

| Token | Value | Usage |
| --- | --- | --- |
| `--bg` | `#0e1015` | Page background |
| `--card` | `#161a22` | Card surfaces |
| `--inset` | `#1b212b` | Inset fields, nav item background |
| `--line` | `#262d3a` | Hairline borders |
| `--tx` | `#e8ecf4` | Primary text |
| `--dim` | `#94a0b4` | Secondary text, meta, `.sub` |
| `--gold` | `#ffd700` | Brand accent, active nav, primary button, `.region-head h2` |
| `--gold-soft` | `#3a3212` | Accent background behind gold text |
| `--eye` | `#00b4ff` | The buddy face's owner colour |
| `--eye-hot` | `#ff3c1e` | Anger / dizziness tint of the eyes |
| `--ok` | `#4ade80` | Online pill, success |
| `--bad` | `#f87171` | Error text, offline pill |

Derived border tints read from the same file: online pill border `#1f3a2a`,
offline pill border `#3d2226` (lines 105-108).

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

Rule: lids are black bands cut out of the eye plus its glow, and a band must
always cover the rotated extent plus the glow pad. A tinted sliver outside the
band is a bug.

## 4. Typography — web panel

Source: `esp32_gadget/web_ui/styles.css`.

- Family: system stack, `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, …`.
  No web fonts: the panel is served from the device with no internet.
- Body: `16px / 1.5`.
- Scale in use: `10`, `11`, `12`, `14`, `16`, `17`, `18`, `24` px.
- Weights: `650` for headings and buttons, `700` for the small uppercase labels,
  regular elsewhere.
- Letter spacing: `0.2px` on the top bar title, `-0.2px` on `h2`.

The glass has no proportional text: labels use the built-in 5x7 bitmap font at
`scale` 1, 2 or 3 (`gfxText`, `font5x7.h`). The face itself never shows text.

## 5. Spacing, radius, elevation — web panel

Source: `esp32_gadget/web_ui/styles.css`.

- Radius: `2`, `10`, `11`, `12`, `16` (`--radius`), `999` (pills), `50%` (dots).
- Padding: `7px 12px` (pills), `10px 16px` (buttons), `10px 11px` / `10px 12px`
  (nav items), `12px 16px` (top bar), `18px 14px 28px` (layout container),
  `20px 16px 8px` (region), `2px 10px` (badge).
- Gaps: `4`, `7`, `12`, `14`, `24`, `30` px.
- Touch target: `--tap: 44px`.
- Elevation: no `box-shadow` anywhere; hierarchy comes from the `--line` border
  and the `--card` / `--inset` surface steps. Keep it that way.

## 6. Component rules

- Pills (`.pill`) are `12px` text, weight `650`, radius `999`, `7px 12px`, with a
  `50%` dot; `.online` uses `--ok`, `.offline` uses `--bad`.
- Cards are `--card` on `--bg` with a `1px` `--line` border and `--radius`.
- The active nav item is gold text on `--gold-soft` with a `--gold` border.
- Focus is always visible: `:focus-visible` uses the accent, never `outline:none`
  alone.
- The face on the glass: eyes only. Solid squircle, owner colour, three glow
  layers, expressions only from lids, gaze, rotation, grow, hot tint, breath and
  blink. No brows, no mouth, no text, no images.

## 7. Motion

- Panel: CSS transitions only, `scroll-behavior: smooth`.
- Glass: everything is a pure function of `millis()`; no timers, no `delay`.
  Blinks and saccades are a deterministic hash of the clock, not a metronome.
- Any new artboard must respect `prefers-reduced-motion`.

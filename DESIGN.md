# Auralloy — Design System

**Concept: liquid-metal aurora.** Two songs fuse into one alloy. The UI is an obsidian canvas lit by a slow aurora,
with alloy-glass panels (graphite, translucent, light-catching rims) and a single signature gradient —
**mint → violet → rose** — reserved for things that are *alive*: the playhead, the logo, the Forge button, active controls.

## 1. Tokens

### Color
| Token | Value | Use |
|---|---|---|
| `obsidian-950` | `#05060A` | window background |
| `obsidian-900` | `#0A0C14` | deep wells (waveform, analyzer) |
| `graphite-800` | `#11131D` | panel base |
| `graphite-700` | `#1A1D2B` | raised controls, knob bodies |
| `line` | `#FFFFFF` @ 8–14 % | hairlines, rims |
| `text-100` | `#EDEFF8` | primary text (contrast 15:1 on 900) |
| `text-300` | `#A9AEC4` | secondary text (8:1) |
| `text-500` | `#6F748C` | hints, disabled (decorative only) |
| `mint` | `#5CF2D6` | accent 1 · "Other" stem · positive |
| `violet` | `#9B8CFF` | accent 2 · "Bass" stem · focus |
| `rose` | `#FF6FB5` | accent 3 · "Vocals" stem · record/forge |
| `amber` | `#FFC857` | "Drums" stem · loop region · warnings |
| `sky` | `#5AB8FF` | secondary info |
| `danger` | `#FF5C7A` | destructive / clipping |
| **`alloy`** | mint → violet → rose, 135° | the signature gradient |

Source colours (up to 8 songs in a mashup) cycle through `mint, rose, violet, amber, sky, #7CE38B, #FF9F6B, #C58BFF`.

### Typography
| Role | Font | Size / weight | Notes |
|---|---|---|---|
| Display | Segoe UI Semibold | 26 / 600 | wordmark, +2 % tracking |
| Title | Segoe UI Semibold | 15 / 600 | card titles |
| Label | Segoe UI Semibold | 11 / 600 | ALL CAPS, +8 % tracking, `text-300` |
| Body | Segoe UI | 13 | |
| Numeric | Cascadia Mono / Consolas | 13–15 | time, BPM, values (tabular) |

### Spacing, radii, elevation
- Spacing scale: **4 · 8 · 12 · 16 · 24 · 32**.
- Radii: chip **8**, control **full (capsule)**, card **16**, panel **22**.
- Elevation: `e1` shadow `0 6 18 @ 35 %`, `e2` shadow `0 14 36 @ 50 %`, `glow` accent `0 0 18 @ 25–40 %`.
- Panels: fill white @ 6→3 % (top→bottom), 1 px rim white @ 22 % (top-left) → 6 % (bottom-right), 40 px top sheen white @ 7 %.

### Motion
| Token | Value | Use |
|---|---|---|
| `dur-hover` | 120 ms | hover / press feedback |
| `dur-state` | 220 ms | toggles, selection, chips |
| `dur-view` | 420 ms | view transitions (slide 24 px + fade) |
| `dur-intro` | 1 900 ms | logo forge intro |
| `ease-out` | `cubic-bezier(.16, 1, .3, 1)` | everything entering |
| `ease-inout` | `cubic-bezier(.65, 0, .35, 1)` | continuous moves |
| aurora drift | 70 s loop | background; always on |
| reactive glow | attack 40 ms · release 400 ms | driven by low-band energy of the output |

Respect "Reduce motion" (setting in *Audio & Display*): intro is skipped, aurora freezes, glow stays static.

## 2. Components

### Knob
- 76 px cell: label (Label style) · 56 px glass sphere · value read-out (Numeric).
- Arc: 3.5 px groove `white @ 10 %`; value arc in the module accent, with a 7 px glow underlay. **Bipolar** controls (EQ, pitch) draw from the centre.
- States: *hover* groove → 20 %, arc glow → 38 %; *drag* adds a 2 px outer ring; *disabled* (module off) alpha 42 %.
- Input: vertical drag, **mouse wheel (2 % / notch, Shift = fine)**, double-click = reset, click value to type.

### Button
- Capsule, height 34–40. *Primary* (Forge, Export): alloy fill; *secondary*: glass (white @ 10 %) with rim; *ghost*: text only.
- States: hover +15 % light, pressed −8 % scale 0.98, disabled alpha 40 %.

### Toggle (switch)
38 × 22 capsule; on = module accent fill, thumb = white glass with soft shadow. Off = white @ 16 %.

### Module card
Glass panel (radius 16) · title (Label, accent) · hairline in accent gradient · power switch top-right · knobs in a row. Disabled module dims its knobs.

### Timeline clip
- Rounded 8 px block; fill = source colour @ 28 %, 1 px rim = source colour @ 70 %; waveform drawn in source colour @ 90 % (lighter), centred.
- Name chip top-left (Label style); fade handles = 8 px triangles at top corners; trim edges = 6 px hit zones.
- Selected: 2 px alloy rim + glow. Muted track: clip alpha 35 %. Drag shows beat-grid snap line in `violet`.

### Deck card (Forge)
Glass card · mini waveform · BPM / key (Camelot badge) / length chips · analysis status ring (animates while analysing).
Compatibility to the current target is shown as a 5-segment meter in alloy colours.

### Compatibility matrix
N×N grid; cell fill = alloy gradient position by score (0 = `graphite-700`, 1 = mint); diagonal blank.

### Forge button & progress
Primary alloy capsule; while forging, the logo's two strokes pulse toward the apex and fuse in a loop while a thin progress arc fills; completion = lens flash.

## 3. Logo
A glass **A** made of two rounded strokes (the two songs): the left stroke runs mint to violet, the right violet to rose. They meet
at a glossy alloy droplet at the apex (the moment the songs fuse), and five soft waveform bars sit under the crossing. The mark is
drawn in code (`drawLogoMark` in `src/app/Theme.cpp`) so it animates: the strokes slide in, the droplet forms, the bars rise.
Wordmark **AURALLOY** is drawn as stroked letter paths (`drawWordmark`, `tools/make_svg.py`) with one mint to violet to rose gradient
running across the whole word, so it looks the same on every machine without a font.

## 4. Accessibility
- Text on 900/800 surfaces ≥ 4.5:1 for `text-100/300`; `text-500` is never the only carrier of meaning.
- Every control reachable by keyboard shortcuts (see README); focus ring = 2 px `violet`.
- Colour is never the only signal: stems are also labelled; clipping shows `CLIP` text.

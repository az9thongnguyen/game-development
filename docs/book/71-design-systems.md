# Chapter 71 — Design systems: tokens, type scale, and the widget layer

> **Where we are.** Ch.68–70 gave us the *materials*: anti-aliased text, smooth
> lines, rounded shapes, soft shadows. But sharp primitives don't automatically
> make a good UI — our old GUI used fine primitives and still looked "chán, không
> thuyết phục" (dull, unconvincing). This chapter is about the *system* that turns
> materials into a UI that reads as one considered thing: a **design system**.

---

## 1. Why "programmer UI" looks unconvincing

The old widgets each chose their own values inline: a grey here, an 8px pad there,
white text at one size everywhere, a 1px hard border, no depth. Nothing was
*wrong*, but nothing was *related* either. The eye reads unrelated values as noise
— "made by an engineer", not "designed". Three specific tells:

1. **No hierarchy.** All text the same size/colour, so nothing says "I'm the title,
   read me first."
2. **No single accent.** If everything can be blue, nothing stands out; the eye has
   no anchor for "the important action."
3. **Ad-hoc spacing & flat surfaces.** Random gaps and borderline-only panels give
   no rhythm and no sense of depth (what's on top of what).

A design system fixes all three by replacing free choice with a **small set of
named, related values** that every widget must draw from.

---

## 2. Tokens: the single source of truth

A **token** is a named design value. Instead of `0xFF3C4049` sprinkled across the
code, there is one `theme::ctrl`, and every control uses it. Change the token,
change the whole UI. Ours (`engine/ui/theme.hpp`) are grouped by role:

```
surfaces   bg  elevated  titlebar  border          ← "how raised is this?"
controls   ctrl  ctrl_hover  ctrl_press  disabled  ← by interaction state
text       text  text_dim  text_muted  on_accent   ← by emphasis
accent     accent  accent_hover  accent_press      ← the ONE hot-action colour
semantic   success  warn  danger                   ← meaning, not decoration
```

Two rules make tokens work:

- **Widgets never use literals.** A button that hard-codes a colour breaks the
  system the moment the palette changes. It must ask for `theme::ctrl`.
- **Roles, not values.** The token is named for *what it's for* (`text_muted`), not
  *what it is* (`grey6b`). That's why swapping the palette later doesn't rename
  anything.

### The one-accent rule

The most load-bearing decision: **one hot-action colour per screen.** Our accent
(`#5AAAE6`) marks the primary action and nothing else competes with it — the
primary button, the checked box, the slider's filled track and knob. Everything
else is neutral. That single point of colour is what makes the UI feel
intentional; scatter the accent and the effect evaporates. (Research on game HUDs
says the same: "one Hot-Action color… used for highlight states, titles, and
important interactions.")

---

## 3. Scales: rhythm you don't have to think about

Beyond colour, three ordered scales remove per-widget guesswork:

- **Spacing scale** `4 · 8 · 12 · 16 · 24`. All gaps/pads pick from these, so
  everything lines up on a shared grid. Arbitrary 7px/11px paddings are what make
  layouts feel "off" without you knowing why.
- **Type scale** `12 · 14 · 16 · 20 · 28` (caption/body/label/title/display).
  Distinct steps create hierarchy at a glance — a 20px title next to 14px body
  *reads* as a title.
- **Radius** `6` (controls) · `10` (panels). Consistent rounding is a signature;
  mixed radii look accidental.

Scales are usually roughly geometric (each step ~1.2–1.5× the last) because the eye
perceives ratios, not absolute differences. Small, fixed sets beat "pick any
number" every time.

---

## 4. Elevation and state: depth and feedback

**Elevation.** Real interfaces layer surfaces: the page, a card on it, a menu on
that. We convey it two ways — a slightly lighter surface (`elevated` > `bg`) and a
**soft drop shadow** under panels (Ch.70's `drop_shadow`). Shadow = "this floats
above the rest," which instantly organizes the screen into foreground and
background.

**State.** A control must visibly answer "can I click this, and am I about to?"
Every interactive token comes in a state ramp:

```
idle → hover → active (pressed) → disabled
ctrl   ctrl_hover  ctrl_press      ctrl_disabled     (neutral buttons)
accent accent_hover accent_press   (muted)           (primary buttons)
```

The interaction *logic* (the hot/active model from the IMGUI chapter) is unchanged
— only the *drawn colour* keys off the state. That separation is why the headless
UI tests still pass: state → colour is pure presentation.

---

## 5. Applying it: the widget layer

`engine/ui/ui.cpp` now draws every widget from tokens + AA primitives, while the
hot/active/hovering logic stays byte-identical:

- **panel** → `drop_shadow` + `fill_round_rect(elevated)` + `draw_round_rect(border)`
  + optional title (in `sz_title`) with a hairline divider.
- **button** → `fill_round_rect(radius_sm)` in the state colour; `primary` swaps the
  neutral ramp for the accent ramp; `disabled` uses the muted ramp and ignores
  input; label centred in `sz_label`.
- **checkbox** → rounded box + accent fill when checked.
- **slider** → rounded groove (`track`) + accent filled portion + a circular knob
  that brightens on hover; the value in `sz_caption`.

A scene turns the whole thing anti-aliased just by handing the renderer a font
(`gfx.set_font(ctx.font, sz)`); with no font, text falls back to 8×8 and everything
still works — the system degrades, it doesn't break.

### Before → after

```
BEFORE                              AFTER
┌──────────────┐ hard 1px border   ╭──────────────╮ soft shadow, rounded, elevated
│ [ Normal ]   │ flat grey box     │  ╭────────╮   │ rounded, hover/press states
│ [x] Enabled  │ 8x8 blocky text   │  │ Primary│←──┤ ONE accent = the key action
│ Speed 0.6    │ one text size     │  ╰────────╯   │ AA Inter text, type hierarchy
└──────────────┘ no depth/hierarchy╰──────────────╯ grid spacing, circular knob
```

The `test_ui_golden` case renders exactly this and dumps a PPM; eyeballed, it
matches the approved "modern dark + one accent" direction.

---

## 6. Pitfalls

- **Leaking literals.** One inline colour and the palette is no longer swappable.
  If you're typing a hex in a widget, it belongs in `theme.hpp`.
- **More than one accent.** The fastest way to make a UI look amateur again. Keep
  it to one; use `semantic` colours only for genuine success/warn/danger, sparingly.
- **Ignoring the scales.** A one-off 7px gap or a 17px font undoes the rhythm.
- **Contrast failures.** Dark-on-dark or low-contrast text is unreadable; enforce a
  ratio floor (the theme test checks ≥4.5:1 for body text — WCAG AA).
- **Coupling logic to style.** Keep state→colour separate from the interaction
  model, or you can't test the logic headless.
- **Radius > half the box / shadow over content.** Clamp radii; draw shadows before
  the panel so they don't tint what's on top.

---

## 7. Glossary

- **Design system** — a coordinated set of tokens + rules that make a UI coherent.
- **Token** — a named design value (colour/space/size/radius) used everywhere in
  place of literals.
- **Type scale / spacing scale** — small ordered sets of sizes/gaps for hierarchy
  and rhythm.
- **Accent (hot-action) colour** — the single colour reserved for the most
  important action/highlight.
- **Elevation** — the sense of surfaces stacked in depth (lighter surface + shadow).
- **State ramp** — the set of colours for idle/hover/active/disabled of one control.
- **Contrast ratio** — perceptual luminance ratio between text and its background
  (WCAG AA body ≈ 4.5:1).

---

## 8. Exercises

1. **Theme swap.** Add a light theme (a second set of token values behind the same
   names) and switch at runtime. What breaks if any widget kept a literal?
2. **Density mode.** Add a "compact" spacing scale (multiply the scale by 0.75) for
   dense editor panels. Where should the multiplier live?
3. **Focus ring.** Add keyboard focus with an accent outline (`draw_round_rect`).
   How does it interact with the hover/active states?
4. **Semantic buttons.** Add `danger` buttons (delete actions). When is a second
   colour justified, and how do you keep it from competing with the accent?
5. **Contrast lint.** Extend `test_theme` to check *every* text token against every
   surface it's drawn on. Which pair is closest to failing, and why?

---

*This completes the engine-level UI overhaul: crisp AA text (68), universal +
analytic anti-aliasing (69–70), and a real design system (71). Chapter 72 applies
it to the games' HUDs and verifies it on the web.*

# Chapter 72 — Porting the design system to the web dashboard

> **Where we are.** Ch.71 built a design system for the *native* IMGUI. The BaaS
> operator dashboard (`baas/web/dashboard.html`, Ch.62) is a separate surface —
> hand-written HTML/JS served by the gateway — and it still looked like a
> programmer's default. This short chapter ports the *same* design language to the
> web, and shows that a design system is a set of decisions, not a pile of code:
> the tokens transfer even though the rendering technology is completely different.

---

## 1. Same decisions, different medium

The native UI rasterizes pixels; the browser lays out the DOM. But the *design
system* — the palette, the one accent, the spacing/type scales, rounded elevated
cards, state ramps — is medium-independent. Porting it is mostly a translation of
tokens:

| Native (`theme.hpp`) | Web (CSS custom properties) |
|---|---|
| `constexpr Color accent = 0xFF5AAAE6` | `--accent:#5AAAE6` |
| `space_sm/md/lg …` | `gap`/`padding` in 8/12/16px |
| `radius_sm/md` | `--r-sm:6px; --r-md:10px` |
| `fill_round_rect` + `drop_shadow` | `border-radius` + `box-shadow` |
| state ramp (idle/hover/press) | `:hover` / `:active` rules |

One difference works in the web's favour: **there is no aliasing to fight.** The
browser anti-aliases text and rounds corners for free, so Ch.68–70's machinery
(fonts, SSAA, Wu, coverage) has no web analogue — the design system is the whole
job here.

---

## 2. Tokens as CSS custom properties

The palette lives once, in `:root`, exactly like `theme.hpp` is the single source
of truth natively:

```css
:root {
  --bg:#12141C; --elevated:#1B1E28; --surface:#2A2F3C; --border:#2A2E3A;
  --text:#E6E9F0; --dim:#A8AEBE; --muted:#6B7180; --on-accent:#0B1017;
  --accent:#5AAAE6; --accent-hover:#82C8FF; --accent-press:#3E86BE;
  --r-sm:6px; --r-md:10px;
}
```

Every rule references `var(--…)` — never a raw hex. Change one line, re-theme the
whole page (the "theme swap" exercise from Ch.71, now trivial on the web).

Body font switches from the old monospace to a **system UI sans stack**
(`-apple-system, Segoe UI, Roboto, …`) — zero payload, native-feeling, and it
gives the type hierarchy real weight. Monospace is kept only where it earns its
place: the one-time secret `.keys` chip and the realtime log.

---

## 3. Components, mapped

- **Header** → sticky bar, an accent "status dot" (glow via `box-shadow`), title +
  muted subtitle, hairline bottom border.
- **`section`** → elevated rounded **card** with a soft `box-shadow` and an accent,
  uppercase, letter-spaced title (the type-hierarchy cue).
- **Fields** → uppercase dim labels + dark inputs that grow with flexbox and show
  an **accent focus ring** (`box-shadow:0 0 0 3px var(--accent-soft)`) — the web's
  version of a focus state.
- **Buttons** → primary = accent, `.sec` = neutral surface, with `:hover`/`:active`
  ramps. The *one-accent rule* holds: Create/Save/Load are accent, everything else
  is neutral.
- **Tabs** → a segmented row; the active tab (JS still toggles `.active`) gets
  accent text + an accent underline instead of a filled box.
- **Tables** → uppercase muted headers, row hover, hairline separators.

---

## 4. The hard constraint: don't touch behaviour

The dashboard's JavaScript addresses the DOM by **id, class, `data-tab`, and
`onclick`** (`$("adminSecret")`, `.tabs button.active`, `rows()` emitting
`<table>`, `.muted`, `.keys`). The redesign is therefore **CSS + static-markup
only**: every one of those names is preserved byte-for-byte, so not a single line
of logic changed. The verification was a browser load with the console open — no
errors from renamed selectors (the only messages are the expected `404`s from
calling the API with no backend attached).

This is the payoff of styling by *class/role* rather than by hard-coded element:
the presentation layer swaps out from under working logic without a ripple.

---

## 5. Pitfalls

- **Restyling by element, not role.** If the JS had inlined styles or keyed off
  colours, the CSS-only swap wouldn't have been possible. Keep behaviour and
  presentation separated by stable class names.
- **Re-introducing literals.** A raw `#2b60c9` in one rule and the theme is no
  longer swappable — everything goes through `var(--…)`.
- **External assets.** The page is served same-origin by the gateway with no CDN;
  embedding a web font would add payload and a request. The system font stack keeps
  it self-contained (an `@font-face` Inter is a noted follow-up, not a need).
- **More than one accent.** Same rule as native — resist it.

---

## 6. Glossary

- **CSS custom property** — a `--name` variable in CSS; the web equivalent of a
  design token.
- **System font stack** — a font-family list of OS-default UI faces; renders with
  zero download.
- **Focus ring** — the accent outline showing which field has keyboard focus.
- **Separation of concerns** — behaviour (JS) and presentation (CSS) decoupled via
  stable selectors, so either can change without the other.

---

## 7. Exercises

1. **Theme swap.** Add a `data-theme="light"` block that overrides `:root` tokens;
   toggle it with a button. What, if anything, in the markup needs to change?
2. **Toast variants.** Give `toast()` a type (info/success/error) that maps to the
   semantic tokens (`--success/--warn/--danger`). Where does the class get set?
3. **Responsive tables.** On a phone the wide tables overflow. Make them scroll
   inside a container without breaking the layout.
4. **Shared token source.** `theme.hpp` and `:root` now duplicate the palette.
   Sketch a build step that generates the CSS variables from the C++ tokens so they
   can never drift.

---

*This closes the UI/UX overhaul across both surfaces: the native engine UI
(ch.68–71) and the web operator dashboard (this chapter) now speak one visual
language — "modern dark + one accent" — even though one paints pixels and the other
lays out the DOM.*

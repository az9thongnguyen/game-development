# Web Admin Dashboard Redesign — Design Spec

> Sub-project #2 of the UI/UX overhaul ("cả hai, tuần tự"). Applies the SAME
> design language approved for the native UI ("modern dark + one accent") to the
> web operator dashboard. Native spec:
> `2026-07-11-native-ui-ux-overhaul-design.md`.

## Problem
`baas/web/dashboard.html` works but looks "chán/không thuyết phục": a monospace
body font, flat 1px-bordered boxes, ad-hoc greens/blues, cramped inline-block
form rows, plain tables. No design system. (Web is vector — no aliasing issue.)

## Goal / non-goals
- **Goal:** restyle it to the design system — token palette, one accent, a proper
  UI font stack, spacing/type scales, rounded cards with subtle elevation, styled
  inputs/buttons/tabs/tables, responsive layout. Keep it self-contained (no
  external assets — served same-origin by the baas).
- **Non-goals:** any change to behaviour/JS logic, the admin API, or endpoints.
  Every element ID, CSS class, `data-tab`, and `onclick` the script relies on
  stays **byte-identical** in name.

## Approach
CSS-only rewrite of the `<style>` block + light restructure of the *static* HTML
(header, form rows) — the JS-generated markup is styled purely via its existing
classes (`.sec`, `.tabs`/`.active`, `.muted`, `.keys`, `table/th/td`, `#toast`,
`#rtlog`). Tokens become CSS custom properties mirroring `engine/ui/theme.hpp`:

```
--bg #12141C  --elevated #1B1E28  --surface #2A2F3C  --border #2A2E3A
--text #E6E9F0  --dim #A8AEBE  --muted #6B7180  --on-accent #0B1017
--accent #5AAAE6  --accent-hover #82C8FF  --accent-press #3E86BE
--success #4CC38A  --warn #E5B454  --danger #E5657A
--r-sm 6px  --r-md 10px   spacing 4/8/12/16/24   type 12/13/14/16/20/26
```

Components: sticky header w/ accent dot + subtitle; `section` = elevated rounded
card + soft shadow + accent section title; labels = small uppercase dim; inputs =
dark field, rounded, **accent focus ring**; `button` = accent (primary), `.sec` =
neutral surface, hover/active states; `.tabs` = segmented row, `.active` = accent
text + underline; tables = uppercase muted header, row hover, rounded container;
`.keys` = monospace success chip; `#toast` = elevated card w/ shadow. Monospace
kept only for keys and the realtime log; body is a system UI sans stack.

## Verification
Serve `baas/web/` statically, open `dashboard.html` in Chrome (chrome-devtools),
screenshot the shell (header + both cards + tabs). API calls fail without a
backend — expected; the static chrome/forms/tabs must render correctly and match
the native look. Confirm no JS errors from renamed selectors (there are none).

## Out of scope / follow-ups
Embedding Inter via `@font-face` (system stack is fine + zero payload); a light
theme; wiring the dashboard's realtime log colours to semantic tokens (cosmetic).

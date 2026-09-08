# 149 — Two thumbs, one hierarchy

The farm looked playable on a phone because a finger became a mouse. That shortcut
carried one position, so it could never describe the gesture the game asks for most:
hold a direction with one thumb and press a verb with the other. The same review also
showed a second kind of ambiguity. Studio buttons knew only `primary=true`; cards,
meters, destructive actions and icons had no shared visual language.

These are both input problems. One loses which finger meant what. The other loses what
an action means before a person presses it.

## Contacts are a snapshot, not callbacks

`platform::InputState` now carries ten fixed `TouchContact` slots. Each contact has a
stable SDL finger id, framebuffer coordinates and independent down/pressed/released
state. A released contact survives for its edge frame and is removed at the next
`begin_touch_frame`. The fixed array keeps the snapshot trivially copyable and adds no
allocation to the platform loop.

SDL finger coordinates are normalised, then clamped into logical framebuffer space.
Real mouse and keyboard events select `KeyboardMouse`; finger events select `Touch`.
Synthesised mouse events do not steal that identity.

`touch_input.hpp` is the adapter between the platform snapshot and the existing pure
`touch::Pointer`. When real contacts exist it omits the synthesised mouse, otherwise one
finger would become two actions. Farm and Creatures still know no SDL types. Their pure
`read` functions accept a pointer span and merge intent: edges are ORed, opposite
directions cancel, duplicate directions clamp back to one, and the first selected cell
or tool wins deterministically.

## Layout includes its neighbours

Farm's layout now reports the top HUD and keyboard-hint region beside the hotbar, not
only controls. The renderer consumes those rectangles and a property sweep proves that
no live control covers either neighbour across thousands of viewport sizes. This is
the same rule as renderer and hit test sharing control rectangles, extended to the
content a control must not hide.

Pressed-state rendering reads every contact too. Holding east no longer leaves the
button visually idle merely because the second finger is the one SDL exposed as mouse.

## Meaning before colour

The UI core gained `ButtonKind` (`Neutral`, `Primary`, `Danger`, `Ghost`) and
`ButtonOptions` for icon, enabled state and shortcut. Existing boolean calls remain a
compatibility wrapper, so adoption can be deliberate rather than a flag-day rewrite.
`card`, `meter` and a small vector `Icon` vocabulary use only the CPU renderer and theme
tokens. Studio's Play toolbar and the release Hub are the first consumers.

The palette moved to deeper navy surfaces, a brighter periwinkle accent and clearer
semantic colours. A golden frame checks exact interior tokens for every new hierarchy,
including both sides of meter clamping and drawing outside its bounds.

## The guard that had to lift

Twenty-one mutations ran. The first pass killed 20. Removing the meter clamp survived:
the existing checks looked inside the meter, where a 120% fill still looked full. The
missing assertion was immediately after its right edge. Once that pixel had to remain
the card surface, the second run killed all 21.

## What is verified, and what is not

Verified:

- `test_ui`, `test_ui_golden`, `test_farm`, `test_farm_scene`,
  `test_creature_world`, `test_creatures_scene` and `test_shell_golden` pass.
- Two scene journeys use real independent contacts: direction + seed in Farm, and
  Save + Act in Creatures. Reversed contact order is covered in both pure readers.
- A geometry sweep keeps Farm controls off the HUD, hotbar and hint region.
- UI component and Studio/Farm golden frames were inspected after the palette change.
- **21 mutations, 21 killed** after the one meter-boundary survivor was covered.
- **95/95 `ctest`, twice** (24.00 s and 24.03 s; the BaaS cases ran outside the
  filesystem sandbox because they bind loopback ports).
- Emscripten 3.1.61 compiled and linked `demo.html`.
- The Creatures golden path inspected, re-published and verified release
  `41369a0bce39b6bb`, promoted development to preview, reached a shippable Hub and left
  zero `.tmp` files.
- Release render benchmark, 200 frames: Studio ss=2 1.17 ms median, Farm 0.75 ms and
  Creatures 1.16 ms; all seven configurations stayed under the 8 ms budget.

Not verified yet:

- No two-finger gesture has been driven on physical hardware or through a browser;
  SDL event translation is compiled, while the end-to-end evidence is headless.
- `ui::Context` still owns one pointer. Multi-touch currently serves the two game
  control layers, not simultaneous Studio widgets or canvas gestures.
- `InputDevice` records the active source but does not yet auto-hide touch controls.

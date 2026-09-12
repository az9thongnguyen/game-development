# 151 — Four cards, not one sentence

Farm and Creatures were playable, but their most important state still looked like
instrumentation. Farm compressed day, season, time, energy, money and cloud state into
one line. Creatures did the same with species, level, health and progress. Their touch
buttons made a phone usable only after the player translated `Z`, `Q`, `F5`, `O` and `S`
back into verbs.

This slice changes no simulation rule. It makes the rules already present readable.

## Status is layout too

Farm's one `layout()` now owns four status rectangles: calendar, energy, gold and cloud.
The renderer turns them into compact surfaces, including numeric energy, a clamped meter
and a status dot. Below 480 logical pixels the rectangles are empty and the compact text
fallback remains; the boundary is tested on both 479 and 480.

Creatures applies the same rule to different information. Its overworld layout owns the
lead-creature card, health meter, progress region and message toast. Battle layout owns
both combatant cards as well as the sprites and command panel. A sweep of more than 800
viewport combinations proves those regions remain on screen and do not overlap either
thumb, either sprite or the battle panel. The renderer and the geometry test read those
same boxes—there is no second coordinate table.

The battle menu now explains its four choices, moves show explicit PP, party rows show
current and maximum health, and names are presented as names rather than lowercase data
keys. Health uses the shared success/warning/danger vocabulary and both idle and pressed
control surfaces use theme tokens.

## A control names the action

Each game has a pure `label(Control)` next to its pure input reader. Keyboard shortcuts
remain in help text, but the touch surfaces now say `USE`, `SEED`, `SAVE`, `KEEP`, `TAKE`,
`PVP` and `ACT`. Tests pin these meanings independently of pixels. This is deliberately
not a shared game layout: chapter 124's distinction still holds—facts about a hand may be
shared, while each game owns its geometry.

## The web gate found yesterday's bug

The first 390×844 browser run failed before it reached either game. The player bar's
`min-height: 58px` used the default content-box model, so its vertical padding made the
bar 73 pixels high and left only 771 pixels for the stage. The controls had the same
problem: a promised 44-pixel target became 46 pixels after its border. Both now use
`border-box`.

The browser check now measures all three chrome controls. Back, Log and Fullscreen must
be visible, at least 44 CSS pixels and wholly inside the viewport; both axes must remain
scroll-free. Removing either `border-box` declaration makes the check fail with the
measured 771- or 783-pixel stage. A bounded, reported retry was also added around the
first WASM Save touch after one run arrived between polled frames; three subsequent
Creatures journeys passed on their first touch.

## Mutation and the almost-mutation

Twenty-seven single-token or single-value mutations covered semantic labels, every
layout threshold, overlap guards, hierarchy colours, meter tracks and both control
states. The first pass killed 26. Moving the message from x=168 to x=150 survived because
it still did not overlap anything; it was a weak mutation, not evidence about the guard.
Moving it to x=80 created the promised collision and the sweep killed it. Final result:
27/27 effective mutations killed, plus 2/2 web-shell mutations, with green post-restore
baselines.

## What is verified, and what is not

Verified:

- `test_farm`, `test_farm_scene` and `test_creatures_scene` pass, including both sides
  of the 479/480, 359/360, 399/400 and 279/280 layout guards and idle/pressed controls.
- **95/95 CTest passed twice** on the implementation commit: 37.07 seconds, then 23.65
  seconds.
- Native offscreen frames for Farm day/small/dialogue and Creatures
  overworld/battle/moves/acknowledge were inspected; no crop or overlap was observed.
- Emscripten linked the final player. Chrome at 390×844 completed real-touch journeys
  through Farm and Creatures; the latter walked into battle, ran, acknowledged and saved.
  The Collection journey still found five games and reached a running player.
- Release render benchmark, 200 frames: Farm 0.78 ms and Creatures 1.35 ms median;
  all seven configurations stayed below the 8 ms budget.
- Farm release `150f60129cb57b22` and Creatures release `41369a0bce39b6bb` both passed
  inspect, publish and parity verification. Creatures reached preview and a shippable
  Hub; zero `.tmp` files remained.

Not verified yet:

- No physical phone, screen reader or native window was used. At portrait width the
  correctly fitted 16:9 canvas is only 219 CSS pixels high, so a 44-logical-pixel game
  button displays at about 27 CSS pixels; the shell recommends rotation but does not
  solve that product limitation.
- Full battle cards intentionally disappear below 280 logical pixels, and Farm uses the
  compact status line below 480. Those fallbacks are tested for geometry, not reviewed
  as complete product layouts.
- The first-save retry addresses a measured frame-boundary flake, not persistent-storage
  failure. IndexedDB denial and quota exhaustion remain unexercised.

# 134 — A road is a road, and the map should say so

`assets/farm/theme.def` had this line in it:

```
autotile ground 2 path 0   # the worn path, sixteen pieces we drew
```

It is a good line. It says: cells holding 2 are a path, the path is a
sixteen-piece line set, and the set starts at index 0 of the `path` sheet. The
farm reads it and draws corners, T-junctions and end caps from each cell's four
neighbours. Chapter 123 built that and it works.

The problem is *where it lives*. It lives in one game's art file. So when you
open the same map in the Studio's Map workspace — the only map editor this
project has, since chapter 132 — the editor draws a flat blue square for cell
after cell, because it has never heard of `theme.def` and would not know what to
do with it if it had. You author a road by painting squares and find out what
the road looks like by quitting the editor and running the game.

This chapter moves one word.

---

## Which half is the world and which half is the art

The `autotile` line is doing two jobs at once.

*This material is a line, not a region* — a road, one tile wide, that connects
along its four sides. That is a fact about the **world**. It is true whether the
sheet exists, whether it is 16 pieces or 16 hand-placed tiles, whether anyone
ever draws it at all.

*The set starts at index 0 of the `path` sheet* — that is a fact about the
**artwork**, and it belongs exactly where it was.

So the first half moves into the map:

```
layer ground tiles -
rule 2 line
row 1 1 1 1 1 …
```

and the second half stays in the theme, which now says only `tile ground 2 path 0`.
Nothing is duplicated. The renderer holds both and adds them together.

The payoff is that "is this a road" is now readable by anything that can read a
map — including an editor that knows nothing about farms.

## One implementation, finally

`farm::line_piece` was the neighbour scan, and it was a copy. It only knew about
lines (the 47-piece rule was in `tilemap/autotile.hpp`, unreachable from it), and
it lived in `games/farm`, where no editor could call it.

It is `tilemap::rule_piece` now:

```cpp
// Which piece of its set the cell at (x,y) wears. **0 when its value has no rule**,
// so a caller can always write `base + rule_piece(...)` without asking first.
int rule_piece(const Map& m, const std::string& layer, int x, int y);
```

That "0 when there is no rule" is the whole ergonomics of it. The farm's renderer
used to branch:

```cpp
const int index = a->index + (a->autotiled ? line_piece(map_, layer, id, x, y) : 0);
```

and now does not:

```cpp
const int index = a->index + tilemap::rule_piece(map_, layer, x, y);
```

The branch did not disappear — it moved into the function, once, where the two
rules already lived side by side.

Its test moved with it, including the five cells of the farm's own committed map
that make that path a path rather than a row of squares. A test that travels with
the function it checks is a test that outlives whichever module owns it.

## Two format decisions

**A file is written at the lowest version that can express it.** `kFormatVersion`
is 2 now, but `to_text` writes `map2 1` for a map with no rules. Every existing
map is byte-identical and its release id does not move for a feature it does not
use. Only `farm_home.map2`, which actually gained a rule, became a v2 file.

That is only meaningful because of the guard that was already there: a version
from the future is *refused*, not half-read. An old binary meeting a file with
rules must stop, rather than drop them and write the loss back.

**Rules come before the grid, and are refused after it.** They say what the
numbers in the grid mean. A file that introduced one halfway down would have meant
two different things in its two halves.

## `autotile` is gone, not deprecated

The theme grammar refuses unknown records — it always has, on the argument that a
typo in an art file reads exactly like "no art yet" and must not be silent. So
deleting the `autotile` keyword makes an old `theme.def` fail loudly rather than
draw the road's base tile in all sixteen positions, which is a picture that looks
like art nobody finished.

Same rule as chapter 132's deletion of `fps::to_text`: remove the writer, don't
deprecate it.

## Seeing it

The Map workspace has no tileset renderer. Ids draw as a stable palette of flat
colours, which is honest about what the editor knows — but it means "show the
resolved tile" was never an option here.

So it draws the **connections** instead: a stub toward every neighbour that
continues, and, for a region, a pip in each corner the piece actually turns.

The pips come from `autotile_canonical`, not from the raw eight-bit mask. A
diagonal whose two adjacent cardinals are not both set cannot change which of the
47 pieces gets picked, so drawing it would promise a difference the renderer will
not make. The editor shows the piece the game will choose, in the only vocabulary
this canvas has.

One row in the inspector authors it: `Rule  none | line | blob`, cycling for
whatever the brush is holding. Brush 0 is refused — empty is not a material — and
the button carries the reason in its label, because a disabled control with no
reason reads as a bug.

## Undo is per material, not per cell

```cpp
std::optional<doc::Command> set_rule(tilemap::Map& m, const std::string& layer,
                                     std::int32_t value, tilemap::RuleKind kind);
```

One command for the whole material. A rule changes how every cell of that value is
drawn and none of them move — because the picture was never stored. Undo restores
the rule and the picture follows.

The merge key splits the same way `set_entity_prop` splits: the first rule on a
material stands alone, every later change merges. So `none → line → blob → none`
is two undo steps, not four and not one: the cycle, and the fact that the material
has a rule at all.

## Mutations, and the guard that was tested by accident

22 single-token mutations; five lived the first pass.

**`a rule after the grid is accepted`** looked tested and was not. The map in that
assertion was one row tall, so by the time the stray `rule` line appeared the grid
was already complete and the *outer* parser rejected it as an unknown directive —
a different guard, firing for a different reason, producing the same green. The
case the guard is actually for is a rule *between* rows, which needs a map at
least two rows tall. A test whose subject never runs is the most expensive kind
of green there is.

**`out of bounds connects`** is a real question with a subtle answer. `Map::at`
returns 0 off the map, and 0 can never carry a rule, so for every cell
`rule_piece` is ever asked about the bounds check changes nothing. It is not
redundant, though: `neighbour_mask` is public and its documented contract covers
an *empty* cell, where 0 == 0 would report the void beyond the edge as more of
itself. So the guard stayed and the contract got the test it never had.

**`brush 0 may carry a rule`** exposed a guard that existed only in the widget.
The button is disabled for brush 0, so removing the check inside `cycle_rule` was
invisible — through the button. The command palette and the keyboard do not go
through the button. The test now calls the operation directly and asserts it
refuses with a reason, which is what the D-rule has been saying since chapter 111.

**Two visual survivors**, `blob pips ignore canonicalisation` and `connectors
drawn for unruled cells`. Both are things only the screenshot could see — and the
answer was not another whole-frame comparison, which proves the picture *changed*
and never that it is *right*. Three specific pixels, each one a sentence: the
centre of an unruled cell is its own colour; the centre of a ruled one is ink; and
the corner of a cell whose diagonal is not backed by its cardinals is not ink.

## What it cost

| | before | after |
|---|---|---|
| implementations of the neighbour scan | 2 | 1 |
| places that know a path is a path | 1 (a game's art file) | 1 (the map) |
| things that can read it | the farm | anything with a `tilemap::Map` |
| autotile kinds reachable from an editor | 0 | 2 |

## Not verified

- **No 47-piece artwork exists.** A `blob` rule is authorable, saved, previewed
  as connectors and resolved by `rule_piece` — but nothing in the repo draws a
  region set, so the only material shipping with a rule is the farm's line path.
  The 47-piece sheet is the next thing this needs, and it is a content task, not
  a code one.
- **The Map workspace still has no tileset renderer.** It draws connectivity, not
  art. Seeing the actual tiles is a different slice.
- **Rules are per (layer, value) and materials do not know about each other.**
  There is no "grass meets sand" — the kind of cross-material rule stack a mature
  editor has.
- **No per-piece override.** If piece 11 of a set is wrong, you fix the sheet.
- **`iso` and the raycaster do not read rules** — neither has an autotiled
  material, and `fps::Map` is still its own uint8 grid.
- The `decor` layer of the farm has no rules; only `ground` does.
- **The browser touch gate was flaky and is now retried, not fixed.** One run in
  five failed with the player not moving, and passed on a plain retry with the
  identical value (`px 4 -> 8`) — a stall, not a regression. The check now holds
  the button up to three times and reports how many it took, so the claim is
  "holding the button moves the player" rather than "it moves within one
  particular 700 ms window". What actually stalls the first frames has not been
  diagnosed.

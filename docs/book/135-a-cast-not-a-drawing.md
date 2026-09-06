# 135 — A cast, not a drawing

The farm's player was a coloured circle. So was Anna. They had been circles since
chapter 124 gave the *tiles* art, because the tiles could be cut out of a pack and
the people could not: Kenney's Tiny Town is buildings and terrain and has nobody
living in it.

The obvious fix is to draw two characters. This chapter does something else, and
the difference is the whole point.

---

## The bar a fourth door has to clear

`CLAUDE.md` has said "exactly **three** offline doors" into `.hrt` since chapter
125, and the number is load-bearing: every door is another way a picture can get
into the repo, another mark provenance has to recognise, another origin in the
ledger, another thing a test has to re-bake. A fourth door is a decision, not a
side effect.

So the bar was: **it answers a question none of the other three can.**

The first three all answer *where did this picture come from*. `asset.import`
says "from outside"; `asset.texture` says "from twelve numbers"; `asset.pixels`
says "somebody typed it". Every one of them produces **one picture**.

`asset.mix` answers a different question: **make me a hundred sprites that all
belong together.**

That is not a smaller version of drawing. It is the opposite trade. A brush gives
you total freedom and, for one person, a cast that does not look related — because
consistency across sixty sprites is a thing you have to *maintain*, by hand, and
nobody does. Parts take the freedom away and hand back the resemblance for free:

```
mix1
name farm_anna
size 16 16
sheet parts textures/parts_farm.hrt 16
part parts 1 at 0 0
part parts 0 at 0 0
part parts 2 at 0 0
swap 5b3a29 8a6f3a
swap 6e8fb5 b5566e
```

Nobody drew Anna. She is the player's body and the player's head, plus a hat, plus
two colour swaps. She matches the village by construction rather than by care, and
the next villager costs three lines rather than a drawing.

`textures/parts_farm.pix` is the whole art budget for both of them: a body, a
head, a hat. None of the three is a character.

## Held to the same standard, and it needed to be

A `.mix` is a **source**. It stays out of the manifest, `provenance_core` derives
`mixed` from the sibling, and `test_commands` re-bakes it and byte-compares —
exactly what a `.recipe` and a `.pix` get.

It re-bakes **both** sprites, not one. The entire claim of a mixer is that the
second character is free; a test that re-baked only the first would not be testing
the part that is new.

And a mix is the door where the output sits furthest from anything anybody drew,
so "it looked right once" is the weakest evidence in the repo. It is also the door
where a silent near-miss is most likely: a typo in a sheet name reads exactly like
"that part has no art yet" and composes a sprite with a hole in it. So the parser
refuses it, by name, with a line number — the same argument `.pix` made in chapter
125, and the same list of refusals:

| refused | because |
|---|---|
| a part naming an undeclared sheet | the typo that must never be silent |
| a mix with no parts | a file somebody abandoned, not a document |
| a second sheet under one name | one of them loses and file order decides which |
| a swap of a colour to itself | a line that does nothing is a line that meant something |
| a part **entirely** off the canvas | a part you cannot see is a typo |
| a part **partly** off the canvas | *allowed* — that is how a hat hangs over the top edge |

`mix::compose` takes its part images from a resolver the caller supplies, so
`mix_core` does no I/O and its own tests compose from pixels they invented. Parts
stack in declaration order with real alpha-over; swaps run last, on the finished
canvas, so recolouring a shirt does not depend on which part drew the pixel.

## A workspace whose job is to make something

Map, Scene and Pixels all open a thing that exists and change it. The Mixer opens
a handful of parts and produces something that did not exist, and that is why it
is a workspace rather than a mode of the Pixel editor. The two answer different
questions — *is this pixel right* versus *which of these, and in what colour* —
and only the second has a small, closed set of answers.

The ramp a recolour cycles through is **six colours, fixed**. That looks like a
missing feature and is the feature: a colour picker with sixteen million answers
hands back exactly the freedom this tool trades away.

**Save and Bake are two buttons because they are two things.** Save writes the
`.mix` — the source, the thing under version control. Bake writes the `.hrt`, and
it does it by running `asset.mix`, the same command the CLI runs, so the button
and the terminal cannot produce different bytes. A single button would make every
keystroke rewrite a committed binary and would hide which of the two files is the
artefact. A test asserts both halves: saving leaves the `.hrt` untouched, and
baking matches the CLI byte for byte.

The set of files it can open is **derived**: a texture is mixable when a `.mix`
sits beside it — the same sibling rule `provenance_core` uses. A second list is a
second thing to keep in step, and the day they disagree the editor is showing a
file the ledger does not believe in.

## Two things found in the right order

**The parts strip was drawn and completely inert.** Every piece of the loop
existed — the rects, the hit arithmetic, `add_part`, the undo command — and
nothing called any of it, because `update()` only ever hit-tested the preview. It
looked perfect in a screenshot.

That is the fourth chapter in a row with this shape (126: a drawn d-pad behind a
modal's early return; 127: a Create button that could not be pressed; 132: a
Facing button writing a key nothing read). What caught it here is a test that
clicks *where a hand clicks* rather than calling the function behind the button —
the habit chapter 133 turned into a rule.

**The chequerboard was two dark greys, so the character had no legs.** Dark
trousers on a dark backing is the same picture as nothing at all. A chequerboard
exists to distinguish *transparent* from *black*, and two dark tones distinguish
neither; mid-greys are the only pair that both ends of a palette read against.
Two inspector captions also ran off the panel edge — an instruction clipped
mid-word is one nobody can follow. Both were only visible in a rendered frame.

## And a comment that predicted its own failure

`test_shell_golden` clicked the second workspace tab at `(2i+1)/2n` across the tab
row, with `n` written down as `3`. Beside it:

> the fraction 3/4 meant "the second of two", and it silently became "the third of
> three" the day a workspace was added.

A fourth workspace arrived, and the literal `3` broke in exactly the way its own
comment described. Both sites now ask `sc.workspace_count()`. A number a test can
ask for cannot go stale — and a comment that knows the failure mode is a request
for a fix, not a substitute for one.

## Mutations, and the one that mattered most

Twenty-one single-token mutations; six lived the first pass, and one of them was
not a test gap at all — it was the whole slice going unchecked.

**`the player never wears its sprite`** replaced the actor draw with `if (false)`
and every test stayed green. The consumer — the entire reason a fourth door was
worth opening — had no assertion on it at all. `test_farm_scene` counted the flat
colours of the *tiles* and had never counted the flat colour of the *people*.

The first fix did not kill it either, and that is the more useful half of the
story. Counting "the parts sheet's skin tone appears" passes while the player is
missing, **because Anna is made of the same parts**. Two characters who share a
body, a head and a skin tone share every colour you would naturally count. What
separates them is the one thing that differs: the player's tunic is the colour the
sheet was drawn in and Anna's is a swap of it. So the assertions are now the
player's circle fill (**zero**), the NPC's circle fill (**zero**), the skin, the
player's **unswapped** tunic and Anna's **swapped** one — and that last pair also
separates "the compose ran" from "the swap ran".

The lesson is older than this chapter and keeps arriving in new clothes: a count
of something two subjects have in common is not a check on either of them.

**`any file is a mix file`** made the first record become the magic whatever it
said. The test that should have caught it (`size 4 4\n…` with no `mix1`) was
refused for a *different* reason — no `size`, because the `size` line had been
eaten as the header. The case that actually distinguishes them is a junk first
record on an otherwise complete file.

**`a short hex colour is accepted`** — seven hex digits is neither `rrggbb` nor
`rrggbbaa`, and "at least six" reads the wrong four bytes out of it. A colour that
is nearly right.

**`a swap overwrites alpha too`** — every other case in the file composites onto
something opaque, so alpha 255 comes out either way and the distinction is
invisible. Which is exactly when it goes wrong unnoticed: a swap changes a colour,
not a silhouette.

Two survivors were not test gaps:

- `tile_count on a too-small image` was **dead arithmetic**. An image smaller than
  one tile already answers 0 by integer division; the extra comparison could not
  be reached. Deleted — the fifth redundant guard this project has found by
  mutating a line and watching every test stay green (121, 122, 123, 125, this).
- `a strip click also recolours` is **equivalent under this layout**: the strip
  sits below the preview and the two regions do not overlap, so the ordering guard
  is insurance against a future layout rather than live behaviour. It stays, and
  saying so is part of the result.

19/19 after — once those two were removed for the right reasons, and once the
first attempt at the player check was found to be measuring the wrong colour.

## Not verified

- **Slots, anchors and rules do not exist.** The research sketch had `slot body
  required`, `attach body:head`, `rule wings requires body:not slime`. This mix
  is a flat ordered list of parts; nothing stops you adding two heads.
- **No frames, no animation.** A `.mix` makes one still image. Chapter 133's
  flipbook reads a vertical sheet, so the join is obvious and unbuilt.
- **No randomiser and no seed.** The bake is deterministic because it has no
  randomness at all, which is not the same as being seed-reproducible. "Randomize"
  is the button the Kenney mixers are actually known for and it is not here.
- **Swaps match an exact RGB.** Our `.hrt` is RGBA8, not indexed, so a "palette
  swap by index" is not available; a swap of a colour that appears in two parts
  changes both, and that is sometimes wrong.
- **The Mixer cannot create a `.mix`.** It opens the ones that exist, exactly like
  the Pixel workspace before chapter 131 gave it `asset.new`.
- **Part offsets can only be authored by hand.** The format carries `at x y` and
  the workspace always adds at `0 0`; there is no dragging.
- **Only the farm consumes it**, and only for two characters standing still.

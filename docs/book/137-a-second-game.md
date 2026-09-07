# 137 — A second game

Chapter 136 was a library and eighteen pictures. It ended with a debt written
down in plain words: *there is no game yet*, and that was only acceptable
because the consumer was the very next slice. This is it.

What arrives is `--project projects/creatures.gameproject`: a route with a
cabin on it, long grass that ambushes you, a battle screen you can drive with a
thumb, a party that levels and evolves, and a save. What is more interesting is
what the second game did to the code that already existed.

---

## The promise that came due

`CLAUDE.md` and `PROGRESS.md` had recorded the same thing twice, in different
words:

> On-screen controls exist only in the farm. If a second game needs them,
> `farm/controls.hpp` will have to split — but there is no second user, so it
> has not.

That is the right rule and it has a matching obligation: when the second user
turns up, you pay. This chapter is the second user, so two things moved.

**`engine/ui/touch.hpp`** takes the parts that are facts about a *hand*: `kBtn`
(44 logical pixels, because a finger is about 9 mm and below that a d-pad
becomes a game about aiming), the proportion rule for whether a pad may cover
the screen (at most half the width, at most two fifths of the height), the
d-pad's three-by-three arithmetic, and `Box`/`Pointer`.

**What did not move is the layout**, and that line is the whole point. The
farm's pad sits above a four-slot hotbar; this one sits above nothing, and the
bottom third of the screen belongs to a battle menu the farm does not have.
Sharing those rectangles would mean one screen is laid out for the other's
neighbours.

The rule both games follow — *one layout function, read by the renderer AND the
hit test* — is a **discipline, not a shape**. It is what prevents a control
drawn in one place and hit in another, which is invisible in a screenshot, and
each game gets it by having exactly one layout of its own.

**`farm::Theme` became `tilemap::Theme`.** "Which picture does this semantic id
wear" was never a fact about farming; it lived in `games/farm/` because that is
where it was needed first. Its tests stayed in `test_farm`, because the farm's
own `theme.def` is still farm content.

---

## Two more facts that belong in the map

Chapter 134 moved *"this material is a road"* out of a game's art file and into
the map, because the Studio has to be able to see it. The same question comes up
twice more here, and gets the same answer both times.

**Whether a tile ambushes you** is `ground` id 3, in the map.

**Which encounter table it rolls** is a `far` **mask layer** — not `x > 20`
written in `world.cpp`, and not a second ground id. A mask means the two bands
look identical, so the player finds out by walking into one; and it means the
map keeps saying it, where an editor could show it.

**Where the game begins** is `entity home`, so moving the cabin in an editor
moves the spawn — and the test asserts the tile under it is walkable, because
nothing else would notice a blackout that puts the player inside a wall.

---

## One stream

The overworld's encounter rolls and the battle's damage rolls come out of the
same `World::rng`. `begin_battle` hands it to the fight, and `settle` hands it
back.

Two streams would mean a save that restored the battle but not the walk that led
to it, and the first time anybody replayed a session it would diverge in the
grass. It is the same rule chapter 136 wrote about the RNG being state rather
than a service, one level up.

---

## Growing up, without being healed by it

The payoff of chapter 136's parts sheet is that evolution is **visible in play**,
and that costs two specific decisions:

- a level-up **regrows stats and keeps the damage taken**. A level that refilled
  HP would end every fight the moment anything levelled;
- an evolution does the same. Arriving bigger is the reward; arriving healed
  would make evolving a potion.

`Growth` reports the **first and last** species across a whole award, not the
last hop. One call can cross two evolutions, and *"blazehound became pyrewolf"*
is not the sentence to show a player who never had a blazehound.

**The save stores no stats.** They are a pure function of species and level, and
storing them would let a save disagree with the table it was balanced against —
a re-balance would silently not apply to anyone already playing.

---

## Three things a rendered frame found

None of them had an assertion. All three were visible in the first screenshot.

**Every battle button was near-black text on a near-black fill.** Present, laid
out, correct, unreadable. `ink() > 0` — the "is anything drawn here" check this
project has leaned on for four chapters — said fine, because the text pixels
*are* a different colour from the fill. `ink_on()` now picks the ink from the
fill's luminance, and the test measures actual **contrast** in the drawn pixels.

**`Back` was drawn on top of the player's creature.** It had its own arithmetic;
the creature had its own arithmetic; two rectangles decided in two places
eventually overlap, and the one that loses is whichever is drawn first. Both
creature rects moved into `Layout`, and the controls test now checks **every
pair** of rectangles in every battle mode for overlap — something no
single-rectangle assertion could do.

**The contrast metric itself needed two attempts.** The first used a 1% coverage
threshold and reported zero for every label, including ones plainly readable in
the screenshot: an anti-aliased word is spread over dozens of near-colours and
not one of them reaches 1% of a button. Four pixels of one exact colour is the
threshold that works.

---

## A balance claim, tested

One assertion in this chapter is about whether the game is any good:

> A level-5 starter, playing reasonably, must **win most of what the near grass
> throws at it.**

Not all of it — losing to a bad matchup is the game — but a route where the
first encounter usually ends the run is a route nobody gets past, and nothing
else here would ever say so. All three starters land at 75–87%, with real
losses. It runs all three because the answer must not depend on which one the
scene happens to hand out.

---

## The farm's proof does not transfer

The browser check was pointed at the second game and it failed. The game was
right.

There, you hold east and press Save. Here, holding east walks you into long
grass, something jumps out, and **the save button is gone** — during a battle
the screen has a menu instead. Four holds, `pos 4 6` every time. A probe
eventually printed what was actually happening: `phase=Battle`, `pos=8,6` — the
player standing in a fight the checker could not see.

So the creature game is proved by **finishing what it starts**: walk until the
battle screen announces itself, run from the fight, acknowledge it, and only
then save. That is a stronger claim than the farm's, because touch had to reach
the overworld, the encounter roll, the battle menu and the end screen.

It is the same script. The scene prints a **second** control line the first time
a battle exists — the farm needed one because everything it can do is on one
screen; this game has two, and a checker cannot aim at a screen nobody
described. Same rule as chapter 126: the numbers the check uses are the numbers
the renderer used.

---

## Thirty-eight mutations, and three assertions that passed for the wrong reason

The first run killed 26. Every one of the twelve survivors was a real gap, and
three of them are the expensive kind — the assertion existed, ran, and was
satisfied by something other than the thing under test:

| Survived | Why the test did not catch it |
|---|---|
| a blackout heals HP but not PP | the cragtitan the test loses to is so much faster that it one-shots the starter *before it ever swings*, so the party came out at full PP and the check compared full to full |
| a save from the future is read anyway | the future-save fixture had no party in it, so it was refused for being **empty** and the version check was never reached |
| a save may claim a party it does not have | refused because `active` was out of range for a party of zero — again, the wrong reason |
| your creature is never drawn | the battle screen's horizon runs straight through the creature's box, and two flat bands already clear a 200-pixel ink threshold |
| every level re-learns the whole list | `learned` was only checked for "not empty" |
| a held menu button fires every frame | nothing tested that an edge is an edge |

The first three are the same shape as chapter 135's *count a colour two
subjects share*: **an assertion can be true for a reason that has nothing to do
with the thing under test**, and the only way to find out is to break the thing
and watch.

38/38 after the fixes.

---

## Not verified

- **One route.** SPEC's MVP asks for three towns, two routes and a gym; this is
  one map with two grass bands and a cabin. There are no trainers, no gym, no
  healing centre building, no PC box, no items and no shop. `trainers.def` and
  `gym.def` do not exist.
- **No dialogue and no NPCs.** The farm has both; this game has a cabin that
  rests your party when you stand on it and press Z.
- **The battle screen does not animate.** No HP bar tween, no hit flash, no
  typewriter log — one message at a time, replaced.
- **A turn is one tap and one message.** A turn can produce a dozen events and
  the log keeps the last interesting one; nothing scrolls and nothing waits.
- **Switching is untested against a real bench.** The Party menu works and the
  test drives it, but the scene never has more than two creatures in play.
- **Determinism is still proved on one machine.** A thousand battles replay
  under this compiler; nothing has replayed a battle in the browser, which is
  the platform the no-floating-point rule exists for. The web run drives a
  battle now, but it does not compare a hash.
- **No cloud save, no BaaS.** The farm signs in, pulls remote config and
  reconciles a cloud save; this game writes one local file. That is S28's job,
  and it is the slice the replay format was designed for.
- **The camera has no transition into a battle** — the screen simply becomes a
  battle.
- **`--bench-ui` still only measures the Studio.** Neither game's frame cost has
  been measured.

# 136 — A battle that replays

There are two games in this repo and one of them is a farm. The plan has said
"Creatures" since the beginning, and the reason it is worth building is not that
two games are better than one. It is that a creature battle is the first thing
here that has to produce **the same result on two different computers**.

A farm does not need that. Its save is a snapshot, its day roll is local, and if
two players' parsnips grew differently nobody would ever find out. A battle is
different the moment it is watched, stored or shared:

- a **replay** is only a fact if replaying it reproduces it; otherwise it is a
  video, and a video of a battle is not evidence of anything;
- a **PvP turn** over a network is four bytes — *which move, which slot* — and
  that only works if both machines can derive the rest;
- a **desync** has to be *detected*, or the two players quietly finish two
  different battles and one of them is told they lost.

So this chapter's subject is not "a battle system". It is determinism, and what
it costs.

---

## The three rules, each one a refusal

`creature_core` is a pure library with no renderer, no clock and no I/O, like
`farm_core` before it. What is new is that three specific things are forbidden
inside it, and each is something the code will not do rather than something it
does.

**1. No floating point in the resolution path.** Effectiveness is a *percent*
(50, 100, 200), STAB is `*150/100`, the damage roll is `*85..100/100`. A `float`
multiplier would be right on both machines almost always, and "almost" is the
entire problem. This is why `TypeChart::eff` is `std::vector<int>` and why the
damage function takes `int type_mult` rather than a scale factor.

**2. The RNG is state, not a service.** `Battle::rng` is a field on the struct,
it is fed into the hash, and only `step` advances it. A battle that reached for a
global generator would replay correctly right up until something *else* drew a
number — a particle system, a UI shimmer, an unrelated test in the same process.

**3. Resolution order is derived from state only.** Priority, then speed, then
one coin flip out of the battle's own stream. Never "side 0 first". That cheap
version works perfectly until side 0 and side 1 are two different computers, at
which point both of them are side 0.

`step` returns **events with no strings in them** — `{kind, side, a, b}`. Not
because sentences are hard, but because a pure core must stay compilable into a
headless test, and because the same battle has to be narrated on a screen in one
language and in a log line somewhere else. Sentences are the caller's job.

---

## One RNG, finally

`engine/rand.hpp` is new, and it is a deletion as much as an addition. The farm
had a `Rng` with this comment on it:

> xorshift64\*. Small, deterministic, and identical on every platform —
> `std::mt19937` is portable but `std::uniform_int_distribution` is **NOT**.

That was the second place the sentence needed to be true. This is the third, so
it stops being a paragraph copied into a game and becomes one header. `farm::Rng`
is now an alias and the duplicate implementation is gone.

The particle system keeps its own xorshift32 **on purpose**. It seeds per emitter
and never has to agree with anything; merging it would make a visual detail share
a contract with a save file.

---

## The table is exceptions only

Six types means a 6×6 effectiveness table: thirty-six numbers. Written out as a
grid, that is thirty-six chances to put a `200` where a `50` belongs — and worse,
a reader cannot tell a deliberate `100` from a forgotten one.

```
eff fire  grass 200
eff water rock  200
eff rock  electric 200
```

Everything unstated is neutral, and saying nothing is unambiguous. The file reads
as the *design* — fire beats grass, water beats rock, rock grounds electricity —
rather than as a matrix. It is the same instinct as `map2` writing itself at the
lowest version that can express it (chapter 134): the file should record decisions
that were made, not decisions that were declined.

Eighteen species are **six evolution lines of three**, one per type. That shape is
what makes eighteen memorable instead of merely eighteen, and it makes `evolve=`
load-bearing: every species but six of them is somebody's future.

---

## Parsing and validating are different jobs

`parse_into` refuses what is *malformed*. `validate` refuses what is *unplayable*,
and they are separate functions because a file can pass the first and fail the
second:

> `species 3 pup type=fire moves=4:ember`

Well-formed. Also a creature that, made at level 3 by a route table, has **no
legal action** — and a battle would sit there rather than crash. `validate`
catches it, along with an `evolve=` pointing at a species nobody declared: a dead
end that would otherwise show up at level 16, in someone else's playthrough.

The split between key and value is the same one used everywhere in this project:
an unknown **key** is ignored so a later field is additive; an unknown **value**
is an error. `type=ice` in a move must not become a silently neutral matchup —
that is the exact bug that makes a balance pass chase a number nobody was reading.

---

## Eighteen species, and nobody drew one of them

The other half of the chapter is the art, and it is the case chapter 135 opened
the fourth `.hrt` door for, at the scale it was opened for.

`textures/parts_creature.pix` is **twelve tiles, and not one of them is a
creature**: three bodies, three faces, three crests, three tails. Every species is
two to four of those stacked plus two colour swaps.

The trade is the one the Mixer chapter argued for, now taken at a scale where it
is not a demonstration. Eighteen drawings kept consistent by hand is a job nobody
finishes — consistency across sixty sprites is something you *maintain*, and
nobody does. Eighteen recipes over one sheet is consistent by construction, and
the nineteenth costs four lines. What it costs in return is freedom: these
creatures will always be built out of the same twelve shapes, and that shows.

**Evolution is the same recipe plus a part.** Stage one is a body and a face,
stage two adds the line's crest, stage three adds its tail. Nothing is redrawn and
the colour never moves, so a creature growing up reads as the same animal — which
is the one thing a mixer gives you that a brush charges for.

That sentence is *checked*, not claimed. For each of the six lines the test parses
the three committed `.mix` files and asserts each stage is the previous one plus
exactly one part, with every earlier part surviving and the swap pairs identical.

---

## The first sheet was invisible

The first version of `parts_creature.pix` put the bodies at row 3 and the crests
at rows 1–4. Every file parsed. Every mix composed. Every test passed. And the
water line's stage two and stage three came out **pixel-identical to stage one**,
because the body is composited *last* and buried the crest.

The single claim the sheet exists to make was invisible in the render, and nothing
but looking at the render could have said so. This is the fourth chapter running
where the finding came from a picture rather than an assertion.

The fix is a layout contract, now written into the file itself as its actual
grammar:

```
crests  rows 0..4          bodies  rows 5..15, cols 1..12
tails   cols 11..15        faces   eyes rows 7-8, mouth row 10
```

A body covers where a crest and a tail **join** it, and nothing more. Put a part
outside its band and the composite is still valid — it is just a picture where
something is missing, which no parser can tell you about.

---

## A rule that was a sweep for one door and three names for the others

`CLAUDE.md` says every `.hrt` source is re-baked by a test and compared byte for
byte. That was true of `.pack` imports, which have swept the whole tree since
chapter 131. It was true of the other three doors only in the sense that **one
`.recipe`, one `.pix` and one `.mix` were named by hand**.

Honest when the repo had three sources. Dishonest the moment a single commit adds
nineteen — and not one of the eighteen species would have been noticed going
stale.

`test_commands` now sweeps every `.recipe`, `.pix` and `.mix` in the tree
(twenty-four today), re-bakes each and compares against its committed `.hrt`, and
**refuses to pass on an empty list** — the failure mode a sweep has and a named
file does not. Verified in both directions: one flipped byte in
`creature_05.hrt` turns it red and names the file.

This is the third time this project has found the same shape (chapter 128's
preload denylist, chapter 131's attribution rule): *the rule was right, the check
covered one instance of it, and the gap was invisible until the count grew.*

---

## Thirty-two mutations, and what eleven of them proved

The centre of `test_creature` is a loop that plays **a thousand random battles**
to the end, replays each, and compares the hash **after every turn** — not just
the final one, which would report the divergence at the point where every field
already differs. It also asserts the *shape* of that sample (1000 decided, 11278
turns, longest 28), because a thousand battles that all ended on turn one would
replay perfectly and prove nothing.

That test is strong, and it still let eleven of thirty-two mutations through on
the first run. Every one of them was a real gap:

| Survived | What it means |
|---|---|
| STAB is worth nothing | the effectiveness test used one attacker, so STAB cancelled out |
| burn does not weaken the attacker | only "HP went down" was checked, never "damage went down" |
| a connecting hit can do zero | the minimum-damage clamp had no case that reached it |
| **a speed tie always favours side 0** | rule 3 above, and nothing tested it |
| nothing ever misses | no accuracy assertion at all |
| paralysis never holds a creature | ditto |
| a small creature's burn does nothing | `max_hp/16` is 0 below level ~5 |
| **the side that died wins** | `winner` is an index; every "did it end" check still passed |
| the type chart forgets rows when it grows | `types.def` declares all six types *before* the first `eff` line, so that code never has work to do |
| **a different xorshift stream** | nothing pinned the sequence |
| **an empty range still moves the stream** | a sentence in a header, checked by nothing |

The last two are the ones worth stopping on. `engine/rand.hpp` claims to be
identical on every platform — and nothing would have noticed the stream changing,
which means nothing would have noticed **every stored replay becoming void**.
There is a golden sequence now. And "an empty range consumes nothing" is not
pedantry: without it, a battle where one side happens to have a single legal
choice advances the stream differently from the same battle where it has two, and
the desync would look like a physics bug.

**One survivor needed the test fixed rather than written.** "A small creature's
burn does nothing" survived my first patch because the opponent attacked in the
same turn, so the HP fell whether the burn ticked or not — the assertion was
satisfied by **the wrong cause**. Both sides now take a no-op action and the check
is exact: `hp == before - 1`. That is the same trap as chapter 135's
count-a-colour-two-subjects-share: an assertion can be true for a reason that has
nothing to do with the thing under test.

Final: **32/32**, baseline green after restore, sources restored clean.

---

## Not verified

- **There is no game yet.** This chapter is a library, eighteen pictures and a
  thousand replayed battles. There is no overworld, no encounter, no party
  screen, no manifest and no `--project`. Chapter 137 is the consumer, and this
  is only acceptable *because* it is the very next slice — the same bargain
  chapter 111 made and named.
- **Determinism is proved on one machine.** A thousand battles replay identically
  under this compiler on this laptop, and the RNG has a golden sequence, but
  nothing has yet replayed a battle on the **web build** — which is the platform
  the whole no-floating-point rule exists for. That check belongs with the first
  build that can record one.
- **All moves are physical.** There is no special/physical split, so burn halves
  every attacker equally and a "special" type has no meaning yet.
- **No stat stages, no items, no weather, no held items, no multi-hit, no
  recoil.** Four status effects exist and one of them (sleep) is applied by
  exactly one move.
- **Sleep costs the turn it wakes up on.** That is a deliberate simplification
  and it is Gen-1 behaviour, not an accident — but it is a balance decision
  nobody has played against.
- **The AI is one rule and a tiebreak**: highest expected damage, switch out
  below 20% HP when the bench improves the matchup. It cannot bait, cannot
  predict, and does not know what PP is for.
- **Catching ends the battle and nothing else.** `try_catch` sets `over` and
  `winner`; there is no box, no party insertion, no "it broke free" turn cost
  beyond the RNG draw.
- **The eighteen have no animation and no back sprite.** One 16×16 frame each,
  facing the viewer.
- **`.mix` and `.hrt` can still drift** in the sense chapter 135 recorded — the
  sweep catches it, it does not prevent it.
- **Nothing consumes `encounters.def`, trainers or gyms**, because those files do
  not exist yet. The species table names sprites; nothing draws them.

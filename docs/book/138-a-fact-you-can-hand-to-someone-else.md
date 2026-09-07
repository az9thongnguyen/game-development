# 138 — A fact you can hand to someone else

`battle.hpp` opens with a sentence it spends its whole header defending:

> the same starting state and the same list of actions produce the same battle,
> on every machine, forever.

Chapter 136 built the thing that sentence is about, and checked it a thousand
times. Every one of those checks ran the simulation twice **inside one
process**. That proves something real — that `step` is a pure function of its
arguments, that nothing reaches for a clock or a global generator — and it
proves nothing whatsoever about *on every machine*. A pure function still has
to be computed, and two compilers targeting two instruction sets are two
different computations of it.

This chapter is where the claim leaves the room it was made in. It leaves as a
file.

---

## The verb that could not be recorded

Before any of that, a bug that had been sitting in plain sight since 136.

A replay is a start state plus a list of actions. The actions are
`Move`, `Switch` and `Run`. And a creature game's most characteristic verb —
throwing a ball — was none of them. `throw_ball` lived in `world.cpp` and did
this:

```cpp
const bool caught = try_catch(d, w.battle, 1);
if (caught) { ...take it...; return true; }

// It broke free, and the turn is spent. The wild side acts; the player does not.
step(d, w.battle, Action{Action::Kind::Switch, w.battle.side[0].active},
     choose(d, w.battle, 1), &w.log);
```

Read the second half again. To give the wild creature its move, the game told
the resolver that the player had **switched to the slot that was already
active** — a legal action that happens to do nothing, chosen because its side
effect was the one wanted. It works. It is also a lie told to `step` to buy a
behaviour, and lies told to a resolver are exactly the thing a deterministic
core is supposed to make impossible.

The consequence was quiet and total: **no recording of this game could contain
a ball.** Not "balls replay incorrectly" — the action simply had no
representation, so a tape of a fight where you threw one would replay a fight
where you did not.

So the ball became an action. `Action::Kind::Ball` carries the bonus percent in
`index`, `Battle::caught` says how the fight ended, priority 6 puts it before
any move, and `act` calls the same catch arithmetic from the live turn stream:

```cpp
if (a.kind == Action::Kind::Ball) {
    const bool stuck = catch_roll(d, b, other, a.index, rng);
    if (stuck) { b.caught = true; b.over = true; b.winner = side; }
    emit(out, Event::Kind::Ball, side, stuck ? 1 : 0);
    return;
}
```

`throw_ball` is now one line and a decrement.

Two things fell out of that which are worth naming.

**`settle` had to check `caught` before `winner`.** A ball that sticks sets
`winner = 0`, and the branch below reads `winner == 0` as a knockout: it would
have awarded experience for a creature that never fainted and called the fight
Won. The old code never hit that because the catch never went through `step` at
all.

**The RNG stream did not move.** The old path drew one number for the catch and
then let `step` draw for the wild side; the new path draws the catch inside
`step` and then the same wild draws. Same numbers, same order. The evidence is
that `test_creature_world`'s balance sample — 237, 261 and 224 wins out of 300
for the three starters — came out identical to the digit. That was not luck; it
was the design constraint, and it is why this refactor could ship in the same
commit as a format that depends on the stream.

`try_catch` stayed, as a second door onto the same arithmetic — it measures the
catch odds without a battle happening around it, which is what makes the
catch-band test possible. Two doors into one calculation is a promise, and this
project has learned what an unchecked promise is worth, so a test now pins the
two against each other over four hundred seeds and asserts both that they agree
and that they *disagree with themselves* often enough for the agreement to mean
something (`through_call > 20 && < 380` — two functions that always answer "no"
also agree perfectly).

---

## What is in the file

`crep1`. Three fields carry the weight, and each one is there because leaving it
out makes a different lie possible.

**The start state, not a seed.** A seed reproduces a battle only if you already
know the parties — and in this game the parties arrive damaged, mid-level, with
PP already spent. Stats are still *derived*: a `c` line stores species, level,
exp, hp, status, sleep and the four move slots, and `make(d, species, level)`
computes the rest. That is the same rule the save follows, for the same reason:
a file that stored `atk` could disagree with the table it was balanced against,
and then a re-balance would silently not reach old files.

**A hash after every turn.** An end hash says *you disagree*. Turn hashes say
*when* — and a divergence caught at turn 4, where the two states differ in one
place, is a bug you can read. The same divergence caught at turn 15 is two
completely different battles and a shrug.

**A fingerprint of the rules.** This is the field that was not obvious. A replay
is a fact *relative to the tables it was played under*. Re-tune a move's power
and every stored replay diverges — correctly, honestly, and in a way that is
indistinguishable from a machine getting the arithmetic wrong. Without
`rules_hash` the verifier would shout DESYNC at every balance commit, and a
verifier that cries wolf is a verifier people turn off.

So there are two faults, and `verify` reports them apart:

```
creatures/reference.crep: OK — replayed 15 turns, winner 1
creatures/ci_tampered.crep: DESYNC — turn 1: 7461e907fabe46d0 != 7461e907fabe46da
```

The fingerprint covers what `step` reads — the type chart, every move's
numbers, every species' stats and learnset — and **deliberately not** the
encounter tables or the sprite paths. Moving a creature to a different patch of
grass, or an artist redrawing it, must not invalidate a recording of a fight
against one. That is a judgement call with a test attached in both directions:
adding an encounter entry leaves the hash alone, bumping a move's power does
not.

---

## The reference battle, as committed bytes

`assets/creatures/reference.crep` is one battle: fixed species, fixed levels,
fixed seed, both sides played by the AI except at turns 2 and 5, where the
script throws a ball and switches. Twenty-six lines, in the repository.

`test_creature` rebuilds it from the tables and compares **bytes** — the exact
standard a `.recipe`, a `.pix` and a `.mix` are held to since chapter 135. CI
runs that test on Ubuntu/x86-64/gcc and on macOS/arm64/clang, and the file in
the repository was written by the second one.

That is the whole chapter in one sentence: **the bytes cross an instruction
set.** A thousand battles inside one process cannot make that claim. Twenty-six
lines checked by two toolchains can.

The script visits three of the four action kinds on purpose. A reference made
only of moves would prove that four bytes a turn reproduce and say nothing
about the kind that carries a *number* — a ball's bonus percent — which is
precisely the field a format is most likely to lose.

CI also points the verifier at a file that must **fail**: one hex digit flipped
in one turn hash, same rules, different arithmetic. A verifier that answers OK
to everything passes the happy line just as well as a correct one does.

```yaml
if ./build/demo --cmd creature.verify creatures/ci_tampered.crep; then
  echo "the verifier accepted a tampered replay"; exit 1
fi
```

---

## The third toolchain

The two machines above are the two CI happens to own. Neither of them is the
one this project ships on.

The web build compiles every line of `creature_core` through a completely
different compiler to a completely different instruction set — and until this
chapter nothing had ever asked it to compute something a native build had
already computed. `?cmd=<id>&args=<...>` runs a headless command in the
WebAssembly build, no canvas and no scene, and `web_touch_check.mjs --cmd`
reads back what it printed:

```
ok    the WASM build started
      creatures/reference.crep: OK — replayed 15 turns, winner 1
PASS  a third toolchain agrees: creature.verify creatures/reference.crep
```

Three lines of `shell.html` and about twenty of the check script. It is the
cheapest strong claim in the chapter, and it was on the "not taken" list until
it turned out to be three lines.

---

## The game writes one without being asked

A recording a player has to remember to make is a recording nobody has when it
matters. So `World` carries a tape — reset by `begin_battle`, appended to by
every resolved turn — and the scene writes it to
`saves/creatures/last_battle.crep` the moment a fight ends.

The interesting part is *where* that write lives. A battle can end four ways: a
knockout, a catch, a run, a blackout — in four different branches of
`update`. A write placed in three of them is a recording that is missing
exactly one outcome, and the missing one is whichever branch the person writing
the code was not thinking about. So `update` became a two-line wrapper around
the old body:

```cpp
const bool was_fighting = world_.phase == Phase::Battle;
update_world(dt, input);
if (was_fighting && world_.phase != Phase::Battle) write_tape();
```

One place asks the question. `test_creatures_scene` then does the thing this
whole chapter is for, with a pointer: it walks into the grass, fights until the
fight is over, reads the file **the game wrote**, re-plays it, and checks the
state it arrives at is the state the scene is holding.

---

## One list, four readers

Small, and the third time this project has found the shape.

The four `.def` files a `Dex` is made of were written out in `creatures_scene.cpp`,
in `test_creature.cpp` and in `test_creature_world.cpp` — and the headless
verifier would have been a fourth. Three copies of "which files are the rules",
one of which somebody eventually forgets. (`test_creature`'s copy had already
drifted: it loaded three of the four.)

`kDexFiles` and `load_dex` now live in `defs.hpp`. `load_dex` takes a *reader*
rather than opening anything, so the core stays pure and the game, two tests and
a command cannot be reading different files from each other.

---

## What the mutations found

Thirty-two single-token mutations on the new code. **Twenty-one killed, eleven
survived** — and the eleven are the chapter's real content, because a mutation
that survives is a sentence a test cannot say.

**One was redundant code, not a missing test.** Deleting the reader's
`k0 > 3 || k1 > 3` check changed nothing, because `legal_action` below it
answers false for every value the cast can produce. It read like belt and
braces and was only a belt. It is gone rather than tested — a guard whose
removal is invisible is a guard the next person will trust.

**Three of the ten real gaps were assertions true for the wrong reason**, which
is by now this project's most reliable finding:

- *"a creature read from a file is at full HP"* survived because **every**
  replay in the suite started with two freshly made parties, and a fresh party
  is at full HP. That is exactly the case the real game is never in: a party
  walks into the grass carrying whatever the last fight left it. The stored
  `hp`, `pp` and `status` had never been read back by anything.
- *"a ball has no priority"* survived because every ball test paired a fast
  thrower with a slow target, where priority 0 and priority 6 produce the same
  battle. Only a fight you are **losing** can tell them apart: a level-3 starter
  against a level-40, where the wild side would otherwise move first, find the
  thrower fainted, and the throw would never happen at all.
- *"a side may be declared twice"* survived because the test case appended a
  bare `side 0 3 0` and got refused for being **three creatures short**. It
  tested a different guard and read exactly like a case that tested the right
  one.

**And one survivor is not a missing test at all.** `p.count >= want[side]` in
the reader stops a seventh creature being written into a party of six. Delete
it and the file is *still refused* — the count check at the end catches it —
but only after `member[6]` has been written over `count` and `active` and into
the other side. The behaviour is identical; the corruption is intra-object, so
there are no redzones and **ASan does not see it either** (checked: the mutant
runs clean under ASan+UBSan). No assertion in this suite can distinguish the
two versions. It is left as it is, with a fixture that documents the intent and
a note that says why the fixture cannot fail. That is the first mutation in this
project that is a real bug no test could ever have caught, and pretending
otherwise by writing an assertion that passes for a third reason would be
worse than saying so.

Final: **42 of 43** across both rounds, with the one survivor named above.
Baseline green after restore, both rounds.

---

## What is still not true

The honest ledger, because the claim in this chapter is a strong one and it is
easy to hear a stronger one.

- **Three toolchains is not every machine.** x86-64/gcc/glibc, arm64/clang/libc++
  and wasm32/LLVM is a genuinely useful spread — and the third one is the target
  this project actually ships on, which is why `?cmd=` exists at all. It is
  still not Windows/MSVC, not 32-bit native, and not big-endian.
- **One reference battle.** Fifteen turns, three of the four action kinds, one
  seed. It exercises damage, a status effect if the roll goes that way, a
  switch and a catch. It does not exercise `Run`, sleep, a party wipe, or the
  hundredth turn of a stall.
- **Nothing sends one anywhere.** The BaaS has had a replay store since chapter
  100 and this game does not touch it. A recording is a local file; two players
  cannot yet compare theirs. That is the next slice, and it is the whole reason
  the format has per-turn hashes.
- **A tape is not a save.** `to_text` cannot store a battle at all, so a game
  reloaded mid-fight does not exist and the tape has no half-state to be in.
  That is a limitation dressed as a simplification, and it is worth saying which
  it is.

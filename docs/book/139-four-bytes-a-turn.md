# 139 — Four bytes a turn

Chapter 136 built a battle that is integer arithmetic all the way down. Chapter
138 made a recording of one into a file three toolchains agree about. Both were
answering a question nobody had asked yet, and the header said so out loud:

> …what lets two players fight over a network by exchanging four bytes a turn
> instead of a world state, and what lets a desync be DETECTED instead of
> quietly deciding the winner.

This is the chapter where somebody asks.

---

## What the two players send each other

Not a health bar. Not a damage number. Not "I hit you for 34".

Each side sends the **action it chose** — a kind and an index — and both compute
the entire turn from it. Then both send `hash(battle)` and compare. The
measured cost of a turn on the wire is **34 bytes**, and 16 of those are the
hash: verifying the turn costs four times as much as playing it, and it is
still nothing.

```
TRACE send <act 4 1 1>
TRACE send <hash 4 18ecdc71da64f376>
```

A protocol that exchanged *outcomes* would have to trust them. This one does not
have to, because it can tell when they differ.

---

## Three things a player does not get to choose

The protocol's header lists them, and each one is a decision that looked
optional and is not.

**Which side you are, and the seed.** Both arrive in the server's `matched`
event. The tempting alternative — each client contributes half a seed and they
mix — sounds fairer and is worse: whoever sends second can grind their half
until the mix suits them. Fixing that properly needs commit-reveal. The server
has no stake in the outcome, so the server picks, and the whole problem
evaporates.

The seed travels as **sixteen hex characters, not a JSON number**. A JSON number
is a double in every browser, and the low bits of a 64-bit seed are the entire
point of it.

**What your creatures are.** The wire carries `species:level`, and both peers
call `make(d, species, level)`. So a peer can lie about *which* creatures it
brings and cannot lie about *what they are* — the same rule as the save and the
replay, arrived at for a third time: stats are derived, never transmitted.

---

## Catching a desync, from both sides

The interesting test is not the thousand matches that agree. It is the forty
where one peer is deliberately playing a build where a move hits harder — a
stale client, in other words, which is the ordinary case in a shipped game.

All forty diverged. **All forty were caught by both sides, at the same turn**,
and neither declared a winner. Each side reports the other's hash as the one it
disagrees with, which is what makes the two reports about the same event rather
than two independent complaints.

Both mattering is the point. A protocol where only the victim notices is a
protocol where the other player keeps playing a game that has already ended.

And a desync reports **nothing to the ladder**. Neither side knows what happened
after the turn they stopped agreeing, so a rating moved by that match would be
a number derived from a battle nobody can reconstruct.

---

## A leaderboard that keeps your best cannot hold a rating

The BaaS has had leaderboards since slice one, and `submit` keeps the better of
the old and new value. That is exactly right for a high score and exactly wrong
for a rating: **an Elo goes down**. A board that silently refused to lower it
would turn a ladder into a record of everybody's best day.

One column — `mode`, `'best'` (the default, so every shipped board keeps
working) or `'last'` — and migration 9.

---

## …and a ladder where the client picks the number is not a ladder

`POST /scores` takes a value the client chose. This project has an anti-spoof
rule it keeps everywhere — *the score belongs to the JWT's user, never a body
field* — and that rule means nothing on a ladder if the VALUE is still a body
field. "I am rated 3000" needs no forged identity.

So the ladder has its own verb. `POST /v1/leaderboards/{key}/match` takes the
**outcome** — an opponent, a result, a match id — and the server does the
arithmetic: reads both ratings, applies Elo, writes both.

Which means the server needs the same Elo the client uses to predict. `engine/elo.hpp`
is header-only, `constexpr`, and pure, and `baas/CMakeLists.txt` now puts
`src/` on its include path. That is a **deliberate exception** to "the backend
links no engine code", and the narrowest one available: nothing is linked, one
header is shared, and the reason is precisely the reason the table exists at
all — a rating computed twice from two copies of a curve is the bug the integer
table was written to prevent.

Both players report, because both played. That is not a retry; it is the normal
case, and applying it twice would move every rating twice. The match id makes it
idempotent — and the idempotency store moved to `baas/common/idempotency` to do
it, under a note that had been sitting in `inv_service.cc` since chapter 102:

> when a SECOND endpoint needs idempotency, graduate these to
> baas/common/idempotency.

The second endpoint arrived. That is three promises this project has now paid
back on the terms it wrote down (chapter 137 paid two).

The second reporter gets `delta: 0`, deliberately. The stored delta belongs to
whoever got there first, and handing it over told the loser of the first real
match that they had gained sixteen points. **A number that is right for someone
else is worse than no number.**

---

## …nor one where it picks the opponent

Which leaves the same hole one level up. A client that cannot forge a rating can
still report a match against the top player — forty times.

Only the server knows who it paired, so the hub remembers: the last 4096
pairings, `"project:room" → (a, b)`, evicted oldest-first, and gone on restart.
`was_matched` is one line in the controller and a `403` if it is false.

The cost is honest and worth writing down: `/match` now works only for a game
that uses the hub's own matchmaking, and only until the server restarts. Both
of those are already true of the hub, which is where the knowledge lives.

---

## The bug two processes found and one process could not

Everything above was green. `test_netbattle` played a thousand matches;
`test_creature_pvp_live` played one over a real socket against a real Drogon
server and checked the ladder afterwards. Then two `--pvp` processes were
pointed at a running backend, and **the match ran for five hundred turns and was
still going**.

The trace said everything:

```
TRACE recv <party 1 1:20 5:18 9:22>
TRACE send <party 1 1:20 5:18 9:22>
...
TRACE recv <act 505 1 0>
```

Both parties are the same, because the headless client had one fixed team. Every
test in the repository had built its parties from randomly drawn species, so
**no test had ever put two identical teams in a battle**. Five hundred turns in,
every creature on both sides was out of PP; nothing could take damage; and
`choose`'s last resort was:

```cpp
const int bench = me.first_alive(me.active);
if (bench >= 0) return Action{Action::Kind::Switch, bench};
```

*Switch to the first living creature* — whatever its PP. So both sides rotated
their benches at each other, forever, each one making the only move it had.

Neither half of the fix is the interesting part; the pair is. **The AI stopped
switching to a creature that also cannot act**, and — separately — **a battle
that reaches 200 turns is a draw**. The second is not a safety valve bolted on
the outside: it is a rule of the game, in `battle.hpp`, so every consumer gets
it at once — the wild game, a stored replay, and a rated match between
strangers. Two hundred is far beyond any real fight (the longest of a thousand
random battles was 28), so no committed recording moved, and `test_creature`
re-bakes `reference.crep` byte-for-byte to prove it.

With the AI fixed, a mirror match now *decides* in 28 turns rather than merely
stopping at the cap — which is why the two guards are tested apart. The mirror
test asserts twelve mirror matches all decide and none reaches the cap; a
separate no-PP stalemate asserts the cap fires, at turn 200, as a draw, with
everybody still alive.

This is the third time in three chapters that a proof did not transfer, and the
third time that finding out was worth more than the proof. Chapter 137: the
farm's browser check did not survive contact with a second game. Chapter 138:
determinism inside one process said nothing about two toolchains. Here: one
process playing both sides never gave them the same team.

---

## What the mutations found

Thirty-two single-token mutations across the Elo table, the protocol, the
battle's new stalemate rule, the ladder service, the hub and the controller.
**Twenty-nine killed.** All three survivors were worth the round.

**One of them proved a comment wrong.** `elo_update` rounds half away from zero,
and the comment above it said that was what kept a match zero-sum. Flipping it
to plain integer division survived every test in `test_elo` — because C++
truncation is *symmetric*: the winner's numerator and the loser's are exact
negations either way, so the ladder stays zero-sum with or without the rounding.
What the rounding actually buys is **magnitude**: truncating rounds a gain down
and a loss up, so a close result is worth a 15 where the arithmetic says 16, on
about half of all games. The comment now says that, and a test pins the one
rating pair where the two rules disagree.

That is a shape worth naming on its own: not a missing test and not a wrong
line of code, but a **correct line defended by a false argument** — which
survives review exactly as well as a true one, and would have been deleted the
first time someone "simplified" it.

**One was a guard whose other half was never exercised.** The AI's new bench
scan skips a creature that cannot act; dropping the `alive()` check survived,
because in the no-PP stalemate every creature was alive and none had PP. A
fainted creature *keeps its PP*, so it would have been chosen — and `act` would
then refuse the switch and spend the turn going nowhere. The case needed was a
party where the only creature with PP is the one that is down.

**One was a capability nothing used.** `begin()` resets the object, and every
test built a fresh `NetBattle` — while any client that plays a second match
reuses one. The test now plays two matches on the same pair of objects, with
different sides, a different seed and different teams, and checks the second
tape is the second match rather than the first one with more on the end.

Final: **35 of 35** across both rounds, baseline green after restore, both
times.

---

## What is still not true

- **There is no PvP in the game.** `--pvp` is a headless client that plays with
  `choose`; the creature game's battle screen has no lobby, no "find a match"
  button and no way to be in one. The protocol takes an action from a caller
  and does not care where it came from, which is what makes that a UI slice
  rather than a redesign — but it is not done, and the number of verbs a player
  can reach by thumb is unchanged.
- **No referee.** Two peers agree or they do not. The server assigns sides,
  seeds and pairings, and rates only matches it made — but it does not replay
  the battle, so two clients modified the same way still agree with each other.
  The stored tape is what makes that checkable *afterwards*, by something that
  did not play it, and nothing yet checks it automatically.
- **No reconnection, no timeout.** A peer that stops sending leaves the other
  waiting until its own loop runs out. There is no surrender, no clock, and no
  way to resume a match whose socket dropped.
- **The hub is still single-node and in memory** — rooms, the queue and now the
  match ledger. A second backend process would pair nobody with anybody and
  refuse every report.
- **K is fixed at 32 for everybody**, there are no rating floors, no placement
  matches and no decay. `apply_match` already computes the two sides
  independently rather than negating one delta, so a per-player K is a change to
  one function; it is not a change that has been made.

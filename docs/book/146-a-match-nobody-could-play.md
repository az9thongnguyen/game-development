# 146 — A match nobody could play

Chapter 139 built rated PvP: matchmaking, a battle exchanged over a socket, a replay
stored, an Elo ladder moved. Chapter 138 built the replay format. Both were verified
against a real server, and both work.

Nobody could play one.

`--pvp` is a headless worker. Its client picks its actions with `choose`, which is the
AI. `test_creature_pvp_live` runs two of them against each other. There is no path from
the game's screen to any of it — the whole feature exists for a terminal and for a test.
That is the strongest form of the bug this project keeps finding: not a control drawn
where it cannot be pressed, but a **feature with no control at all**.

---

## The comment named the slice again

`pvp.hpp` has said this since chapter 139:

```cpp
//  The ACTION is chosen by `choose` — this is a headless client, and there is
//  nobody to ask. A player-driven one would take the action from a screen instead;
//  everything else here is unchanged, which is the point of the protocol being pure.
```

Chapter 144 opened on chapter 112's comment naming its own slice. This is the second in
a row, and this one turned out to be *exactly* right:

```cpp
void set_auto_play(bool on);                     // off: the screen decides
[[nodiscard]] bool waiting_for_action() const;   // ...and this is when it is asked
bool act(Action a);
```

Three declarations and one `&& auto_play_`. Everything below them is the same code, which
is why `--pvp` still plays the same match — and why the *live* test could keep the
opponent as the headless client. What is under test in the new one is the **screen**, not
a second copy of the protocol.

`cancel()` came with them, and it is not cosmetic: a player who taps Cancel has to leave
the **server's** queue. A client that merely stops updating stays queued and gets matched
with somebody who then waits for a peer that is not coming.

## The screen

`Mode::Online` is a status line and one control, which is Cancel. It is a *battle* mode
rather than an overworld one for two reasons: the panel is already there and the sprites
are about to be, and the d-pad has to be **gone**. Walking off down the route while a
server holds you in a queue is the same bug `cancel()` prevents, arriving through the
other door.

The battle itself is the battle screen. One function decides which battle is on it:

```cpp
const Battle& CreaturesScene::shown_battle() const {
    return net_battle_live() ? online_->net().battle() : world_.battle;
}
int CreaturesScene::my_side() const {
    return net_battle_live() ? online_->net().side() : 0;
}
```

That is `layout()`'s rule one level up: the renderer, the menu and the hit test must not
each decide which battle they mean. `my_side()` matters more than it looks — **the server
hands out sides**, and a screen hard-coded to 0 shows you your opponent's party while you
pick moves for it.

Ball and Run are drawn **dim** in a rated match rather than hidden, and tapping one says
why (a wild creature can be caught and a trainer's cannot; "Run" against a person is a
forfeit the protocol has no frame for). A menu that changes shape between a wild fight and
a rated one teaches two layouts.

---

## Three bugs, three different instruments

### The wire carried an empty party

The first live run refused the match: `peer sent an illegal party`.

`start_online` built the outgoing `Party` in place — filled `member[]`, set `active`, and
never set **`count`**. `write_party` iterates `count`. So the frame said nothing, and
`read_party` refused it on the other side, correctly and specifically.

The fix is the lesson: `make_party()` exists, and a struct with a constructor function has
one because that function is the only place that knows all of its fields. Hand-filling it
is how you find out which field you did not know about.

Worth saying plainly: **the protocol's own validation caught this in one debug print.**
Chapter 139 wrote `read_party` to stop a peer claiming a level-9000 creature. It also
stops a peer claiming nothing, which is what a bug in your own client looks like from the
other side.

### The result screen was about the wrong battle

`shown_battle()` first keyed on `online_->state() == Playing`. The result screen is by
definition reached **after** the match ends, when the state has moved to `Reporting` and
then `Done` — so it drew `world_.battle`: the last wild fight, or nothing.

A "what is on screen" question answered by a state that has already moved on is answered
correctly for every frame except the one that matters. It keys on the protocol having
begun now — `net_battle_live()` — which stays true through the result and until the
session is dropped.

### The button was hittable and invisible

And then the picture.

```cpp
case Mode::Ack:
    if (online_) {
        …choose the text…
        break;        // ← out of the switch, past the draw, past the button, past return
    }
```

The rated result screen drew **nothing**: no text, no Continue button, two blank
placeholder squares over two empty health bars. And every test passed — including the new
ones — because they tap the rect the **layout** reports, and the layout was right.

This project's standing lesson is *drawn but dead*: a control that looks right and is
wired to nothing. This is its inverse. The control was wired perfectly and was not drawn.
A test that reaches controls by name (chapter 144's rule, and the right rule) cannot see
it, because the name resolves to a rectangle whether or not anything was painted there.

The check that catches it is chapter 143's, applied to a control instead of a state:
**count the colour, in the frame the renderer actually produced.**

```cpp
render(idle);
… count th::accent inside l.ack …   CHECK(accent > 0);
… count ink inside l.log …          CHECK(ink > 0);
```

Four chapters running where a bug was visible only in a picture.

---

## The test that could not have been written before

`test_creature_pvp_live` proves the protocol; both of its players are an AI, and an AI
never presses a button. `test_creatures_online_live` replaces one of them with the actual
game screen: a framebuffer, a pointer, and taps at the rectangles the renderer reported.
It never calls `PvpClient::act` — it taps **Fight**, then a **move**, and the action
reaches the wire because the screen is wired to it.

```
  the player took 2 turns with 4 taps
  rating 1184 (-16, applied)
```

`taps == turns * 2` is asserted, because it is the whole claim: every turn cost exactly
one Fight and one move, chosen by a pointer. And the opponent's team was rebalanced after
the first run finished in **one exchange** — levels 19–21 against a level-5 starter passed
every assertion about the protocol and proved nothing about a player taking turns.

---

## Twenty-five mutations, and fourteen survivors

The first run killed 11 of 25. Fourteen survivors is a lot, and every one of them said
something:

**Six were plain gaps** a pure test closes in a line — `Box::overlaps` on an empty box,
the button on a screen too short for it, the search screen claiming two creature rects it
has nothing to draw in, starting a rated match from inside a wild one, a failed session
drawing two blank creatures, and a result line an ink count could not tell apart from a
leftover toast.

**Two were assertions that had nothing to say.** `my_side()` hard-coded to 0 survived
because the scene queued **first** and got side 0 — the check was true either way. The
opponent queues first now, the player is side 1, and the assertion means something. This
is the memory that keeps coming back: *a test can pass without touching the code.*

**Three were a flag only one caller ever set.** `waiting_for_action`'s auto-play half was
never exercised on a client that was Playing and owed an action — the only state it means
anything in. It is now toggled both ways at that exact moment: same client, same phase, no
update in between, so the only thing that changed is the flag.

**One was unobservable by construction.** The between-turns refusal needs "I have acted
and my opponent has not", and with auto-play on a client acts inside the very `update()`
that builds the battle — the peer is always ahead, and that state lasts less than one tap.
The peer is now driven by hand too, which also gives `act()` a second caller that is not a
screen.

**Two were the code's fault.**

- `cancel()` used a **blacklist** of states to skip, and `Done` was not on it — so
  cancelling a finished session reset it to Idle and threw away the result still on
  screen. A whitelist of the states an operation applies to cannot forget a state; a
  blacklist can, and did.
- `cancel()` also sent the cancel frame **and** dropped the socket, and dropping the
  socket clears the server's queue by itself. Two mechanisms covering each other, which is
  chapter 145's lesson exactly — no test could tell the frame from its own absence. It
  keeps the socket now (a player who cancels and looks again should not pay for a new
  connection) and the test keeps a **ghost** client alive: cancelled, still connected, and
  never matched. If the frame had not reached the server, the two real clients would be
  paired with the ghost instead of each other, which is what a player experiences as "my
  opponent never moved".

**And the last one was equivalent, twice over.** `if (oy >= kMargin)` around the button's
placement could not fail: the d-pad only appears from ~360 px of height, and at that
height the row above it is already 150 px down. Dead code — deleted, with the check moved
into the test where a future regression can be loud instead of silent. Then the *test*
that was supposed to prove it turned out to sweep heights 120–400, where the pad does not
exist below 360, so almost every pass hit `continue` and the loop asserted **nothing**. A
sweep that never reaches its body is decoration. It sweeps 300–900 now and counts how many
times it ran.

Final: **25 mutations, 24 killed, 1 equivalent (deleted rather than pinned)**, across three
rounds.

## What is verified, and what is not

Verified:

- **95/95 `ctest`, twice.** `test_creatures_online_live` is new: a real Drogon server, a
  real socket, a real `CreaturesScene`, and `--pvp`'s own client on the other side.
- A rated match **played by tapping**, with the rating moved on the ladder, both clients
  reporting exactly once, and the recording holding exactly as many turns as the player
  tapped moves for.
- The screen shows the player **their** side — asserted with the player on **side 1**,
  because on side 0 the assertion is true of a screen that never looked.
- **A rendered frame looked at** — which is what found the third bug.
- With no server anywhere: the button is clear of its neighbours at every height the pad
  exists at, the d-pad is gone while a session is up, a second session is refused, the
  world does not move, the failure reaches the screen, Cancel works by being **tapped**,
  and Continue restores the route without healing the party.
- Golden path on `projects/creatures.gameproject`, 0 `.tmp` leaks; Emscripten build.

Not verified:

- **Nobody has played a rated match in a native window, or in a browser.** Every claim
  above comes from a headless driver. The web build compiles this code, and the SDK's ws
  transport on Emscripten is a different implementation from the one the test exercises.
- **The default config is `127.0.0.1:8080` natively**, so on a desktop with no backend
  the button fails immediately and says so. That is honest but it is not a product: there
  is no server list, no address field, and nothing that tells a player where to point it.
- **A disconnect mid-match is `fail("the socket dropped mid-match")` and nothing more** —
  no reconnect, no forfeit, no ladder consequence. Two players and a flaky train tunnel
  are an untested combination.
- **The party sent is a fresh copy at the same species and levels.** A rated match
  therefore ignores held damage, which is deliberate, and also ignores everything else a
  creature might carry one day.
- `waiting_for_action()` is false between turns and the menu still *renders* live. Nothing
  is queued and nothing resolves, and the tap says "Waiting for your opponent" — but a
  player who taps three times sees the same sentence three times, not a screen that looks
  busy.
- **One guard is untested and known to be**: `start_online` refuses an empty party. The
  world's party always has the starter in it, so the branch is unreachable short of a
  corrupt save — kept as insurance against exactly that, and recorded here rather than
  pinned by a test that would have to fake the state it defends against.

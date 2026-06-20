# Chapter 14 — The Windowed Frontend (GUI)

> **Goal of this chapter.** Give chess a real face in the window: draw the board
> and pieces with our 2D software renderer, select and move with the mouse,
> highlight legal moves, show game status, and let the AI reply. Crucially, it
> drives the **same `Game` controller** as the TUI — the GUI adds only
> presentation and input, no rules.

---

## 1. A Scene over the shared core

`ChessScene` is an `engine::Scene` (Chapter 3): the engine calls its `update(dt,
input)` for logic and `render(ctx)` for drawing. It owns a `Game` and a little UI
state (which square is selected, an AI think-timer). It is the **only** chess file
that includes the engine — the rules below it know nothing about windows or SDL.

```
ChessScene  (engine::Scene)  -- update/render -->  draws with renderer2d, reads input
     | owns
     v
   Game (controller)  -->  movegen . search . rules   (pure core, also used by TUI)
```

---

## 2. Drawing the board

The framebuffer is 480×270; the board is 8×8 squares of 28px (224×224) at an
origin, leaving the right side for a status panel.

- **Squares:** a double loop fills each square light or dark. `(file + rank) % 2`
  picks the color (a1 is dark). Screen-y grows *down*, so rank 8 is drawn at the
  top: `y = origin + (7 - rank) * square`.
- **Pieces:** for v1 we draw each piece as its **letter** (P N B R Q K) via the
  bitmap font at scale 2, white pieces in near-white, black pieces in near-black —
  instantly readable, and a clean upgrade path to hand-drawn sprites later (just
  swap the `draw_text` for a `blit`).

> Drawing pieces as letters is a deliberate v1 choice: zero asset work, perfectly
> legible, and it isolates "how pieces look" to one line. Sprites are cosmetic.

---

## 3. Mouse → square, and click handling

`square_at(mouse_x, mouse_y)` inverts the layout: subtract the origin, divide by
square size, and flip the rank (`7 - fy`) because of screen-y direction. Out-of-
board clicks return `-1`. Because input is in *framebuffer* coordinates (Chapter
6), this lines up exactly with what's drawn, regardless of the window's 2× scale.

**Click as a state machine** (`on_click`):

1. Nothing selected + you click your own piece → **select** it.
2. A piece selected + you click a legal target → **move** there.
3. Click the selected square again → **deselect**.
4. Click another of your pieces → **reselect**.

To turn the legal move into a real move we scan `game_.legal_moves()` for one with
matching from/to (auto-choosing the **queen** for promotions in v1), so castling /
en passant / promotion all execute correctly via the core.

### The click latch (input edge done right)

Recall (Chapter 6) that the fixed-timestep `update` can run 0, 1, or several times
per frame, so reading a "pressed" edge there is fragile. Instead we **latch on the
button level**: act once when the button goes down, then ignore until it's
released.

```cpp
bool ldown = in.down(MouseButton::Left);
if (ldown && !click_latched_) { click_latched_ = true; on_click(square_at(...)); }
if (!ldown) click_latched_ = false;
```

One physical click = exactly one action, no matter how many update steps run. The
same pattern handles **Space** for "new game".

---

## 4. The AI's turn (without freezing the UI)

When it's the AI's move we don't search instantly — we let a small timer elapse
first so the human's move is visible and the status can say "AI thinking...":

```cpp
if (ai_turn) {
    ai_timer_ += dt;
    if (ai_timer_ > 0.35) { game_.play(search(state, depth).best); ai_timer_ = 0; }
}
```

For Easy/Medium the search is well under a frame; at Hard (depth 6) it may pause
briefly — acceptable for M1. (A fully non-blocking AI would search on another
thread, but we're single-threaded by design — a tidy future upgrade.)

---

## 5. Status, results, restart

The right panel shows the mode, whose turn it is, **CHECK!** when in check, and the
**game result** (checkmate + winner, stalemate, or 50-move draw) using
`result()`/`winner()`. **Space** starts a new game at any time. Selection is shown
with a yellow outline and legal destinations with small green dots — the
"highlight legal moves" requirement, straight from `legal_moves()`.

`main` wires it up:

```
demo --gui [hvh|hvai] [easy|medium|hard]
```

---

## 6. Run & observe

```sh
cmake --build build
./build/demo --gui hvai medium      # you are White; click a pawn, then its dot
```

Click one of your pieces — its square outlines yellow and legal targets show green
dots; click a dot to move; the AI replies after a beat. Try `--gui hvh` for two
humans, and **Space** to restart. (Head-less `HAND_ENGINE_FRAMES=120 ./build/demo
--gui` just verifies it runs without a crash, since there's no one to click.)

---

## 7. Common pitfalls

- **Reading click edges in `update`** without a latch → missed or double clicks.
  Latch on the button level.
- **Rank flip** — forgetting screen-y is inverted draws the board upside-down.
- **Mouse in window vs framebuffer coords** — use the logical coords (Chapter 6) so
  hits match the drawing at any scale.
- **Rules creeping into the Scene** — only call the controller; promotion choice is
  the lone UI policy here (auto-queen), and it just *selects among* legal moves.
- **Blocking on a deep search** — fine at low depth; thread it if you go deeper.

---

## 8. Glossary

- **Scene** — an engine unit with `update` + `render`.
- **Latch** — act once per press; reset on release.
- **Pick/place** — the two-click selection→move interaction.
- **Auto-queen** — defaulting promotions to a queen in the UI.

---

## 9. Exercises

1. **Promotion picker.** When a pawn reaches the last rank, pop up a tiny Q/R/B/N
   chooser instead of auto-queen. *(The core already generates all four.)*
2. **Last-move highlight.** Tint the from/to squares of the most recent move.
3. **Flip board.** Add a key to view from Black's side (mirror the layout math).
4. **Sprites.** Replace the letter glyphs with 16×16 hand-drawn piece sprites via
   `blit` — pure presentation, no core changes.

---

## 10. What's next

Chess is fully playable in both a window and the terminal, vs a human or the AI.
**Chapter 15** runs the **M1 acceptance** — perft, full games in both frontends, no
leaks, and the architecture invariant that the chess core stays free of SDL/engine
— then merges M1 into `main`.

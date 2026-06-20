# Chapter 11 — The Game Controller

> **Goal of this chapter.** Wrap the raw rules in a small, friendly **controller**
> that the AI and both frontends (GUI + TUI) can drive without knowing the internals
> of move generation: list legal moves, apply a move, detect game-over (checkmate /
> stalemate / draw), and translate moves to and from human coordinate notation.

---

## 1. Why a facade

Move generation (Chapter 10) is powerful but low-level — vectors of `Move`, make/
unmake, attack queries. The AI and UIs shouldn't repeat that plumbing; they want a
handful of questions answered:

- *What can I play?* → `legal_moves()`
- *Is the game over, and how?* → `result()` / `winner()`
- *Play this move.* → `play(m)`
- *What's the move "e2e4"?* → `find_move(...)` / `move_to_string(...)`

`Game` is that **facade**: it owns the current `State` and exposes exactly those
operations. This keeps each frontend tiny and the rules in one tested place — the
same "small units with clear interfaces" principle the whole engine follows.

```cpp
struct Game {
    State state = initial_state();
    std::vector<Move> legal_moves() const;
    bool   is_check() const;
    Result result() const;            // Ongoing / Checkmate / Stalemate / FiftyMoveDraw
    Color  winner() const;            // valid when result()==Checkmate
    bool   play(const Move& m);       // applies iff legal
    void   reset();
};
```

---

## 2. Detecting the end of the game

Almost everything keys off **one fact: are there any legal moves?**

```cpp
Result Game::result() const {
    auto moves = legal_moves();
    if (moves.empty())
        return is_check() ? Result::Checkmate : Result::Stalemate;  // mate vs stalemate
    if (state.halfmove_clock >= 100)                                // 50 moves = 100 plies
        return Result::FiftyMoveDraw;
    return Result::Ongoing;
}
```

The crucial distinction beginners miss: **checkmate and stalemate are the same
"no legal moves" condition** — they differ *only* by whether the side to move is
currently in check. In check + no moves = checkmate (you lose); not in check + no
moves = stalemate (draw). On checkmate, the *winner* is the side **not** to move
(they delivered it), which is why `winner() = opposite(side_to_move)`.

We include the **50-move rule** (100 plies without a pawn move or capture →
draw), tracked by `halfmove_clock` from Chapter 9. Threefold repetition is left as
a later refinement (it needs position history).

---

## 3. Coordinate notation

To play moves from a terminal (or test, or save file) we need text ⇄ `Move`. We use
**long algebraic / coordinate** notation: source square + destination square, plus
a promotion letter when needed:

```
   e2e4      pawn e2 to e4
   g1f3      knight g1 to f3
   e7e8q     pawn promotes to queen
   e1g1      king "to g1" = kingside castle (the engine knows it's a castle)
```

- `move_to_string(m)` builds it from the move's squares (+ promotion piece).
- `find_move(state, "e2e4")` generates the legal moves and returns the one whose
  string matches — so the caller gets the **fully-flagged** legal move (with
  `castle`/`en_passant` set), not a bare guess. Unparseable or illegal input simply
  yields `nullopt`.

`play(m)` matches the requested move against the legal list by from/to/promotion
and applies that legal move. So even if a frontend hands it a flag-less move, the
correct special-move behavior happens — and illegal moves are refused.

> Why coordinate notation and not SAN ("Nf3", "exd5")? Coordinate notation is
> unambiguous and trivial to parse — perfect for a TUI and tests. SAN is prettier
> for humans and a nice later upgrade, but not needed to *play*.

---

## 4. Run & observe

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

The chess tests now also cover: 20 legal moves at the start, playing `e2e4` flips
the side to move, `g1f3` round-trips through notation, fool's mate reports
`Checkmate` with Black as winner, the stalemate position reports `Stalemate`, a
promotion parses as `a7a8q`, and an illegal move is rejected by `play`.

---

## 5. Common pitfalls

- **Confusing checkmate and stalemate** — both are "no moves"; the check status
  decides. Getting this wrong turns a win into a draw (or vice-versa).
- **Wrong winner** — the mated side is the one *to move*; the winner is the other.
- **Trusting raw input** — always go through `find_move`/`play`, which validate.
- **Forgetting flags** — apply the *legal* move object, not a hand-built one, so
  castling/en passant execute correctly.

---

## 6. Glossary

- **Facade** — a small interface that hides a subsystem's complexity.
- **Result** — Ongoing / Checkmate / Stalemate / FiftyMoveDraw.
- **Coordinate (long algebraic) notation** — from-square + to-square (+ promo).
- **Halfmove clock** — plies since the last pawn move or capture (50-move rule).

---

## 7. Exercises

1. **Scholar's mate.** Play the move sequence for Scholar's mate via `find_move` +
   `play`, and assert `result() == Checkmate`.
2. **Insufficient material (design).** Sketch how you'd detect K vs K (and K+B/K+N
   vs K) as an automatic draw. Where would it slot into `result()`?
3. **SAN, lightly.** Write `move_to_san` for the easy cases (piece letter + dest,
   "O-O" for castling). Where does it get ambiguous?
4. **Threefold (design).** What would you store to detect threefold repetition, and
   what's the cost?

---

## 8. What's next

We have a complete, tested rules engine with a clean controller. **Chapter 12** is
the brain: a hand-written **minimax search with alpha-beta pruning** and a
material + piece-square **evaluation**, with difficulty levels by depth — the
opponent for Human↔Machine play.

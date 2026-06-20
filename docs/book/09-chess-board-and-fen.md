# Chapter 09 — Chess: The Board & FEN

> **Goal of this chapter.** Start M1 (chess) by modelling a *position* in code:
> how we number squares (mailbox 8×8), what a full game **State** must remember
> beyond piece placement, and how to read/write the standard **FEN** text format.
> FEN gives us a precise, testable way to set up any position — which we'll lean on
> heavily for the move-generation tests in Chapter 10.

---

## 1. Why a separate, UI-free "chess core"

Before any board is drawn, note the architecture: all chess *rules and logic* live
in `src/games/chess/` as a **plain-C++ library (`chess_core`) with no dependency
on the engine or SDL.** Both frontends — the GUI (in the window) and the TUI (in
the terminal) — drive the same core. Benefits:

- The rules are **unit-testable** with no window (perft, FEN, mate tests).
- The expensive, correctness-critical part (move generation, search) is written
  and verified **once**, independent of how it's displayed.

This is the same seam discipline as M0: isolate a well-bounded unit behind a clean
interface so it can be understood and tested on its own.

---

## 2. Representing the board: mailbox 8×8

There are several classic ways to store a chess board; we chose the most
transparent for learning — a **mailbox**: a flat array of 64 squares.

```cpp
std::array<Piece, 64> board;   // a1 = 0 … h8 = 63
```

We number squares with a single index, `0..63`:

```
   index = rank * 8 + file          file = index % 8   (a=0 … h=7)
                                     rank = index / 8   (rank 1 = 0 … rank 8 = 7)

   rank 8:  56 57 58 59 60 61 62 63
   rank 7:  48 ...
   ...
   rank 1:   0  1  2  3  4  5  6  7        e1 = file 4, rank 0 = 4
            a  b  c  d  e  f  g  h         e4 = file 4, rank 3 = 28
```

A `Piece` is just a `{PieceType, Color}` (with `None` for an empty square). Helpers
`make_square(file,rank)`, `file_of`, `rank_of`, `on_board` keep the index math in
one place so the move generator (next chapter) never open-codes it.

> Alternatives, for context: **0x88** stores the board in 128 slots so off-board
> tests are a single `&0x88`; **bitboards** pack each piece type into a 64-bit
> integer for blazing speed. Both are great later; mailbox is clearest now, and
> plenty fast for our alpha-beta depths.

---

## 3. A position is more than pieces: the `State`

If you only stored piece placement you couldn't answer "can White still castle?"
or "is en passant available?". The rules need six things — which is exactly what
FEN encodes:

```cpp
struct State {
    std::array<Piece,64> board;           // 1. where the pieces are
    Color side_to_move;                   // 2. whose turn
    bool  white_kingside, white_queenside,// 3. castling rights (4 bits)
          black_kingside, black_queenside;
    int   en_passant;                     // 4. square a pawn may capture onto, or -1
    int   halfmove_clock;                 // 5. plies since pawn move/capture (50-move)
    int   fullmove_number;                // 6. move count (for notation)
};
```

- **Castling rights** are lost permanently when the king or a rook moves (or the
  rook is captured) — so they must persist in the State, not be recomputed.
- **En passant** is only legal *immediately* after the enemy pawn's two-square
  push, so the State remembers the one square where it's possible this turn (or
  `-1`). This is the classic detail beginners forget.
- The **clocks** drive the 50-move draw rule and move numbering.

---

## 4. FEN: a position as one line of text

**Forsyth–Edwards Notation** writes a whole position as one string — the standard
way to share positions and, for us, to write tests:

```
rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
|--------- placement (rank 8 -> 1) ---------| |  |   | | |
                                              |  |   | | '- fullmove number
                                              |  |   | '- halfmove clock
                                              |  |   '- en-passant target ('-' = none)
                                              |  '- castling rights ('-' = none)
                                              '- side to move (w / b)
```

Placement rules: ranks are listed **from 8 down to 1**, separated by `/`; within a
rank, files go **a→h**; an **uppercase** letter is White, **lowercase** Black; a
**digit** means that many consecutive empty squares.

### Parsing (`parse_fen`)

We split on spaces and walk the placement string, starting at `rank=7, file=0`
(a8) and decrementing rank at each `/`. Letters place a piece, digits skip empties.
We validate as we go (each rank must total 8 files; bad characters → `nullopt`).
Returning `std::optional<State>` forces callers to handle a malformed string
instead of trusting it — the same "make failure explicit" idea as the asset loader.

> **The #1 FEN pitfall: rank order.** FEN starts at rank **8** (the top), but our
> index 0 is a1 (the bottom). So parsing decrements rank from 7 to 0. Get this
> backwards and the whole board is mirrored vertically.

### Formatting (`to_fen`) and the round-trip test

`to_fen` does the reverse, run-length-encoding empty squares back into digits. The
cheapest strong test of both is a **round-trip**: `to_fen(parse_fen(x)) == x` for
several positions (start, an en-passant position, a sparse endgame). If a field is
mishandled, the round-trip breaks immediately. That's what `tests/test_chess.cpp`
checks, alongside spot-checks of specific squares in the start position.

`initial_state()` is even defined as `parse_fen(kStartFEN)` — one source of truth,
and it exercises the parser every time the game starts.

---

## 5. Run & observe

```sh
cmake --build build
ctest --test-dir build --output-on-failure      # "chess: all tests passed"
```

`ascii_board(state)` renders a position for eyeballing / the TUI:

```
8 r n b q k b n r
7 p p p p p p p p
6 . . . . . . . .
...
1 R N B Q K B N R
  a b c d e f g h
```

---

## 6. Common pitfalls

- **Rank order in FEN** (rank 8 first) vs index 0 = a1 — easy to mirror.
- **Forgetting the non-board state** (castling/en-passant/clocks) — rules silently
  break later.
- **Trusting FEN input** — always handle the `nullopt`.
- **`char` sign issues** when classifying FEN chars — cast to `unsigned char`
  before `std::isupper/tolower`.

---

## 7. Glossary

- **Mailbox** — board stored as a flat 64-square array.
- **State / position** — board + side + castling + en passant + clocks.
- **FEN** — one-line text encoding of a position.
- **En-passant target** — the single square a pawn may capture onto this turn.
- **Round-trip test** — `format(parse(x)) == x`.

---

## 8. Exercises

1. **Print any position.** Paste a FEN from a real game into `parse_fen` and
   `ascii_board` it. Does the picture match?
2. **Square math.** Without running code, compute the index of `d5` and `g2`, then
   verify with `make_square`. *(Hint: file d=3, rank 5→4.)*
3. **Break the round-trip.** Temporarily make `to_fen` emit castling before side to
   move; watch the round-trip test fail. Revert. (Field order matters.)
4. **Add a field check.** Extend a test to assert the en-passant square of
   `"... w KQkq e6 0 2"` equals `make_square(4, 5)` (e6).

---

## 9. What's next

We can describe any position. **Chapter 10** is the heart of a chess engine:
generating **all legal moves** — including castling, en passant, and promotion —
and proving the generator correct with **perft** (counting leaf positions and
matching known reference numbers). Get this right and the rest of chess follows.

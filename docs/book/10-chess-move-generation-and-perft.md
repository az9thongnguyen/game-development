# Chapter 10 — Move Generation & perft

> **Goal of this chapter.** Generate every legal move in a position — including the
> tricky ones (castling, en passant, promotion) — and **prove the generator
> correct** with perft, the gold-standard chess test. This is the single most
> important and most bug-prone part of a chess engine; get it right and the rest
> (rules display, AI) is comparatively easy.

---

## 1. Pseudo-legal vs legal

We split move generation in two:

- **Pseudo-legal** moves follow how a piece moves, but *ignore* whether they leave
  your own king in check.
- **Legal** moves are pseudo-legal moves filtered so your king is safe afterwards.

Why split? Because "does this move leave my king in check?" is answered uniformly
for *all* pieces by one rule — make the move, ask if the king is attacked, undo —
instead of baking pin/check logic into every piece's generator. Simpler and far
less error-prone.

```cpp
void generate_legal(const State& s, std::vector<Move>& out) {
    std::vector<Move> pseudo;
    generate_pseudo_legal(s, pseudo);
    for (const Move& m : pseudo) {
        State c = s; make_move(c, m);                 // try it on a copy
        if (!is_square_attacked(c, king_square(c, us), opposite(us)))
            out.push_back(m);                         // king safe → legal
    }
}
```

---

## 2. Moving pieces on a mailbox

Because the board is a flat 64 array, we must move by **(file, rank) deltas** and
bounds-check with `on_board`, never by raw `index ± k` (which would wrap around
edges — a knight on a1 would "jump" to the h-file). Direction tables encode each
piece's geometry:

```cpp
KNIGHT[8]  = {(1,2),(2,1),(2,-1),(1,-2),(-1,-2),(-2,-1),(-2,1),(-1,2)};
KING_DIR[8]= the 8 neighbours;   BISHOP_DIR[4]= diagonals;  ROOK_DIR[4]= orthogonals;
```

- **Knights / king**: one hop per direction; land if empty (quiet) or enemy
  (capture).
- **Bishops / rooks / queens** *slide*: step along a direction until you hit the
  edge, an enemy (capture, then stop), or a friend (stop). `slide_moves` does this
  for any direction set; the queen just uses both bishop and rook directions.

---

## 3. The pawn (where most bugs live)

Pawns are the fiddly piece — they move differently than they capture, have a
double first step, promote, and capture en passant:

- **Push**: one square forward if empty; from the start rank, a **double push** if
  both squares are empty (and it sets the en-passant target).
- **Captures**: diagonally forward onto an enemy.
- **Promotion**: any push/capture reaching the last rank generates **four** moves
  (Q, R, B, N) — forgetting under-promotions is a classic perft mismatch.
- **En passant**: if the diagonal-forward square equals the position's
  `en_passant` target (set by the enemy's last double push), capture there — and
  the captured pawn is *beside* you, not on the target square.

`forward`, `start_rank`, and `promo_rank` flip with color so one code path serves
both sides.

---

## 4. Attacks, check, and castling

`is_square_attacked(s, sq, by)` asks "does any `by` piece attack `sq`?" by looking
*outward* from `sq`: adjacent pawn-attack squares, knight hops, king neighbours,
and sliding rays for bishop/rook/queen. It's the workhorse behind:

- **Check**: `in_check(s, c)` = is `c`'s king square attacked by the opponent.
- **Castling**: legal only if the king and rook haven't moved (tracked in the
  State's rights), the squares between are empty, and the king is **not in check,
  doesn't pass through an attacked square, and doesn't land on one**. Those three
  "not attacked" checks are exactly where `is_square_attacked` earns its keep.

---

## 5. make / unmake (and why reversibility matters)

`make_move` applies a move and returns an `Undo` capturing everything needed to
reverse it; `unmake_move` restores the prior State **exactly**. We need this
because the search (perft now, AI next chapter) explores millions of positions —
copying the whole board at every node would be wasteful, so we mutate in place and
undo.

The details `make_move` must handle (each a potential bug):

- normal vs **en-passant** capture (the captured pawn is on a *different* square
  than the destination),
- **promotion** (replace the pawn; on undo, demote back to a pawn),
- **castling** (move the rook too; undo it),
- **castling-rights updates** (lose them when the king/rook moves, or when a rook
  is *captured* on its home square),
- **en-passant target** (set only after a double push, cleared otherwise),
- the **clocks** and side to move.

The invariant we rely on: `unmake_move(s, make_move(s, m))` leaves `s` byte-for-
byte unchanged. perft is precisely what proves this holds.

---

## 6. perft: proving it all correct

**perft(depth)** counts the number of leaf positions reachable in exactly `depth`
plies, playing *all legal moves*:

```cpp
long long perft(State& s, int depth) {
    if (depth == 0) return 1;
    std::vector<Move> moves; generate_legal(s, moves);
    if (depth == 1) return moves.size();
    long long n = 0;
    for (auto& m : moves) { Undo u = make_move(s,m); n += perft(s, depth-1); unmake_move(s,u); }
    return n;
}
```

The magic: these counts are **published and exact**, so they catch *any* bug —
a missing under-promotion, a wrong en-passant square, an illegal castle, a
make/unmake that doesn't perfectly reverse — as a number mismatch. Our tests check:

| Position | d1 | d2 | d3 | d4 |
|----------|----:|----:|-----:|------:|
| Start | 20 | 400 | 8902 | 197281 |
| Kiwipete | 48 | 2039 | 97862 | — |
| EP endgame | 14 | 191 | 2812 | — |

"Kiwipete" is deliberately dense (castling both sides, en passant, pins,
promotions), so matching it is strong evidence the generator is fully correct. All
of ours match. ✅

> **Debugging tip: perft divide.** If a count is wrong, print perft(depth-1) for
> each root move ("divide") and compare against a reference engine move-by-move;
> the move whose subtotal differs localizes the bug fast. (We didn't need it — but
> it's the standard technique when perft disagrees.)

---

## 7. Common pitfalls (all caught by perft)

- **Index wraparound** on a mailbox — always step by file/rank with `on_board`.
- **Missing under-promotions** — generate all four promotion pieces.
- **En passant**: capture square ≠ destination square; only legal the immediate
  move; remember to clear the target afterwards.
- **Castling through check** — check king start, pass-through, and destination.
- **Castling rights** — also lost when a rook is *captured* on its home square.
- **make/unmake asymmetry** — promotion must demote on undo; restore the captured
  piece on the right square (en passant!).

---

## 8. Glossary

- **Pseudo-legal / legal** — moves before / after the king-safety filter.
- **Slide** — extend a sliding piece along a ray until blocked.
- **make / unmake** — apply / exactly reverse a move.
- **perft** — leaf-node count to a depth; the canonical move-gen test.
- **perft divide** — per-root-move perft, for localizing a mismatch.
- **Kiwipete** — a famous dense test position.

---

## 9. Exercises

1. **Count by hand.** From the start position there are 20 moves (16 pawn + 4
   knight). List them and confirm `perft(1) == 20`.
2. **Break it, watch perft scream.** Temporarily generate only a Queen promotion
   (drop R/B/N). Re-run — which perft depth first changes, and by how much?
3. **Add a position.** Find another reference perft position online, add it to the
   test, and confirm. (Coverage you trust = bugs you'll never ship.)
4. **Divide.** Write a small loop that prints each root move (as `e2e4`) with its
   `perft(depth-1)` subtotal from the start position.

---

## 10. What's next

Move generation is correct and proven — the foundation everything rests on.
**Chapter 11** turns it into a *game*: detecting checkmate / stalemate / draws and
exposing a clean controller (legal-move list, apply move, game-over state) that the
AI and both frontends will drive.

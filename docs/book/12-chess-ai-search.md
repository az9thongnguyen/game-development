# Chapter 12 — The AI: Evaluation + Minimax/Alpha-Beta

> **Goal of this chapter.** Build the opponent: a hand-written search that looks
> ahead several moves and picks the best one. Two pieces — an **evaluation** that
> scores a position, and a **minimax search with alpha-beta pruning** that assumes
> both sides play their best. Difficulty is simply how deep it looks.

---

## 1. Evaluation: turning a position into a number

The search needs a verdict on quiet positions: who's better, and by how much? Our
`evaluate` returns **centipawns** (100 = one pawn) and combines two ideas:

- **Material** — the classic values: P=100, N=320, B=330, R=500, Q=900. Just
  counting these already plays "don't lose pieces, win pieces."
- **Piece-square tables (PSTs)** — a per-square bonus/penalty giving *positional*
  sense: knights love the center, pawns want to advance, the king should hide in
  the corner during the midgame. We use the well-known "Simplified Evaluation"
  tables.

```cpp
int v = material(type) + pst[type][ color==White ? (sq ^ 56) : sq ];
score += (color == White) ? v : -v;   // White adds, Black subtracts
return side_to_move == White ? score : -score;   // ...then side-to-move's view
```

Two subtleties:

- **Orientation.** The tables are written rank-8-first from White's view; a White
  piece flips its square with `sq ^ 56` (flip the rank), a Black piece uses the
  square directly — which mirrors the table for it. The start position evaluates to
  exactly **0** (perfectly symmetric), which the tests assert.
- **Perspective.** We return the score from the **side to move's** point of view
  (positive = good for them). That's what lets the search use *negamax* (below).

---

## 2. Minimax, the negamax way

Minimax: I pick the move that maximizes my score; my opponent replies with the move
that minimizes it; and so on to some depth, where we evaluate. Because our eval is
"from the side to move's view," the opponent's best score is just the negative of
ours — so min and max collapse into one function, **negamax**:

```cpp
int negamax(State& s, int depth, int ply, int alpha, int beta) {
    if (depth == 0) return evaluate(s);
    moves = generate_legal(s);
    if (moves.empty())                                 // terminal
        return in_check(s, stm) ? -(MATE - ply) : 0;   // mated : stalemate
    int best = -INF;
    for (m : moves) {
        make_move(s, m);
        best = max(best, -negamax(s, depth-1, ply+1, -beta, -alpha));  // note the minus
        unmake_move(s, ...);
        ...
    }
    return best;
}
```

The single `-negamax(..., -beta, -alpha)` is the whole trick: flip the score and
swap/negate the window for the opponent.

**Mate scoring.** A checkmate returns `-(MATE - ply)`: a huge negative for the side
being mated, made slightly *less* extreme the deeper it is. Subtracting `ply` makes
the engine prefer mate **in 2** over mate **in 4**, and to delay being mated.
(Because mate is only detected when a node has *no moves*, a mate-in-1 is seen at
depth ≥ 2 — fine, since our easiest level is depth 2.)

---

## 3. Alpha-beta pruning

Plain minimax explores every branch — exponential and wasteful. **Alpha-beta**
proves that most branches can't matter and skips them, returning the *same* answer
far faster.

`alpha` = the best score I'm already guaranteed; `beta` = the best the opponent will
allow. If a move's reply makes the score ≥ `beta`, the opponent would never let me
reach this position (they have a better option earlier), so we **cut off** —
`break` — without examining the rest:

```cpp
if (best > alpha) alpha = best;
if (alpha >= beta) break;   // beta cutoff
```

How much it saves depends entirely on **move ordering**: if you try the best move
first, the cutoffs happen immediately. With perfect ordering alpha-beta searches
roughly the *square root* of minimax's nodes — letting us go twice as deep for the
same work.

### Move ordering (MVV-LVA)

We sort captures first, by **Most Valuable Victim − Least Valuable Attacker**:
grabbing a queen with a pawn is tried before a quiet rook shuffle. Cheap to compute,
and it makes the pruning dramatically more effective:

```cpp
key = capture ? 10000 + victim*10 - attacker : 0;   // + promotion bonus
stable_sort(moves, by key descending);
```

---

## 4. Difficulty = depth

```cpp
enum class Difficulty { Easy = 2, Medium = 4, Hard = 6 };
```

Each extra ply makes the engine markedly stronger (and slower). `search` returns the
chosen `Move`, its `score`, and the **node count** — the last so you can *see*
alpha-beta working: compare nodes with ordering on vs off, or alpha-beta vs a plain
minimax, and watch the count plummet for the same move.

---

## 5. What the tests prove

- `evaluate(start) == 0` — symmetric and unbiased.
- Depth-2 search finds the **back-rank mate** `a1a8` with a mate-level score.
- Depth-3 search **captures a hanging queen** (`Rxd5`) — material + search working
  together — and always returns a *legal* move.

---

## 6. Limitations (honest, and where M1+ could improve)

- **No quiescence search** → the *horizon effect*: the engine might stop searching
  mid-capture-sequence and misjudge a trade. A quiescence search (extend captures
  at depth 0) is the standard fix and a great next exercise.
- **No transposition table** → it re-searches positions reached by different move
  orders. A hash table of seen positions (Zobrist keys) is the classic speedup.
- **Simple eval** → no mobility, king safety, or pawn structure terms. Enough to
  play a reasonable game; not a grandmaster.

These are deliberate: M1's goal is a correct, understandable opponent, not a
world-class one.

---

## 7. Glossary

- **Centipawn** — score unit; 100 = one pawn.
- **Piece-square table (PST)** — per-square positional bonus.
- **Minimax / negamax** — best-play search; negamax = minimax via score negation.
- **Alpha-beta** — pruning that skips provably irrelevant branches.
- **Beta cutoff** — stopping a node because the opponent won't allow it.
- **MVV-LVA** — capture-ordering heuristic.
- **Horizon effect** — misjudging because the search stopped mid-tactic.

---

## 8. Exercises

1. **See the pruning.** Temporarily disable `order_moves` and compare `result.nodes`
   at depth 4 from the start. How much does ordering save?
2. **Tune a value.** Bump bishop material to 350 and see if move choices change in a
   few positions. Why might "bishop > knight" be reasonable?
3. **Mate in two.** Find a mate-in-2 position, search at depth 4, and confirm the
   first move + score.
4. **Quiescence (stretch).** At depth 0, instead of returning `evaluate`, search
   *just the captures* until quiet, then evaluate. Does it stop hanging pieces in
   trades?

---

## 9. What's next

The engine can play. **Chapter 13** gives it a face in the terminal — the **TUI**:
print the board, read coordinate moves, and run Human↔Human or Human↔Machine games
end to end, all driven by the `Game` controller.

# Chapter 15 — M1 Acceptance

> **Goal of this chapter.** Confirm M1 is *done* against the requirements and the
> spec: full legal chess, two play modes (Human↔Human and Human↔Machine), a
> hand-written minimax/alpha-beta AI with difficulty levels, in both a GUI and a
> TUI — correct, leak-free, and architecturally clean — then merge it to `main`.

---

## 1. What M1 delivered

| Subsystem | Where | Verified by |
|-----------|-------|-------------|
| Board + position + FEN | `board`, `fen` | FEN round-trip tests |
| Full legal move generation | `movegen` | **perft** (start, Kiwipete, EP) |
| Castling / en passant / promotion | `movegen` + `make/unmake` | perft (these are what makes Kiwipete match) |
| Check / checkmate / stalemate / 50-move | `game` | mate + stalemate tests |
| Coordinate notation | `game` | parse/format tests |
| AI: eval + minimax + alpha-beta | `evaluate`, `search` | eval symmetry, mate-in-1, capture-hanging-queen |
| TUI frontend (HvH / HvAI) | `chess_tui` | scripted Fool's mate → checkmate |
| GUI frontend (HvH / HvAI) | `chess_gui` | builds + headless run; interactive play |

---

## 2. The acceptance checks (and results)

Mapping to `requirements.md` §7 (M1) and the M1 spec:

| Criterion | How verified | Result |
|-----------|--------------|--------|
| Full legal rules incl. special moves | `perft` matches reference counts | ✅ start 20/400/8902/197281; Kiwipete 48/2039/97862; EP 14/191/2812 |
| Check / checkmate / stalemate | FEN positions through `result()` | ✅ |
| Highlight legal moves | GUI draws dots from `legal_moves()` | ✅ |
| Mouse to select & move | GUI click latch → `on_click` | ✅ |
| AI minimax + alpha-beta, ≥2 difficulties | `search`, Easy/Medium/Hard (2/4/6) | ✅ finds mate-in-1, wins material |
| Human-vs-Human **and** Human-vs-Machine | `--gui/--tui [hvh\|hvai]` | ✅ both modes |
| **GUI and TUI** | one binary, mode flag | ✅ `--gui` / `--tui` |
| Play a full game vs AI, no crash, no illegal move | scripted + interactive | ✅ (TUI scripted mate; GUI headless exit 0) |
| No leaks | `leaks --atExit` on a TUI game | ✅ 0 leaks |
| Warning-clean build | `-Wall -Wextra -Wpedantic` | ✅ |
| **Architecture: core free of SDL/engine** | `grep` includes + `SDL_` usage | ✅ only `chess_gui` touches the engine; no `SDL_` in chess at all |

The last row is the structural win: the **entire chess core** (`board`, `movegen`,
`game`, `evaluate`, `search`, `fen`) is plain C++ that compiles, runs, and is
tested with **no window** — exactly why both frontends could share it, and why the
TUI exists at all.

How to reproduce:

```sh
cmake --build build && ctest --test-dir build --output-on-failure   # perft + rules + AI
printf 'f2f3\ne7e5\ng2g4\nd8h4\n' | ./build/demo --tui hvh           # -> Checkmate! Black wins
./build/demo --gui hvai medium                                       # play vs the AI
grep -rEn 'SDL_[A-Za-z]' src/games/chess || echo "no SDL in chess core"
```

---

## 3. Honest limitations (and where they'd improve)

- **Promotion in the GUI auto-queens** (the core supports all four; a picker is
  Chapter 14, exercise 1).
- **AI has no quiescence search** → possible horizon effect in sharp tactics;
  **no transposition table** → re-searches transpositions; **simple eval** (material
  + piece-square only). All deliberate for clarity; all standard next upgrades.
- **Draws:** stalemate and the 50-move rule are detected; **threefold repetition**
  and **insufficient material** are not yet.
- **Hard (depth 6)** can pause the GUI briefly (single-threaded by design).

None of these affect legality or "play a correct full game" — they're strength /
polish, noted so the engine's scope is honest.

---

## 4. M1 is complete

A from-scratch chess engine — correct to perft, with a real AI — playable in a
window and a terminal, sharing one UI-agnostic core, all built on the M0 engine.
The `feat/m1-chess` branch merges into `main` as the M1 milestone.

---

## 5. Glossary

- **Acceptance** — the checklist that defines "done" for the milestone.
- **perft** — leaf-count move-gen proof (Chapter 10).
- **Architecture invariant** — a property enforced mechanically (here, "no SDL/
  engine in the chess core").

---

## 6. What's next (M2)

With chess shipped, the roadmap continues to **M2 — an FPS raycaster**
(Wolfenstein-style): a 2D grid map, per-column raycasting with textured walls,
billboarded enemy sprites, grid collision, and the engine's first **real audio**.
It's the stepping stone to the true 3D core at M3. Like M1, it gets its own spec,
plan, and chapters — built on everything here.

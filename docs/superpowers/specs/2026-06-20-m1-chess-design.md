# M1 — Chess: Design & Implementation Plan

> Status: **spec + plan, not yet implemented.** Build in a fresh session (cost +
> lean context; GateGuard is disabled for this repo from the next session). Built
> on the M0 engine (see `docs/book/00-overview.md`).

## Context & goals

M1 is the first real game on the engine. From `requirements.md` §7 / §11:

- Full legal chess: every piece's moves, **castling, en passant, promotion**,
  check, checkmate, stalemate. Highlight legal moves.
- **Two play modes:** Human↔Human and Human↔Machine.
- **AI:** hand-written **minimax + alpha-beta**, 2–3 difficulty levels (by search
  depth), evaluation = material + basic position (piece-square tables).
- **Two frontends:** **GUI** (SDL window, our `renderer2d`) and **TUI** (terminal).
- Acceptance: play a full legal game vs AI, no crash, no illegal move.

### Decisions (confirmed)
- **Board representation:** **mailbox 8×8** (`array<Piece,64>`) — clearest for
  learning move-gen; fast enough for alpha-beta at depth 3–6.
- **Packaging:** **one binary, mode flag** — `chess --gui` (default) / `chess --tui`.
  A single UI-agnostic **chess core** library is shared by both frontends.

## Architecture

```
        [ chess_gui (engine::Scene) ]     [ chess_tui (terminal loop) ]
                       \                   /
                        \                 /
                     [ chess core  (UI-agnostic) ]
                       board · move · movegen · rules · make/unmake
                       · fen · evaluate · search(minimax+ab) · game(controller)
                                     |
                              [ M0 engine ]   (GUI only: renderer2d, input, scene)
```

The **core has zero dependency on the engine or SDL** — it's plain C++ that the
TUI can use with no window. Only `chess_gui` touches the engine. This mirrors the
M0 seam discipline and makes the core trivially unit-testable (perft, FEN, mates).

### Core data model (`src/games/chess/`)
- `Piece` enum (None, WP..WK, BP..BK) or `{Color, Type}`; board = `Piece[64]`,
  index = `rank*8 + file` (a1 = 0 … h8 = 63).
- `State`: board, side to move, castling rights (4 bits), en-passant target square
  (or none), halfmove clock, fullmove number. (Enough for rules + draws later.)
- `Move`: from, to, promotion type, flags (capture, double-push, en-passant,
  castle). Small POD.
- `make_move(State&, Move) -> Undo` / `unmake_move(State&, Undo)` — fast, no alloc;
  required for search.

### Move generation + rules
- Pseudo-legal generation per piece via offset tables (knights/king), sliding
  loops (bishop/rook/queen), pawn pushes/captures/promotions/en-passant, plus
  castling (squares empty + not attacked).
- `is_square_attacked(State, sq, by_color)` underpins check detection and castling.
- **Legality filter:** make the move, reject if own king is in check, unmake.
- End states: checkmate (no legal moves + in check), stalemate (no legal moves +
  not in check). (50-move / threefold repetition: optional, see Out of scope.)
- `FEN` parse/format — for setting positions, tests, and (later) save/load.

### AI (`evaluate` + `search`)
- `evaluate(State) -> int` (centipawns, from side-to-move POV): material values
  (P100 N320 B330 R500 Q900) + **piece-square tables** for basic positional sense.
- `search`: minimax with **alpha-beta pruning**, fixed depth. Difficulty =
  Easy(2) / Medium(4) / Hard(6) ply. Move ordering (captures first, MVV-LVA) to
  make pruning effective. Returns best `Move`.
- Mate handling: large scores ± depth so it prefers faster mates / avoids them.

### Frontends
- **chess_gui** (`engine::Scene`): draw 8×8 board with `fill_rect` (light/dark
  squares), pieces rendered as bold letter-glyphs via our font (K Q R B N P, white
  vs dark) for v1 — hand-drawn piece sprites are an easy later upgrade. Mouse:
  click-select a piece → highlight legal target squares (`draw_rect`) → click to
  move; promotion picker; status line (turn, check, mate, "AI thinking"). A simple
  start menu chooses HvH / HvAI + difficulty + side.
- **chess_tui**: print the board as text each turn (files/ranks labels), read moves
  from stdin in coordinate form (e.g. `e2e4`, `e7e8q`), validate against legal
  moves, show check/mate; same `game` controller drives it. Runs a plain
  read-eval loop (no engine window) — allowed because it's a separate mode.
- `main`: parse `--gui` (default) / `--tui`; GUI path does `platform::init` + run
  a `ChessScene`; TUI path runs the terminal loop. (No blocking loop leaks into
  the engine; TUI is its own program mode.)

## Build order (each step = code + guidebook chapter + checkpoint)

1. **Board + FEN + text dump.** `State`, `Piece`, FEN parse/format; print a
   position. *Checkpoint:* start position prints correctly. *Chapter 09.*
2. **Legal move generation + make/unmake + perft.** Full generation incl. specials
   + legality. *Checkpoint:* `perft` matches known counts (20, 400, 8902, 197281;
   plus the "Kiwipete" position). *Chapter 10.* ← the correctness keystone.
3. **Rules surface + end states.** check / checkmate / stalemate; legal-move list
   API. *Checkpoint:* mate/stalemate FEN tests pass. *Chapter 11.*
4. **AI: evaluate + minimax + alpha-beta + ordering + difficulty.** *Checkpoint:*
   solves mate-in-1/2 puzzles; alpha-beta visits far fewer nodes than plain
   minimax (logged). *Chapter 12.*
5. **Game controller + TUI.** Modes HvH / HvAI; play in the terminal end to end.
   *Checkpoint:* full legal game vs AI in TUI, no illegal moves. *Chapter 13.*
6. **GUI ChessScene.** Board + pieces + mouse select/move + legal highlights +
   promotion + status; start menu. *Checkpoint:* full game vs AI in the window.
   *Chapter 14.*
7. **`main --gui/--tui` dispatch + M1 acceptance.** Perft green, rule/mate tests
   green, full games both frontends, leak check. *Chapter 15.*
8. *(optional, later)* save/load via FEN/PGN.

Natural review pauses: after **step 2** (perft proves move-gen) and **step 6**
(playable GUI).

## Testing / verification
- **perft** at depths 1–4 from the start position and from the "Kiwipete"
  position (catches castling/en-passant/promotion bugs) — the single most
  important test for a chess engine.
- FEN round-trip tests; specific rule positions (castle through check illegal, en
  passant only immediately, promotion to all 4 pieces, pinned-piece can't move).
- AI: mate-in-1/2 test positions; assert alpha-beta result == plain minimax result
  (same best score) but with fewer nodes.
- Full-game smoke (TUI scripted moves; GUI headless via `HAND_ENGINE_FRAMES`).
- `leaks` clean; warning-clean build; core has no SDL/engine includes (grep).

## File layout (new)
```
src/games/chess/
  piece.hpp  board.hpp/.cpp  move.hpp  movegen.cpp  rules.cpp
  fen.hpp/.cpp  evaluate.hpp/.cpp  search.hpp/.cpp  game.hpp/.cpp
  chess_gui.hpp/.cpp           # engine::Scene
  chess_tui.hpp/.cpp           # terminal mode
src/main.cpp                   # add --gui/--tui dispatch
tests/test_chess.cpp           # perft + rules + mates (CTest)
docs/book/09..15-*.md          # guidebook chapters
assets/                        # (optional) piece sprites later
```

## Out of scope for M1
- Bitboards, magic move-gen (mailbox is enough; perf revisited only if needed).
- Opening book, advanced eval (mobility/king-safety/pawn structure), endgame
  tablebases.
- 50-move / threefold-repetition draws and PGN/network — optional follow-ups.
- Piece *sprite art* (letter-glyphs first; sprites are a cosmetic upgrade).

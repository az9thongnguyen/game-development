# Chapter 13 — The Terminal Frontend (TUI)

> **Goal of this chapter.** Make the chess engine *playable* with the simplest
> possible interface — text in the terminal. The TUI uses no window and no SDL; it
> just prints the board and reads coordinate moves from stdin. Building it first
> proves the chess core is genuinely UI-agnostic, and gives us a fast way to play
> and script full games.

---

## 1. Why a TUI first

The GUI (next chapter) needs the window, the renderer, input handling, layout. The
TUI needs none of that — `std::cout` and `std::cin`. So it's the quickest path to
"I can actually play a game," and, crucially, it **drives the exact same `Game`
controller** the GUI will. If a full game works in the TUI, the rules/AI are sound;
the GUI then only adds *presentation*.

This is the UI-agnostic-core payoff: the same engine, two faces.

```
   chess_tui (stdin/stdout)   chess_gui (window)
              \               /
            Game  (controller)  <- identical logic for both
```

---

## 2. The loop

`run_tui(vs_ai, depth)` is the whole frontend — a read/decide/print loop over the
`Game`:

```cpp
while (g.result() == Result::Ongoing) {
    print(ascii_board(g.state));                 // show the position
    print(side_to_move + (is_check ? " (check)"));
    if (vs_ai && black_to_move) {
        Move m = search(g.state, depth).best;     // AI's turn
        print("AI plays " + move_to_string(m));
        g.play(m);
    } else {
        getline(cin, line);                       // human's turn
        if (line == "quit") break;
        if (auto m = find_move(g.state, line)) g.play(*m);
        else print("Illegal or unknown move");
    }
}
print_result(g);                                  // checkmate / stalemate / draw
```

Everything hard was already built: `ascii_board` (Chapter 9) renders, `find_move`
(Chapter 11) parses + validates coordinate input, `search` (Chapter 12) is the AI,
and `result()`/`winner()` end the game. The TUI is just glue — which is exactly
what a good frontend should be.

- **Modes.** `vs_ai = false` → Human vs Human (both sides typed in). `vs_ai = true`
  → Human is White, the AI plays Black at `depth`.
- **Input.** Coordinate notation (`e2e4`, `e7e8q`); illegal/garbled input is
  rejected and re-prompted, never crashes (thanks to `find_move` returning
  `optional`). `quit` / EOF exits cleanly — which also makes games **scriptable**
  by piping moves on stdin.

---

## 3. Dispatch in `main`

`main` now chooses a mode from the command line:

```
demo                 -> M0 engine demo (window)
demo --tui [hvh|hvai] [easy|medium|hard]   -> chess in the terminal
demo --gui           -> chess window (added next chapter)
```

The TUI path calls `run_tui`; the GUI path brings up the platform + engine. Note
the TUI never touches the platform window — it's a separate program *mode*, so the
"no blocking loop in the engine" rule is untouched (the TUI's `while` is its own
top-level loop, not inside `engine`/`platform`).

---

## 4. Run & observe

Play interactively (you are White vs a Medium AI):

```sh
cmake --build build
./build/demo --tui hvai medium
```

Or **script a whole game** — here Fool's mate, human vs human:

```sh
printf 'f2f3\ne7e5\ng2g4\nd8h4\n' | ./build/demo --tui hvh
# ... ends with:  Checkmate! Black wins.
```

A quick AI sanity check:

```sh
printf 'e2e4\nquit\n' | ./build/demo --tui hvai easy   # AI replies, e.g. "AI plays b8c6"
```

Scripting like this is a cheap regression test for the whole stack — rules,
notation, and AI together.

---

## 5. Common pitfalls

- **Crashing on bad input** — always validate via `find_move`; never assume the
  string is a legal move.
- **Forgetting EOF** — `getline` returning false (piped input ends) must break the
  loop, or it spins.
- **Mixing UI into the core** — the TUI should only *call* the controller; no rules
  logic belongs here. (If you're tempted to special-case a rule in the frontend,
  it belongs in the core.)
- **Flushing prompts** — flush the `> ` prompt so it appears before blocking on
  input in an interactive terminal.

---

## 6. Glossary

- **TUI** — text user interface (terminal).
- **Frontend** — presentation/input layer over the game core.
- **Coordinate notation** — `from``to`(+promo), e.g. `e7e8q`.
- **Scripted game** — moves piped on stdin for automated testing.

---

## 7. Exercises

1. **Add `undo`.** Keep a stack of `Undo` and let the human type `undo` to take
   back a move (and the AI's reply). *(Hint: you already have `unmake_move`.)*
2. **Pick your color.** Add a flag so the human can play Black (AI moves first).
3. **Show last move + eval.** After each move, print it and `evaluate(state)` so you
   can watch the score swing.
4. **Legal-move help.** On input `moves`, list all legal moves via
   `move_to_string`. Great for learning the notation.

---

## 8. What's next

Chess is fully playable — in text. **Chapter 14** gives it a real face: the **GUI
ChessScene** in the window, drawing the board and pieces with our 2D renderer,
selecting and moving with the mouse, highlighting legal moves, and showing game
status — the same `Game` underneath.

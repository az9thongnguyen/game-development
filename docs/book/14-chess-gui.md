# Chapter 14 — The Windowed Frontend (GUI) & the Image Pipeline

> **Goal of this chapter.** Give chess a real face in a large, crisp window: draw
> the board, blit **real piece artwork** (hand-loaded from our own `.hrt` image
> format — no SDL_image), select and move with the mouse, highlight legal moves,
> show game status, and let the AI reply. Crucially, it drives the **same `Game`
> controller** as the TUI — the GUI adds only presentation and input, no rules.
> Along the way we build a reusable **image pipeline**: an offline asset script,
> a tiny raster format, and a from-scratch loader.

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

Chess runs in a **large, crisp window** (980×720, `smooth = true`, `highdpi = true`
— see Chapter 2), not the 480×270 retro demo. The board is 8×8 squares of **80px**
(`kSquare`), so 640×640, at origin `(24, 24)`, leaving the right side (`kStatusX`)
for a status panel.

- **Squares:** a double loop fills each square light or dark. `(file + rank) % 2`
  picks the color (a1 is dark). Screen-y grows *down*, so rank 8 is drawn at the
  top: `y = kOY + (7 - rank) * kSquare`.
- **Pieces:** each piece is a **blitted sprite** — real artwork loaded from our own
  `.hrt` files (next section). If a sprite is missing, we fall back to drawing the
  piece's **letter** (P N B R Q K) via the bitmap font at scale 6, so the GUI is
  always usable even before the art is fetched:

```cpp
const gfx::Image& im = images_[ci][int(p.type)];
if (!im.pixels.empty())
    g.blit(gfx::Sprite{ im.pixels.data(), im.w, im.h }, x, y);   // real art
else
    g.draw_text(x + 16, y + 16, letter, color, 6);               // graceful fallback
```

> The letter path used to be the whole story (an honest v1 that needed zero assets).
> It now survives only as a fallback — proof that "how a piece looks" stays isolated
> to these few lines, exactly as a clean presentation layer should.

---

## 2b. The image pipeline: `.hrt`, the loader, and the offline script

Loading a PNG normally means a library (SDL_image, stb_image, …). The project's
thin-shim rule forbids that — so we built a tiny **image pipeline** of our own, in
three pieces. It's reusable: any scene (FPS textures, future tools) can load art the
same way.

### 1) A dead-simple raster format: `.hrt`

We don't parse PNG at runtime. Instead, art is pre-converted to a trivial format we
*can* parse in a dozen lines:

```
  magic "HRT1" | uint32 BE width | uint32 BE height | RGBA8 rows (w*h*4 bytes)
```

No compression, no chunks, no color tables — just a header and raw RGBA pixels. The
point is pedagogy and honesty: every byte is something we wrote the code to read.

### 2) The offline asset script: `scripts/fetch_pieces.py`

A **build-time** Python script (not part of the engine, not shipped) prepares the
art once:

- downloads the public-domain *Cburnett* chess SVGs→PNG thumbnails from Wikimedia,
- decodes the PNGs using only Python's stdlib `zlib` (no Pillow — same no-library
  spirit as the C++ side),
- area-scales them to the 80px board square,
- writes `assets/pieces/{w,b}{K,Q,R,B,N,P}.hrt` (+ a `CREDITS.txt` for the licence).

It's resumable (skips pieces already produced). Keeping decoding/scaling *offline*
means the engine ships only the trivial `.hrt` loader, never a PNG decoder.

### 3) The from-scratch loader: `engine/image.cpp`

`gfx::load_image(path)` reads the bytes through the **asset seam** (Chapter 7 — so
it works on the web too), validates the magic + size, then converts each RGBA
texel to our ARGB8888 `Color`:

```cpp
if (!(b[0]=='H'&&b[1]=='R'&&b[2]=='T'&&b[3]=='1')) return std::nullopt;
const uint32_t w = read_be32(&b[4]), h = read_be32(&b[8]);
if (b.size() < 12 + size_t(w)*h*4) return std::nullopt;     // bounds check
for (each texel i)
    img.pixels[i] = rgba(px[i*4+0], px[i*4+1], px[i*4+2], px[i*4+3]);  // RGBA→ARGB
```

Note the big-endian read (`read_be32`) — the format stores dimensions
network-order, so the loader is explicit about byte order rather than `memcpy`-ing a
`uint32_t` and hoping the host matches. The returned `Image` is just `{w, h,
vector<Color>}` — exactly what `Renderer2D::blit` consumes.

### Putting it together

`ChessScene`'s constructor loads all twelve sprites once into `images_[2][7]`
(`[color][PieceType]`); if any fail, `images_ok_` flips and the letter fallback
kicks in. The pieces have **transparent backgrounds** (alpha in the `.hrt`), so
`blit` — which alpha-blends (Chapter 5) — composites them cleanly over the board
squares. Real art, zero third-party image code.

---

## 3. Mouse → square, and click handling

`square_at(mouse_x, mouse_y)` inverts the layout: subtract the origin, divide by
square size, and flip the rank (`7 - fy`) because of screen-y direction. Out-of-
board clicks return `-1`. Because input is in *framebuffer* coordinates (Chapter
6 — the backend maps window pixels to the framebuffer by ratio), this lines up
exactly with what's drawn, regardless of the window's scaling or HiDPI.

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
4. **Your own art.** Point `fetch_pieces.py` at a different piece set (or draw your
   own `.hrt` by hand) and drop the files in `assets/pieces/` — the GUI picks them
   up with no code change. Then delete one file and watch the letter fallback for
   that piece kick in.

---

## 10. What's next

Chess is fully playable in both a window and the terminal, vs a human or the AI.
**Chapter 15** runs the **M1 acceptance** — perft, full games in both frontends, no
leaks, and the architecture invariant that the chess core stays free of SDL/engine
— then merges M1 into `main`.

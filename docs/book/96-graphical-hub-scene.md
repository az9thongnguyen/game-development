# Chapter 96 — The Graphical Hub: One View Model, Two Renderers

> Code: `src/engine/hub/hub.{hpp,cpp}` (`hub_lines`) · `src/engine/hub/hub_build.{hpp,cpp}`
> (`build_hub_view`) · `src/games/hub/hub_scene.{hpp,cpp}` · `src/main.cpp` (`--hub`,
> `--hub-ui`) · `tests/test_hub.cpp`

Chapter 95 built the Hub as a headless dashboard: it aggregated a project's state and
printed the next recommended action. This chapter gives it a window — but the interesting
part is what *didn't* have to change to do so. The rule this chapter demonstrates is:
**a view model is rendered, not owned by a renderer.** The same `HubView` now feeds a
terminal and a framebuffer, and neither renderer knows the other exists.

## The trap: two renderers drifting apart

The naïve way to add a graphical hub is to write a `HubScene` that loads the project,
reads the channels, formats some strings, and draws them. Do that and you now have *two*
copies of "what the hub says" — the CLI's `printf`s and the Scene's draws — and they drift
the first time you change a label or add a field. A month later the terminal says
"NOT shippable" and the window says "invalid" for the same project, and nobody knows which
is right.

The fix is to make "what the hub says" a **value**, not behaviour. `hub_lines(const
HubView&)` returns the exact list of text lines — title, status, problems, package hash,
each channel, and the `next:` recommendation — as a `std::vector<std::string>`. It is pure
and unit-tested. Then:

- the CLI (`--hub`) prints each line;
- the Scene (`--hub-ui`) draws each line.

Both call `hub_lines`. There is exactly one source of truth for the hub's content, it has
a test, and the two renderers *cannot* disagree because they render the same vector. This
is the roadmap's "Hub and CI surface identical diagnostics" requirement, satisfied not by
discipline but by construction.

## Three layers, each testable at its own level

The hub now spans three files, and the split is deliberate — each layer is testable
without the one above it:

| Layer | File | Purity | Tested by |
|---|---|---|---|
| **Decision + display** | `hub.cpp` (`recommend`, `hub_lines`) | pure | `test_hub.cpp`, no I/O |
| **Assembly** | `hub_build.cpp` (`build_hub_view`) | reads through `assets::` | `--hub` CLI smoke in CI |
| **Rendering** | `hub_scene.cpp` (`HubScene`) | SDL/framebuffer glue | manual visual accept |

The valuable, breakable logic (what to recommend, what to display) sits in the pure layer
with a fast unit test. The assembly layer is I/O but still headless — it is exercised every
CI run by `--hub`. Only the thinnest layer, "draw these strings at these y-coordinates,"
is genuinely un-unit-testable, and it is small enough to read and trust. This is the same
`*_core` discipline the whole codebase follows, applied to a UI: **push everything that can
be tested below the pixels, and keep the pixel layer trivial.**

## Why `build_hub_view` became its own library

`build_hub_view` was born in `main.cpp` (Chapter 95). To let the Scene build the view the
same way the CLI does — without copying the assembly logic into `src/games/` — it moved
into a small library, `hub_build_core`. It reads a manifest, validates it, resolves the
content closure, hashes the source, and reads each channel, filling a `HubView`.

One subtlety in its CMake: `hub_build_core` calls `assets::load_file`, `parse_project`,
`package_hash`, and friends, but it does **not** link those libraries. It only needs their
*headers* to compile; the symbols are provided by the final `demo` link, which already
links `project_core`, `resource_core`, `release_core`, `hub_core`, and `assets.cpp`
directly. This is the same pattern `renderer3d` uses to reference `Renderer2D` without
linking it — a static library defers its dependencies to whoever links it into an
executable. Stating the link dependencies here too would just make the linker warn about
duplicate archives.

## The Scene itself is almost nothing

Which is the point. `HubScene` holds a project path and an `optional<HubView>`, builds the
view in its constructor, and rebuilds it when you press **R** (so you can edit the project
or run a release verb in another terminal and refresh). Its `render` clears the frame, then
walks `hub_lines` drawing each string, tinting three cases for legibility: the `next:`
recommendation green, problems amber, a `NOT shippable` status red. That colour logic reads
the *content* of each line (`s.rfind("next:", 0) == 0`) rather than being told a type,
which keeps the Scene decoupled from `hub_lines`' internal structure — if a new line
category appears, the Scene simply draws it in the default colour until someone decides it
deserves a tint.

`--hub-ui [project]` opens it (defaulting to the reference project); `--hub` prints the
identical content headlessly. `ponytail:` the Scene is **read-only** — it shows the
recommended verb, you run it from the CLI, and press R. Interactive mutation (a key that
publishes or promotes in place) waits until the domain operations are extracted from
`main.cpp` behind a callable interface; wiring a button to a function that currently only
exists as CLI-dispatch-plus-`printf` would be the tail wagging the dog.

## Honesty about verification

The pure and assembly layers are verified automatically (unit test + CI smoke). The Scene's
*pixels* are not — this environment can't screenshot a native window, so "the hub renders
legibly" is a manual accept, exactly like the web build's in-browser render. What *is*
verified is that the binary constructs the Scene and runs its frame loop without crashing,
and that every string it can draw comes from a tested function. That is the honest state:
the hub's brain and mouth are tested; confirming its face is a five-second look for whoever
runs `./build/demo --hub-ui`.

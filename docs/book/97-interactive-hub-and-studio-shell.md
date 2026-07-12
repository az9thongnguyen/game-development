# Chapter 97 — From View to Controller: Interactive Hub and the Studio Shell

> Code: `src/engine/release/ops.{hpp,cpp}` (`OpResult`, `publish`, `promote`, `rollback`,
> `current_release`) · `src/games/hub/hub_scene.cpp` (keys) ·
> `src/games/studio_shell/studio_shell_scene.{hpp,cpp}` · `src/main.cpp` (`--shell`) ·
> `tests/test_release_ops.cpp`

Chapter 96 gave the Hub a window, but it was read-only: it *told* you to publish, and you
switched to a terminal to do it. This chapter closes that loop — you press **Space** in the
window and it publishes — and it builds the Studio shell that frames the Hub alongside other
sections. The whole chapter turns on one extraction, and it is a textbook case of *why* you
separate an operation from the way it's triggered.

## The problem: an operation trapped inside a CLI

By Chapter 94, `publish`/`promote`/`rollback` worked — but they lived in `main.cpp` as
functions that did the work *and* `printf`'d the result *and* returned a Unix exit code.
That shape is fine for a CLI and useless for anything else. A window can't consume an exit
code or a `printf`; it needs the *outcome* as data so it can draw it. As long as the
operation and its presentation were fused, the Hub could never do more than recommend.

So the operations moved into a small library, `release_ops`, with one deliberate change to
their signature: they return an **`OpResult { bool ok; std::string message; }`** instead of
printing. Presentation becomes the caller's job:

- the CLI prints `message` (to stdout on success, stderr on failure) and maps `ok` to an
  exit code — three-line wrappers in `main.cpp`;
- the Hub Scene flashes `message` on screen for a few seconds and refreshes its view.

Same operation, two presentations, zero duplicated logic. This is the "separate what you
do from how you report it" rule, and the payoff is immediate: the moment `publish` returned
a value instead of printing, it became callable from a keypress.

## The dividend: the ops are now *testable*

There is a second, quieter payoff, and it matters more than the interactivity. When the
logic lived in `main.cpp` interleaved with `printf` and `argv`, it could only be exercised
by running the binary. Extracted into `release_ops`, it can be driven directly by a test
against a temporary asset base (`tests/test_release_ops.cpp`):

```cpp
assets::set_base_path("test_ops_tmp");
CHECK(publish("p.gameproject", "development", "v1", {"fps"}).ok);
CHECK(promote("development", "preview", "share").ok);
CHECK(!rollback("../evil", id, "x").ok);              // path-traversal channel refused
CHECK(!publish("broken.gameproject", "development", "", {"fps"}).ok);  // missing asset
```

This is the honest answer to "how do you verify a UI you can't screenshot?" — you don't
verify the UI, you verify everything *under* it and keep the UI trivial. The keypress→op
wiring in the Scene is four lines a human can read; the ops those four lines call are now
covered end to end. The window's correctness reduces to "does pressing Space call
`publish`?", which is inspection, not testing.

## The Hub Scene becomes a controller

With ops that return values, `HubScene::update` is almost nothing:

```cpp
auto did = [&](const engine::OpResult& r) { flash_ = r.message; flash_t_ = 5.0; rebuild(); };
if      (in.pressed(Key::Space)) did(engine::publish(path_, "development", "hub", known_entries_));
else if (in.pressed(Key::Num1))  did(engine::promote("development", "preview", "hub"));
else if (in.pressed(Key::Num2))  did(engine::promote("preview", "production", "hub"));
else if (in.pressed(Key::R))     rebuild();
```

Press Space, it publishes and the `development` line updates and turns `==source`; press 1,
`preview` catches up; press 2, `production` does; and the `next:` line walks down to
"in sync" as you go — the pipeline from Chapter 95 made tactile. (`Space` rather than a `P`
key only because the engine's small `Key` enum, grown as scenes needed it, never added `P`;
the lazy correct move is to use a key that exists, not to widen the enum for one scene.)

## The Studio shell: a frame, not a rewrite

The Studio shell (`--shell`) is the roadmap's "shared shell and dock/navigation model." It
is a left nav rail — **Hub / Learn / About** — over a main panel, with Up/Down to switch.
The important thing is what it *reuses*: the Hub section is not a reimplementation, it draws
the same `engine::hub_lines` and calls the same `engine::release` ops as `--hub-ui`. The
Learn section is a static map from the platform to its own documentation (the guide and the
spine chapters). The About section reads the project name from the same `HubView`.

There is no new domain logic in the shell at all — it is pure frame and navigation. That is
the discipline the roadmap's own risk note demands ("UI work starts before command/resource
behavior stabilizes"): because the command/resource/release layer was hardened first
(Chapters 93–95) and the view model and ops are tested cores, the shell can be *only* a
layout, and adding a fourth section later is adding a `case`, not a subsystem.

`ponytail:` the shell has three sections and hosts the Hub — no dock manager, no thumbnails,
no folding the existing Texture/Map Labs in. Those arrive when a second author (or a second
project) actually needs them; today one reference project drives everything, so the shell
stays a thin, honest frame around the one panel that does real work.

## What is verified, and what still needs eyes

The ops are unit-tested; the view model and display lines are unit-tested; the CLI paths run
in CI. What remains a manual visual accept is the same thin layer as always: that the Hub
and Studio *windows* render legibly and that a keypress visibly does the thing. The binary
is verified to construct both scenes and run their frame loops without crashing. Both scenes
are also wired into the web build (`?mode=hubui`, `?mode=shell`), so the visual accept can be
done in a browser as easily as natively — the web VFS is read-only, so channels read "unset"
and the Hub simply recommends "publish," which is the correct rendering for that environment.

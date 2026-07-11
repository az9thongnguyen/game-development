# Chapter 93 — The Immutable Release Store and Channels

> Code: `src/engine/release/release.{hpp,cpp}` (`release_dir`, `release_manifest_path`,
> `channel_path`, `serialize_channel`/`parse_channel`, `valid_channel_name`/`valid_hash_hex`)
> · `src/main.cpp` (`--project-publish`, `--release-promote`, `--release-rollback`,
> `--release-status`) · `src/engine/assets.cpp` (`write_file` now makes parent dirs)
> · `tests/test_release.cpp`

Chapter 92 turned a resolved project into a **package manifest** — one deterministic
document plus one combined `packagehash` that names it. That hash was called the
"release-id seed." This chapter plants the seed: it stores that manifest **immutably**,
addressed by its own hash, and puts a movable pointer — a **channel** — in front of it.
Publishing, promoting, and rolling back are then nothing more exotic than *writing a
sixteen-character string into a small file*.

This is Horizon 1 of the platform strategy (`docs/strategy/`), built at learning scale:
a local content-addressed store you can inspect with `ls`, standing in for the full
vision's server-side registry. The path scheme is the same either way, so the server
version slots in behind it later without changing the mental model.

## Two ideas: an immutable object, and a movable name

Almost every release system — container registries, Nix, Git, package managers — is built
on the same two-part shape, and it is worth stating plainly because once you see it you
see it everywhere:

1. **The release is immutable and content-addressed.** Its identity *is* its content
   hash. `releases/c95febd882741b29/package.txt` holds exactly the bytes whose
   `packagehash` is `c95febd882741b29`. You can never store *different* bytes under that
   name, because the name is derived from the bytes. There is no "update a release" —
   only "publish a new one," which gets a new name.

2. **A channel is a mutable pointer at one immutable release.** `channels/preview` is a
   one-line file: `channel1 c95febd882741b29`. "Promote to production" copies that line
   to `channels/production`. "Roll back" writes an older release id back into the
   channel. The releases never move; only the *names pointing at them* move.

Separating these two is what makes rollback trivial and safe. The old release was never
deleted or mutated — rolling back is just aiming an existing pointer at an object that
was there the whole time. Compare the alternative (mutating a "current build" in place):
a rollback would have to *reconstruct* the previous bytes, and you had better hope you
kept them.

## The pure layer is just paths and a pointer format

`release.hpp` contains no I/O at all — no `assets::`, no streams. It is the *scheme*: how
a hash becomes a store path, and how a channel pointer is written and read.

```cpp
std::string release_dir(const std::string& hex)           { return "releases/" + hex; }
std::string release_manifest_path(const std::string& hex) { return release_dir(hex) + "/package.txt"; }
std::string channel_path(const std::string& name)         { return "channels/" + name; }

std::string serialize_channel(const std::string& hex) { return "channel1 " + hex + "\n"; }
std::optional<std::string> parse_channel(const std::string& text);  // "channel1 <hex>" → hex, else nullopt
```

Keeping this layer pure means the whole store scheme is unit-testable with no filesystem
and no window (`tests/test_release.cpp`), exactly like `project_core` and `resource_core`
before it. The impure half — read this file, write that one — lives in `main.cpp` and
goes through the one `assets::` seam, so the web build redirects persistence in a single
place. (Web persistence is preview-only; the store is fundamentally a native/server
concept, which is fine — the strategy files durable release hosting under the same
horizon.)

## The validators are a security boundary, not decoration

Two of the pure functions look trivial and are the most important in the file:

```cpp
bool valid_hash_hex(const std::string& hex);      // exactly 16 lowercase hex chars
bool valid_channel_name(const std::string& name); // nonempty, ≤64, [A-Za-z0-9_-] only
```

Here is why they exist. `--release-rollback <channel> <release-id>` takes **two operator
strings and turns them into filesystem paths**: `channels/<channel>` and
`releases/<release-id>/package.txt`. If a caller passed `../../etc/something` as the
channel name, an unguarded `channel_path` would happily build a path that escapes the
store. So both arguments are validated *before* any path is constructed — the channel
name may contain no `/` and no `.` (so it cannot traverse), and the release id must be
exactly the 16-hex shape a real hash has. This is the "never simplify away input
validation at trust boundaries" rule made concrete: a rollback verb that writes files
based on argv is a trust boundary, and the test suite pins the rejection cases
(`..`, `a/b`, `has space`, over-length) so a later "cleanup" can't quietly loosen them.

## Publish: idempotent by construction, and it refuses to lie

`publish_project` packages the project (Chapter 92), derives the release id, and writes
the manifest — but only if it isn't already there:

```cpp
if (auto existing = assets::load_file(mpath)) {
    if (*existing != pkg_bytes) {         // same id, different bytes → refuse
        std::fprintf(stderr, "release: %s already stored with different bytes — refusing to overwrite\n", hex);
        return 1;
    }
    verified = true;                      // byte-identical: immutable release already present
} else if (!assets::write_file(mpath, pkg_bytes)) { ... }
```

Two behaviors fall out of this and both matter:

- **Idempotence.** Publishing the same project twice is a *verified no-op* — it re-derives
  the same hash, finds the same bytes already stored, and just re-points the channel. The
  headless smoke shows this: the second publish prints `verified` instead of `published`.
- **It cannot be fooled.** If the bytes at a release id ever *disagree* with what we're
  about to publish under that id, that is either a hash collision or a corrupted store —
  and the only safe response is to refuse, never overwrite. An immutable release that
  silently changed its bytes would be a lie the whole system is built to prevent.

## Why `write_file` grew a `create_directories`

There is one small ripple into the engine seam. `releases/<hash>/package.txt` needs its
`<hash>` directory to exist, and `std::ofstream` will not create it. So `assets::write_file`
now makes the parent directory first:

```cpp
if (full.has_parent_path()) std::filesystem::create_directories(full.parent_path(), ec);
```

This is a strict improvement to the seam — every caller that writes to a nested path now
just works, and the existing callers (iso's `farm_save.txt`) are unaffected because their
parent is the already-present base directory. It works identically on native and
Emscripten's in-memory filesystem.

## The full lifecycle, headless

Everything is a CLI verb with no window, so the whole flow is scriptable and CI-checkable:

```sh
demo --project-publish projects/creator.gameproject    # → releases/<hash>/, channel preview
demo --release-status                                  # preview <hash> [present] / production unset
demo --release-promote preview production              # copy preview's pointer to production
demo --release-rollback production <older-hash>         # aim production back at a prior release
```

`--release-status` deliberately reads the two *fixed* channel files rather than scanning
the `releases/` directory. That is not laziness — it is the strategy's explicit warning
against the "collection database" smell, where a feature quietly becomes "enumerate
everything on disk." History and listing, if ever needed, will come from an append-only
log, not a directory walk that breaks the moment the store lives behind a web VFS or a
network.

## What this unlocks, and what is deliberately deferred

With this chapter the content-flow vertical is complete end to end: **create a project →
declare its content → validate the closure → fingerprint it into a package → publish it
immutably → promote and roll back by moving a pointer.** That is the whole Horizon 0→1
spine the strategy asked to build *before* adding breadth.

## Preview parity, the cheap way (metric P2)

The strategy tracks a metric called **preview parity**: is what you *previewed* byte-for-byte
what you *published*? Because a project already reduces to one package hash, answering it is
a string comparison, not a diff engine — `--project-verify <path> <channel>`:

```cpp
const std::string local = engine::hash_hex(engine::package_hash(r->resources));  // what I'd ship now
auto live = read_channel(channel);                                               // what's on the channel
if (local == *live) return 0;   // parity
return 2;                        // drift: valid inputs, but they differ
```

The exit codes are the point. `0` = parity, `2` = drift, `1` = error — three *distinct*
outcomes, so a script (or CI) can tell "the project drifted from the channel" apart from
"the command broke." A CI gate that publishes and then asserts `--project-verify ... preview`
exits `0` catches the whole class of "the thing I shipped isn't the thing I tested" bugs for
the price of one hash comparison. That the metric fell out this cheaply is the dividend of
having made the package deterministic back in Chapter 92.

## What is deliberately deferred

Still **not** built, each with its trigger:
- **A release log / history listing** — add when a UI or audit genuinely needs to
  enumerate past releases; it will be an append-only file, never a directory scan.
- **Server-side hosting** — the same path scheme behind an HTTP/object store, which is
  the ops-heavy part the strategy keeps *conditional on evidence*, not scheduled.

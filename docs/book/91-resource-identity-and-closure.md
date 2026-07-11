# Chapter 91 — Resource Identity, Content Hashing, and Dependency Closure

> Code: `src/engine/resource/resource.{hpp,cpp}` · `src/engine/project/project.{hpp,cpp}`
> (the `asset` field) · `src/main.cpp` (`launch_project` closure check) ·
> `tests/test_resource.cpp` · `assets/projects/creator.gameproject`

Chapter 90 gave a project a *name* and an *entry*. This chapter gives it *content it is
accountable for*. A project that says "I am the Creator game" but silently ships without
its level or wall textures is not a project — it is a landmine that detonates at runtime
in front of a player. So we make two things true: a project **declares** what it depends
on, and the launcher **refuses to run** if any of it is missing. Along the way we build
the smallest piece of resource *identity*: a content hash.

This is priority #2 in the strategy ("resource identity, schema versioning, and packaged
dependency graph") — reduced, as the blend posture demands, to exactly the slice the
first golden path can use today.

## Why a content hash, and why now

The target architecture repeats one phrase: *content hash*. A release records the hash of
every resource so "what did we publish?" has a byte-exact answer; a preview is compared to
the published package by hash, not by hope (metric P2, "preview parity"); a derived asset
is cached by the hash of its inputs. None of that machinery exists yet — but all of it
needs the same primitive, so we build the primitive first and let the machinery arrive
later. That is the difference between a hash *function* (one small pure thing) and a hash
*framework* (which we refuse to build until a second consumer proves its shape).

### FNV-1a, by hand

```cpp
uint64_t content_hash(const std::vector<uint8_t>& bytes) {
    uint64_t h = 14695981039346656037ULL;   // FNV-1a 64-bit offset basis
    for (uint8_t b : bytes) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ULL;               // FNV-1a 64-bit prime
    }
    return h;
}
```

FNV-1a is nine lines because it is a *non-cryptographic* hash: deterministic, fast,
identical on native and WASM, ideal for fingerprinting content and detecting drift. It is
**not** collision-resistant against an adversary, and the header says so — the day we need
tamper-evidence (signed releases), a stronger hash slots in behind the same signature
without touching a caller. Choosing the flimsy-but-correct tool *and naming its ceiling*
is the whole discipline.

### The bug the test caught

The offset basis is a 20-digit constant, `14695981039346656037`. The first draft dropped
two digits. Both the source and the "empty input hashes to the basis" test used the same
wrong number, so *that* check passed — a self-consistent lie. What failed was the
independent cross-check: `hash_hex(basis) == "cbf29ce484222325"`. The hex of the wrong
number is not the hex of the right one, and the test went red.

The lesson is general: a known-answer test on a math/security primitive must anchor to a
value derived *independently* of the code under test — a published constant, a
hand-computed vector — not to the code's own output. A test that hashes with the same
function it is testing and compares the result to itself proves only that the function is
deterministic, never that it is *correct*. This is why the chapter's primitive ships with
a known-vector test and not just a round-trip.

## Declaring content in the manifest

The manifest grows a repeatable, optional line — additive, so old manifests and old builds
are unaffected (Chapter 90's forward-compat rule earning its keep):

```text
gameproject1
name Creator Demo (FPS + Labs)
schema 1
entry fps
asset map maps/level_00.map
asset texture textures/wall_1.hrt
asset texture textures/wall_2.hrt
asset texture textures/wall_3.hrt
```

Each `asset <type> <path>` becomes an `AssetRef{type, path}` in `Project.assets`. The
parser records only well-formed declarations and `to_text` emits them in canonical order
after the identity fields, so a tool can rewrite the file losslessly.

## Dependency closure: resolve, hash, or refuse

`launch_project` now walks the declared assets through the `assets::` seam:

```cpp
for (const auto& a : proj->assets) {
    if (auto ab = assets::load_file(a.path))
        resolved.push_back({a, hash_hex(content_hash(bytes_of(*ab)))});
    else
        missing.push_back(a.path);
}
```

Two consumers of the result:

- **`--project-inspect`** prints every asset with its content hash, or `MISSING`:

  ```
  asset  map      maps/level_00.map    [d3536514de25ca3c]
  asset  texture  textures/wall_1.hrt  [25827ac49ca0c51a]
  status OK
  ```

- **`--project`** treats a missing dependency as a **hard reject** and refuses to open a
  window (metric C3, "package dependency completeness": an unresolved dependency is a hard
  rejection, not a warning). A broken project fails *before* the player sees a
  half-rendered world, with an actionable message naming the missing path.

Notice the division of labor, unchanged from Chapter 90: the *pure* cores
(`project_core`, `resource_core`) know nothing about the filesystem — they parse, validate,
and hash bytes. The *impure* seam (`main.cpp`) is the only place that touches `assets::`.
Closure is a property of the project-in-its-environment, so it lives at the environment
boundary; identity and hashing are properties of bytes, so they live in headless cores that
a future Hub or CI can reuse without a window.

## What we did not build

No resource *ids* beyond the path yet (the path is the identity for now), no dependency
*graph* (assets don't reference each other yet), no *migration* (there is only schema 1),
no *cache keyed by input hash* (nothing derives assets through the manifest yet). Each is
named in the architecture; each waits for its first real consumer. The hash we built today
is the seed all of them share, which is precisely why it was worth building alone.

## Exit criterion

`./build/demo --project-inspect projects/creator.gameproject` reports the reference game's
four declared assets with content hashes and `status OK`; a manifest naming a missing asset
fails closed (exit 1, no window) from both `--project-inspect` and `--project`. The suite is
49/49 green. The project now knows what it is *made of*, and the release pipeline has its
fingerprint primitive.

## Exercises

1. Add a `--project-hash` verb that prints a single combined hash of the whole closure
   (hash of the sorted list of per-asset hashes). That is the seed of a package id — what
   property must the combination have so that reordering `asset` lines does not change it?
2. Make `content_hash` stream from a file through `assets::` without loading the whole file
   into memory. Where does the chunk boundary risk changing the hash, and why doesn't it
   for FNV-1a?
3. Declare an asset that exists but is empty. What hash do you get, and why is "empty file"
   distinguishable from "missing file" in the inspect output? Should it be?

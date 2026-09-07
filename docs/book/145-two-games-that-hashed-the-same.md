# 145 — Two games that hashed the same

A slice with two small items on it: let `--bench-ui` measure a game, and give the iso
sim and the colony manifests. Both were bookkeeping. Both turned into something else
about four minutes in.

---

## The benchmark that could not be asked a question

`--bench-ui` has existed since chapter 108 and lived entirely inside `src/main.cpp`: the
warm-up count, the percentile arithmetic, the eight-millisecond verdict and the printing,
sixty lines in one block behind a flag.

That is the one shape this repo forbids everywhere else. `CLAUDE.md` states it twice:
*the operation lives in a pure `*_core` lib and the trigger (CLI flag or keypress) only
calls it*. The benchmark was the exception, and the cost of the exception was exactly
what the rule predicts.

```sh
$ ./build/demo --bench-ui 0
[1]    12873 segmentation fault
```

`ms[ms.size() / 2]` on an empty vector. And `std::atoi` turns any typo into `0`, so
`--bench-ui twenty` did the same thing. Nobody had ever found it, because asking the
percentile code a question meant allocating a 14 MB framebuffer and rendering the Studio.

The percentile was wrong in a second way that no crash announces:

```cpp
const double p95 = ms[static_cast<std::size_t>(ms.size() * 95 / 100)];
```

That is integer arithmetic on the **index**. For 120 samples it asks for index 114, which
is about right. For ten samples it asks for index 9 — the **worst frame of the run**,
reported as its 95th percentile. An index formula that agrees with the correct one on the
sizes you happen to try.

Split into `bench_core`, in two pieces, because the two fail differently: `summarize` is
arithmetic that can be wrong in a way no timing test would ever catch, and `run` is a loop
whose only honest claim is that it produced the number of samples it was asked for. An
empty run is now a `Summary` of zeros with `frames == 0`, which is the *answer* to "you
asked for no frames" rather than a crash or a lie.

### Four survivors that were four redundant lines

`percentile` was first written with early-outs *and* clamps:

```cpp
if (p <= 0.0) return sorted_ms.front();
if (p >= 1.0) return sorted_ms.back();
auto rank = static_cast<std::size_t>(std::ceil(p * n));
if (rank < 1) rank = 1;
if (rank > sorted_ms.size()) rank = sorted_ms.size();
```

Four mutations survived: removing any one of those four lines changed no answer anywhere.
The reason is visible once it is stated — **the early-outs and the clamps cover each other
exactly**. `p <= 0` produces a rank of 0, which the low clamp raises to 1, which is
`front()`. `p >= 1` produces a rank of `n`, which is `back()`.

The temptation is to write tests that pin all four. That would be pinning a coincidence.
Four survivors were not four missing tests, they were **four redundant lines**. The
early-outs are gone; the clamps stay, because an out-of-range `p` is what they are for,
and the rank is now computed **signed** so a negative one cannot wrap into a very large
index. Two assertions — `percentile(v, -1.0)` and `percentile(v, 2.0)` — are what makes
the clamps load-bearing, and both mutations then die.

One equivalent mutant remains, recorded rather than papered over: computing `n` as
`size_t` instead of `long long` changes nothing reachable, because the low clamp runs
before the high comparison.

### And the numbers nobody had

`PROJECT-BRIEF` has carried this warning since chapter 117: *"`--bench-ui` measures the
Studio with **no game running**."* Both games that draw a full on-screen control pad every
frame at `ss=2` arrived after it was written. `--bench-ui [frames] [all|<entry>|<manifest>]`
now walks `entries()` — the one table, not a second list — and benches each game at the
size its manifest actually launches it at:

```
bench-ui  200 frames per configuration (after 20 warm-up), RENDER only
          build: Release
  studio       1280 x 720  ss=1  ( 1280 x 720   px)  median   0.94 ms  p95   3.99 ms
  studio       1280 x 720  ss=2  ( 2560 x 1440  px)  median   5.63 ms  p95  16.02 ms
  fps           640 x 400  ss=1  (  640 x 400   px)  median   0.94 ms  p95   1.70 ms
  farm          640 x 360  ss=2  ( 1280 x 720   px)  median   2.20 ms  p95   3.10 ms
  creatures     640 x 360  ss=2  ( 1280 x 720   px)  median   3.27 ms  p95   4.09 ms
```

Both games are comfortable. That is a good answer, and it is the first time it has been an
answer at all rather than an assumption.

---

## Two labs that were games

`--lab` is for a demo of an engine subsystem, or a scene that has not earned a manifest.
`iso` — an isometric farm with a build/bulldoze loop and a save file — and `colony` —
agents, jobs, and the project's own BaaS client — are games. They had been in the lab
table since chapter 120 folded twelve flags into one door, which meant neither had ever
been through `inspect`, `package`, `publish` or the hub, and neither appeared on the page
you send someone.

Two manifests, two lines moved from `labs()` to `entries()`. `--lab iso` now answers with
where it went rather than "unknown lab", because it worked for a hundred chapters and
"unknown" reads as "deleted".

Then:

```
$ ./build/demo --project-publish projects/iso.gameproject    development "..."
published Iso Farm → development cbf29ce484222325
$ ./build/demo --project-publish projects/colony.gameproject development "..."
cbf29ce484222325 already stored with different bytes — refusing
```

## The release id did not name the release

Two different games, the same id. And `cbf29ce484222325` is not a coincidence — it is the
FNV-1a 64 offset basis, the hash of **nothing**:

```cpp
uint64_t package_hash(std::vector<PackagedResource> resources) {
    sort_by_path(resources);
    std::string canon;
    for (const auto& r : resources) canon += r.path + " " + hash_hex(r.hash) + "\n";
    return content_hash(...);
}
```

The fingerprint covered the resources. It did not cover `project`, `schema` or `entry` —
which `build_package` writes into the file, three lines above the hash of it.

The empty-closure case is the loud version. The general one is worse and quieter: **two
projects sharing their art and differing only in which scene they launch were one
release.** A `Farm` and a `Farm (creature mode)` over the same tiles would have collided
silently, and whichever was published second would have been refused — or, if the bytes
had happened to match, promoted to production as the other one.

The store's rule is that identical bytes are a verified no-op and different bytes under an
existing id are refused. That rule cannot be kept unless **the id covers everything
`package.txt` says.** So it does now, and by construction rather than by care:

```cpp
std::string build_package(...) {
    sort_by_path(resources);
    const std::string body = canonical_body(name, schema, entry, resources);
    const uint64_t    pkg  = content_hash(bytes_of(body));
    return body + "packagehash " + hash_hex(pkg) + "\n";
}
```

One `canonical_body`, written once, used by both the hash and the file. Strip the last
line of any `package.txt`, hash what is left, and the id comes back — which is now a test.
There is no second spelling of "canonical" that could drift from the first.

Every release id moves once. `assets/releases/` and `assets/channels/` are this machine's
state and gitignored, so the migration is: re-publish, re-bake `collection.json`. The cost
is small precisely because chapter 93 made the store content-addressed and disposable.

### The test had encoded the bug as its expected value

```cpp
// A shippable project with no assets (package hash = the empty/FNV-basis hash).
CHECK(dev.has_value() && *dev == "cbf29ce484222325");   // empty package → FNV offset basis
```

`test_release_ops` had asserted the literal, with a comment explaining it. It was not a
weak test — it was a test of the wrong thing, written by somebody who had reasoned it out
and reasoned out the wrong invariant. It now asserts that the id is *not* the empty-input
hash, and publishes a second asset-less project to check the two differ.

---

## A blank card reads as broken

Manifests put both games on the collection page, and the page's check failed:

```
FAIL  card 0 has no decoded cover — no cover
```

That assertion is right, and `collection.html` says why in its own comment: *"a silently
blank panel is indistinguishable from a game that has no art."* A storefront where two of
five cards are empty looks broken, not honest.

So they needed covers, and a cover is a `.hrt`, which means it comes through one of the
**four doors**. It came through `asset.pixels` — the door for art we drew — and the iso
cover is laid out by the game's *own* projection: a tile at `(col, row)` sits at
`x = 32 + (col-row)*9, y = 20 + (col+row)*5`, the same diamond `iso_render` walks. The
cover is the game's geometry rather than a picture of it.

The tempting alternative was to render a frame of the running game and save it — which
`bench_core`, freshly written two commits earlier, makes about six lines of work. That
would be a **fifth door** into `.hrt`, with a new origin the provenance scanner would have
to learn. Chapter 135 had to argue that a fourth door answers a question none of the other
three can. A fifth door might well clear that bar — *what does this game look like* is a
real question — but it clears it in a chapter of its own, not as a side effect of needing
two thumbnails.

---

## What is verified, and what is not

Verified:

- **94/94 `ctest`, twice in a row.** `test_bench` is new.
- **23 single-token mutations, 22 killed, 1 equivalent** (recorded above), plus four
  survivors resolved by deleting redundant code rather than by adding tests. The list
  covers every percentile edge, both clamps, the warm-up not being timed, each identity
  field's presence in the package file, the id ignoring identity again, and `inspect`
  reporting an id `build_package` would not write.
- **The page, driven by Chrome:** five games listed, every cover decoded, five cards
  playable, and tapping Play landed in a running **colony** — a game that could not be
  launched from a manifest at all yesterday.
- **Looked at:** both covers as PNGs, and the rendered Colony card on the page.
- Golden path (publish → verify → promote → status → hub), 0 `.tmp` leaks; Emscripten
  build; the provenance ledger re-baked to 48 assets with the prose written by hand.

Not verified:

- **`--bench-ui` measures RENDER only** — no `update()`. For the Studio that is nearly the
  whole frame; for a game it leaves out the simulation, and the farm's day loop and the
  colony's job queue are not in these numbers. The flag is named `--bench-ui` and the
  question it answers is "does the rasterization fit", but a reader could take the number
  for the frame cost and be wrong.
- **The p95 column is not dependable on this laptop** — 16 ms for a 5.6 ms median says so.
  Medians and ratios are the usable part, which has been true since chapter 117.
- **Neither new game has been played from its manifest in a native window.** `--project
  projects/iso.gameproject` launches the same scene `--lab iso` did, through the same
  table, but the only thing that has actually run them since the move is the web build and
  the bench.
- **The iso sim's save is `farm_save.txt` at the asset root** — not under `saves/`, not
  namespaced by project, and it collides with nothing today only because nothing else uses
  that name. Being a game with a manifest is what makes that a problem worth fixing.
- **`colony` still writes `colony_agent.hrt` at runtime** if it is missing — a `.hrt` that
  is gitignored, generated by C++ rather than by any of the four doors, and therefore
  invisible to the provenance ledger. The manifest does not declare it. That is the next
  thing this game will make somebody deal with.

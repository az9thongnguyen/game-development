# Chapter 90 — The Project Manifest and the Golden Path

> Code: `src/engine/project/project.{hpp,cpp}` · `src/main.cpp` (`launch_entry`,
> `launch_project`, `--project`, `--project-inspect`) · `tests/test_project.cpp` ·
> `assets/projects/creator.gameproject`

This is the first chapter of a new arc. Chapters 1–89 built *subsystems* — a renderer,
an ECS, a raycaster, a Studio, a backend. Every one of them is reached the same way: a
hard-coded flag in `main.cpp` (`--fps`, `--iso`, `--studio`, …) constructs a specific
scene. That was exactly right for learning one mechanism at a time. It is exactly wrong
for a *product*, where the thing you ship is not "a scene the source selects" but "a
project a file describes."

This chapter builds the smallest piece of the bridge: a **`game.project` manifest** and
a CLI that launches a game *from that manifest*. It is slice 1 of Horizon 0 in
[`docs/strategy/`](../strategy/04-roadmap-2026-2029.md) — the platform spine — and it is
deliberately tiny. The lesson is as much about *what we refused to build* as about what
we built.

## Why a manifest is the #1 priority

The gap analysis is blunt: the repository's weakness is "no project model or single
supported workflow joining the layers." Twelve subsystems, zero projects. You cannot
package what has no identity, cannot version what has no schema, cannot publish or roll
back what has no name. **The manifest is the root of every downstream product action**,
so it comes before all of them — before the resource registry, before packaging, before
the Hub. Build the contract first; hang the machinery off it later.

## The format

A manifest is text, in the same hand-parsed style as `fpsmap1` (Chapter 76) and the
Studio recipe sidecars — no new dependency, no JSON/TOML library, because SDL2 remains
the only runtime dependency and a four-field file does not justify a parser.

```text
gameproject1                    # magic — identifies the format and its generation
name Creator Demo (FPS + Labs)  # display name (required)
schema 1                        # manifest schema version (required)
entry fps                       # entry scene id (required, must be a known entry)
```

Four rules make this future-proof without over-building it:

1. **Magic first.** The first non-blank token must be `gameproject1`. Anything else is
   not our format and parsing fails closed. The magic carries a generation number so a
   future incompatible format (`gameproject2`) is unmistakable.
2. **Schema is a version, not decoration.** `kProjectSchema` is the highest schema this
   build understands. A manifest with a *newer* schema is rejected by `validate` with an
   actionable message ("unsupported schema version 2 (this build supports up to 1)").
   This is how "unknown required version fails" becomes a line of code rather than a
   slogan.
3. **Unknown keys are ignored.** When a later slice adds `assets content/` or
   `baas http://…`, old manifests still parse and new manifests still open on an old
   build *as long as the change is additive*. Backward compatibility is the default, not
   an afterthought. `test_forward_compat` locks this.
4. **Required fields are validated, not assumed.** Missing `name` or `entry` is a
   reported error, never a silent empty string that launches something surprising.

## The core: parse, validate, emit

`project_core` is pure and headless — no SDL, no `assets::`, no window — so it is
unit-tested in milliseconds and can be reused by a future Hub or CI without dragging in
the platform layer. It exposes exactly three functions:

- `parse_project(text) -> optional<Project>` turns text into a struct. It returns
  `nullopt` **only** for a syntactic failure that makes the file unusable: a wrong or
  absent magic, or a `schema` value that is not an integer. Everything else — missing
  fields, unknown entry — parses successfully and is caught by `validate`. This split
  matters: *parsing* answers "is this our format?", *validation* answers "is this a
  usable project for this build?". Conflating them produces error messages that can't
  tell a typo from an incompatibility.

- `to_text(Project)` emits the canonical form. `to_text(parse(x))` round-trips, which is
  what makes a manifest safe for a tool to rewrite (a future "Studio → save project"
  cannot silently reorder or drop fields it understands).

- `validate(Project, known_entries)` returns one human-readable string per problem, or
  an empty vector for a valid project. Crucially, **the set of launchable entry ids is
  passed in**, not hard-coded. The core knows nothing about scenes, SDL, or `main.cpp`;
  it only knows "the caller said these entries exist, and this manifest names one that
  isn't among them." That keeps the pure core pure.

```cpp
// the whole contract, headless and testable:
std::optional<Project> parse_project(const std::string& text);
std::string            to_text(const Project& p);
std::vector<std::string> validate(const Project& p,
                                  const std::vector<std::string>& known_entries);
```

## The seam: one launch path

In `main.cpp`, a single function owns "turn an entry id into a running game":

```cpp
int launch_entry(const std::string& entry) {
    if (entry == "fps") { /* Config + run_window(make_unique<RaycastScene>()) */ }
    std::fprintf(stderr, "unknown entry scene: %s\n", entry.c_str());
    return 1;
}
```

Both `--fps` and `--project` now route through `launch_entry`, so the flag and the
manifest cannot drift apart — there is exactly one place that knows how the FPS game
starts. `kKnownEntries = {"fps"}` is the list handed to `validate`, kept next to
`launch_entry` so the two stay in sync.

This is the payoff for the golden path: **adding a new project needs no code change** —
just a new `.gameproject` file whose `entry` names an existing scene. Adding a new
*entry type* (a genuinely new game) is a small, localized edit to `launch_entry` and
`kKnownEntries`. That is the right boundary: data selects among capabilities; code adds
capabilities.

## Two verbs: launch and inspect

`launch_project(path, inspect_only)` loads the manifest through `assets::load_file` (so
the identical asset-relative path works native and in the browser VFS), parses,
validates, and then either:

- **`--project <path>`** — refuses to launch if validation fails (printing every
  problem), otherwise calls `launch_entry`. A broken project never opens a window in a
  confusing half-state.
- **`--project-inspect <path>`** — the read-only *doctor*. It prints the identity and a
  status line, then exits `0` (OK) or `1` (problems), touching no window. The roadmap
  insists on read-only inspect/doctor *before* any automation mutates projects; this is
  it, and it is what makes the golden path testable in CI with no display:

```
$ ./build/demo --project-inspect projects/creator.gameproject
project: projects/creator.gameproject
  name   Creator Demo (FPS + Labs)
  schema 1
  entry  fps
  status OK
```

An invalid manifest reports actionably and fails:

```
  status 2 problem(s):
    - project name is required
    - unknown entry scene 'chess' (known: fps)
```

## What we refused to build

The architecture's manifest contract lists eight sections (identity, compatibility,
runtime, resources, build profiles, online services, telemetry, distribution). We built
three fields. That is not laziness for its own sake — it is the roadmap's own warning
made concrete: *"overdesigning the manifest before proving one project"* is listed as
Horizon 0's main risk. A resource graph with no packager, build profiles with no build
pipeline, a backend endpoint with no SDK wiring — each would be a schema we'd have to
migrate before anything consumed it. We add a field the day a consumer needs it, and the
"ignore unknown keys" rule means that day costs nothing today.

## Exit criterion

A clean checkout runs the reference game with `./build/demo --project
projects/creator.gameproject` — no `src/main.cpp` edit, no `web/shell.html` edit to pick
the scene. `test_project` locks the core contract; the full CTest suite stays green
(48/48). The pile of `--flag` scenes now has its first *project*, and every later slice —
resource IDs, packaged preview, immutable releases — has a root to hang from.

## The create verb (closing the loop's front)

Once the *rest* of the golden path existed — inspect, package, publish, verify, run — one
verb was still missing at the front: **create**. The strategy's canonical loop starts
"new project → create → …", and until you could make a project without hand-typing the
manifest format, the loop had no first-class entry. `--project-new <out-path> <entry>
[name]` fills it, and it is almost nothing because the pure core already had the pieces:

```cpp
engine::Project p{name, engine::kProjectSchema, entry, /*assets*/{}};
if (!engine::validate(p, kKnownEntries).empty()) return 1;   // never scaffold an unlaunchable project
if (assets::load_file(out_path)) return 1;                   // create, don't clobber
assets::write_file(out_path, to_text(p));                    // reuse the round-trip serializer
```

Two guards make it trustworthy rather than a convenience: it **validates before writing**
(a scaffold that can't launch is a bug handed to a beginner, not a starting point — so an
unknown `entry` is refused with the known list), and it **refuses to overwrite** an
existing file (creating is not the same as clobbering). That it reuses `to_text` — the
same serializer `test_project` already round-trip-tests — means the file it writes is
guaranteed to parse back to the same project. The scaffolded manifest immediately runs the
whole downstream loop: `--project-inspect` reports `status OK`, `--project-publish` stores
it, `--project-verify` reports parity. `ponytail:` this is the headless stand-in for the
real create experience — a Studio shell — which the roadmap files under a later horizon;
the CLI verb closes the loop today without pretending to be that UI.

## Exercises

1. Add an optional `assets <dir>` field. Parse and round-trip it, but do **not** validate
   it yet (no consumer). Confirm old manifests and old builds still work — that's the
   forward-compat rule earning its keep.
2. Register a second entry (`entry studio`) in `launch_entry`/`kKnownEntries` and write a
   `studio.gameproject`. Notice the diff: two lines of code, one new file.
3. Make `--project-inspect` print the *guidebook chapter* for each diagnostic class
   (the architecture wants every error to link to its explanation). Where would that
   mapping live so the pure core stays scene-free?

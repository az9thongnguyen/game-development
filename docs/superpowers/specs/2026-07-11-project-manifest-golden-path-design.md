# Project Manifest + Golden Path (Horizon 0, slice 1) — Design Spec

**Date:** 2026-07-11
**Status:** Built
**Strategy link:** [`docs/strategy/04-roadmap-2026-2029.md`](../../strategy/04-roadmap-2026-2029.md)
Horizon 0 ("make it one product"), adopted _blend_ posture.

## Decision

Introduce the first piece of the platform spine: a versioned `game.project` manifest
and a CLI seam that launches a reference game **from that manifest** instead of a
hard-coded scene flag in `src/main.cpp`. This is the smallest slice that starts the
canonical loop's `new project → … → test/run` steps and lets the accumulated `--flag`
scenes begin to cohere into *one project*.

## Why this slice first

Per the gap analysis ([`02 §10`](../../strategy/02-current-state-and-gap-analysis.md)),
the repo's real weakness is not missing subsystems but the absence of a project model:
every game is a separate `--flag`. The manifest is the ranked #1 priority ("canonical
project manifest and golden-path CLI — the contract every UI, test, build, and release
action can share"). Everything downstream (resource IDs, packaging, release channels,
Hub) depends on it, so it goes first.

## Scope (deliberately minimal — ponytail)

A manifest names only what the *first* golden path needs: identity + entry scene.

```text
gameproject1          # magic
name Creator Demo      # display name (required)
schema 1               # manifest schema version (required, <= build's kProjectSchema)
entry fps              # entry scene id (required, must be a known entry)
```

Unknown keys are **ignored** so later additive fields (assets, build profiles, backend
endpoint, telemetry) stay backward-compatible without a schema bump. Non-additive
changes bump `kProjectSchema`; a manifest newer than the build fails validation with an
actionable message. This directly implements the architecture's manifest rules
(versioned, migratable, unknown-required-version fails, GUI and CLI read one model).

## Components

- **`engine::project` core** (`src/engine/project/project.{hpp,cpp}`, lib `project_core`):
  pure, headless, SDL-free. `parse_project(text) -> optional<Project>` (fails closed on
  bad magic / malformed schema), `to_text(Project)` (canonical round-trip), and
  `validate(Project, known_entries) -> vector<string>` (semantic checks; the known-entry
  set is injected so the core stays free of scene/SDL knowledge).
- **`launch_entry(id)`** in `main.cpp`: single source of truth mapping an entry id to its
  window config + scene factory. Both `--fps` and `--project` route through it, so a
  manifest selects a game with no dispatch edit.
- **CLI verbs:** `--project <path>` (validate then launch) and `--project-inspect <path>`
  (headless: print identity + validation report, exit 0/1, no window — the read-only
  `inspect/doctor` the roadmap asks for before automation mutates anything).
- **Sample manifest:** `assets/projects/creator.gameproject` (`entry fps`), loaded through
  the `assets::` seam so the same path works native and in the web VFS.

## Data flow

`assets::load_file(path)` → `parse_project` → `validate(known_entries)` →
(inspect: print report / launch: `launch_entry(entry)` → `run_window`). File I/O stays
behind `assets::`; the core touches no filesystem.

## Error behavior (fail closed)

- Unreadable path → stderr + exit 1.
- Bad magic / malformed schema → "not a valid gameproject1 manifest", exit 1.
- Semantic problems (missing name, missing/unknown entry, unsupported schema) → one
  actionable line each; `--project` refuses to launch, `--project-inspect` reports and
  exits 1. A valid manifest with a known entry launches (or inspects OK, exit 0).

## Testing

`tests/test_project.cpp` (CTest `project`, dependency-free): parse + canonical
round-trip, fail-closed (bad magic, empty, malformed schema), forward-compat (unknown
additive keys), and validate (valid / unknown entry / missing name / missing entry /
schema-too-new / schema-unset). End-to-end golden path proven headlessly via
`--project-inspect`.

## Non-goals (this slice)

- No resource IDs, dependency graph, packaging, or build profiles (later Horizon 0/1).
- No Hub/GUI. No workspace scan or multi-project registry.
- No backend endpoint or secrets in the manifest.
- Manifest lives under `assets/` for now (respects the VFS seam); real workspace paths
  are a later slice.

## Exit criterion (met)

A clean checkout runs the reference game via `./build/demo --project
projects/creator.gameproject` without editing `src/main.cpp`, and can validate any
manifest headlessly with `--project-inspect`. Full suite 48/48 green.

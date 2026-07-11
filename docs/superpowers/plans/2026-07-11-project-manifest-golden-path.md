# Project Manifest + Golden Path — Implementation Plan (Horizon 0, slice 1)

**Goal:** Launch a reference game from a versioned `game.project` manifest instead of a
hard-coded scene flag, with a headless inspect/validate path.

**Architecture:** Pure `project_core` (parse/validate/emit) + a shared `launch_entry`
seam in `main.cpp` reached by both `--fps` and `--project`. File I/O via `assets::`.

**Tech:** C++20, hand-parsed text format (no new deps), CTest.

---

### Task 1: `project_core` — parse/validate/emit  ✅

- Create `src/engine/project/project.{hpp,cpp}`: `Project{name, schema, entry}`,
  `parse_project` (fail closed on bad magic / malformed schema; ignore unknown keys),
  `to_text` (canonical round-trip), `validate(p, known_entries)` (schema range, required
  name, required + known entry).
- `tests/test_project.cpp`: round-trip, fail-closed, forward-compat, validate cases.
- CMake: `project_core` STATIC lib + `test_project` target + `add_test(NAME project)`.
- Verify: `ctest -R project` → PASS.

### Task 2: CLI seam + golden path in `main.cpp`  ✅

- Add `launch_entry(id)` (config + factory for `fps`), `kKnownEntries`, and
  `launch_project(path, inspect_only)` (load via `assets::`, parse, validate, then
  inspect-print or launch).
- Refactor `--fps` to `return launch_entry("fps")` (one source of truth).
- Add `--project <path>` and `--project-inspect <path>` dispatch.
- Link `project_core` into `demo`.
- Verify: build; `--project-inspect projects/creator.gameproject` prints OK/exit 0;
  invalid manifest → diagnostics/exit 1; missing file → exit 1.

### Task 3: Sample manifest + docs  ✅

- `assets/projects/creator.gameproject` (`entry fps`).
- Guidebook ch.90; README usage row.
- Verify: full `ctest` green (48/48), no regressions.

### Task 4: Commit / merge / checkpoint  ✅

- Commit on `feat/project-manifest`; merge `--no-ff` to `main`; push; memory checkpoint.

## Self-review checklist

- [x] No new runtime dependency (hand-parsed text; SDL-free core).
- [x] File I/O only through `assets::`.
- [x] Fails closed on malformed input; diagnostics are actionable.
- [x] `--fps` and `--project` share `launch_entry` (no duplicated launch logic).
- [x] Adding a new project needs no `main.cpp` edit (only a new manifest); adding a new
      *entry type* is an expected, localized code change.

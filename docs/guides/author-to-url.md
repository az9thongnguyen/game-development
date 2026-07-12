# Author to URL — the Horizon 1 golden path, and a failure lab

This is the operator's guide to the platform spine: how one person takes a game from
*nothing* to a *promoted, rollback-able release*, entirely through headless commands, plus
a **failure lab** that shows what every wrong turn looks like and why the system responds
the way it does. Everything here is real — the commands run as written against a clean
build, and CI exercises the same flow.

> Build first: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`.
> All paths are resolved under `assets/` (the one I/O seam). The release store lands in
> `assets/releases/` and `assets/channels/` (both gitignored — they're runtime state).

## The whole loop in seven commands

```sh
# 1. CREATE — scaffold a new, valid, launchable project (no hand-typing the format)
./build/demo --project-new projects/mine.gameproject fps "My Game"

# 2. TEST/DOCTOR — validate the manifest + resolve its content closure, headless
./build/demo --project-inspect mine.gameproject         # 'status OK' or the exact problems

# 3. HUB — one glance: where is this, and what should I do next?
./build/demo --hub mine.gameproject                     # ends in a 'next' recommendation

# 4. PUBLISH — store the release immutably (atomic) + point the development channel + audit
./build/demo --project-publish mine.gameproject development "first cut"

# 5. VERIFY — preview parity (P2): is what I'd ship identical to what's on the channel?
./build/demo --project-verify mine.gameproject development   # exit 0 = match

# 6. PROMOTE — move the same immutable release up the pipeline (a pointer write, not a rebuild)
./build/demo --release-promote development preview "looks good"
./build/demo --release-promote preview production "ship it"

# 7. OPERATE — status, audit history, and rollback if a release goes bad
./build/demo --release-status                            # what dev/preview/prod point at
./build/demo --release-log                              # append-only history, with reasons
./build/demo --release-rollback production <prior-id> "revert"   # aim a channel back
```

The Hub (step 3) is the shortcut: at any point, `--hub <project>` tells you which of these
steps is the right next one, so you never have to remember the pipeline yourself. It walks
**fix → publish → promote-to-preview → promote-to-production → in-sync** and names the first
step that isn't done.

## Why the loop has the shape it does

- **Create validates before writing.** A scaffold that can't launch is a bug handed to a
  beginner, so `--project-new` refuses an unknown entry and won't clobber an existing file.
- **Publish is content-addressed and immutable.** The release id *is* the package hash;
  the same content always lands at the same path, and the store refuses to overwrite a
  release id with different bytes. Re-publishing identical content is a *verified* no-op.
- **Promotion never repackages.** It copies a pointer, so the bytes you tested in
  `development` are bit-identical to what goes live — that's what makes "author to share in
  under 15 minutes" achievable and what makes preview parity (P2) meaningful.
- **Every move is atomic and audited.** Writes stage a `.tmp` then rename (no torn
  releases); every publish/promote/rollback appends to `releases/audit.log` with the
  predecessor it displaced — so a bad release always has a known-good id to roll back to,
  and the prior release is still in the store because releases never mutate.

## The failure lab

Each entry is a real failure you can reproduce, the command that triggers it, what the
system does, and *why*. Exit codes matter: a script or CI step can tell these apart.

### 1. A declared asset is missing
```sh
printf 'gameproject1\nname Broken\nschema 1\nentry fps\nasset map no/such.map\n' > assets/broken.gameproject
./build/demo --project-inspect broken.gameproject   # → status: 1 problem, 'missing asset: no/such.map'; exit 1
./build/demo --project broken.gameproject           # → refuses to LAUNCH; exit 1
```
**Why:** dependency closure is enforced at the boundary. A project that references content
it can't resolve is not shippable, and the launcher won't start a game that would fail to
load mid-frame. The Hub reports the same as `NOT shippable` and recommends `fix: …`.

### 2. The manifest itself is malformed / from a newer schema
```sh
printf 'not-a-gameproject\n' > assets/bad.gameproject
./build/demo --project-inspect bad.gameproject      # → 'is not a valid gameproject1 manifest'; exit 1
```
**Why:** the parser fails *closed* on a bad magic line, and `validate` rejects a `schema`
newer than the build understands (forward-compat: unknown *keys* are ignored, but an
unknown *version* is refused with an actionable message rather than silently misread).

### 3. Publishing bytes that collide with an existing release id
```sh
# (Only reachable via store corruption or a hash collision.) The store refuses:
# '<id> already stored with different bytes — refusing'; exit 1
```
**Why:** an immutable release's bytes must never change. If what's on disk under a release
id disagrees with what you're about to publish there, the only safe response is to refuse —
overwriting would make the id a lie the entire system is built to prevent.

### 4. Preview drift — what you'd ship isn't what's live
```sh
./build/demo --project-publish mine.gameproject development
# ...edit an asset the manifest declares...
./build/demo --project-verify mine.gameproject development   # → 'DRIFT: local <a> != channel <b>'; exit 2
```
**Why:** `--project-verify` compares the *current source's* package hash to the channel's
release. Exit **2** (drift) is distinct from exit **1** (error) so CI can gate on "did the
thing I shipped stay identical to the thing I tested?" The Hub catches the same case via
`matches_local` and recommends `publish` rather than waving you forward.

### 5. Promoting or rolling back to something that isn't there
```sh
./build/demo --release-promote preview production   # preview unset → 'channel is unset or malformed'; exit 1
./build/demo --release-rollback production deadbeefdeadbeef "oops"   # → 'no such release'; exit 1
```
**Why:** you can't promote a dangling pointer or roll back to a release the store doesn't
hold. Both are checked before any channel is moved, so a failed operation never leaves a
channel pointing at nothing.

### 6. A path-traversal attempt through a channel name or release id
```sh
./build/demo --release-rollback ../evil cbf29ce484222325   # → 'invalid channel name'; exit 1
./build/demo --release-rollback production ../../etc/passwd  # → 'invalid release id'; exit 1
```
**Why:** rollback turns two operator strings into filesystem paths, so both are validated
at the trust boundary *before* a path is built — a channel name may contain no `/` or `.`,
and a release id must be exactly 16 lowercase hex characters. These rejections are pinned
by unit tests so a later "cleanup" can't quietly loosen them.

### 7. Recovering from a bad release
```sh
./build/demo --project-publish mine.gameproject development "v1"
./build/demo --project-publish mine.gameproject development "v2 bad"   # audit records prev = v1's id
./build/demo --release-log development            # read the predecessor id off the 'v2' line
./build/demo --release-rollback development <v1-id> "revert bad v2"    # v1 is still present → playable
```
**Why:** this is exit gate 7 in practice. Because releases are immutable and every move
records its predecessor, rolling back is never a reconstruction — it's aiming a pointer at
an object that never left the store.

## Where this stops (honestly)

This guide covers the *headless* golden path, which is complete and CI-covered. Three
Horizon 1 pieces are deliberately beyond it, each for a real reason:

- **A live preview URL on a browser matrix** (exit gate 4) needs the web build served and
  driven in a real browser — the wasm build is verified, but the live pixel confirm is
  gated on the browser tooling.
- **A graphical Studio/Hub UI** (exit gate 1) is a genuine subsystem that deserves its own
  design pass; it can now be built on top of a stabilized command/release domain and the
  already-tested Hub view model.
- **Hosted release hosting and BaaS contract conformance** are ops/service work that lives
  outside the engine (in `baas/`) and stays conditional on real usage evidence.

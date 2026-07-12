# Chapter 94 — Atomic, Audited, Channelled Releases

> Code: `src/engine/release/release.{hpp,cpp}` (`AuditEntry`, `audit_line`,
> `parse_audit_line`, `audit_log_path`) · `src/engine/assets.cpp`
> (`append_file`, `rename`) · `src/main.cpp` (`write_atomic`, `record_audit`,
> `--release-log`, dev/preview/prod channels) · `tests/test_release.cpp`

Chapter 93 built a release *store*: immutable objects, movable channel pointers. It was
correct but naïve in three ways a real delivery system cannot be, and the roadmap names
each as a Horizon 1 exit gate:

- **Gate 3** — publishing must be retry-safe and *cannot expose a partial release*.
- **Gate 5** — promotion and rollback must be *audited*.
- **Gate 7** — after a bad release is rolled back, the *prior release remains playable*.

This chapter closes all three, and it is a good lesson in how little code the *correct*
version costs once the shapes are right — because the fix for each gate is a single small
idea, not a rewrite.

## Gate 3: atomic writes, or "never a torn file"

The naïve store wrote a channel pointer (or a release manifest) with one `write_file`,
which opens, truncates, and streams. If the process dies after truncate but before the
bytes land, a reader finds an empty or half-written file — a *torn* release. The channel
now points at nothing, or at garbage that fails to parse.

The fix is the oldest trick in filesystem programming: **write to a temporary file, then
rename it over the target.** `rename(2)` is atomic on a single filesystem — a reader sees
either the entire old file or the entire new one, never a mixture.

```cpp
bool write_atomic(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    return assets::write_file(tmp, bytes(text)) && assets::rename(tmp, path);
}
```

That required one new primitive on the I/O seam — `assets::rename` — kept behind
`assets::` for the same reason everything else is: the web build can redirect it in one
place. A crash now leaves the old file intact and a stray `.tmp` as harmless garbage; the
CI smoke asserts `find ... -name '*.tmp' | wc -l` is zero after a clean run, so a leaked
temp (a rename that silently failed) turns the build red.

There is a subtlety worth naming: this makes each *individual* file write atomic, not the
whole publish. A publish writes two files — the release manifest and the channel pointer.
Because the manifest is content-addressed and immutable, writing it first and the channel
second is safe in either crash order: a half-published run leaves an extra immutable
release that nothing points at (invisible, harmless) rather than a channel pointing at a
release that isn't there. Ordering the writes so the *pointer* is last is the cheap way to
get transactional behaviour without a transaction.

## Gate 5: an audit log that is a log, not a scan

"Audited" means: for every channel move, you can later answer *what moved, to what, from
what, when, and why.* The temptation is to reconstruct this by scanning the store — but
that is exactly the "collection database" smell Chapter 93 warned against, and it can't
answer "when" or "why" at all. So the history is a first-class **append-only log**:

```
1783818560 publish  development c95febd882741b29 <- (none)             # v1
1783818560 publish  development cbf29ce484222325 <- c95febd882741b29   # v2 (bad)
1783818560 rollback development c95febd882741b29 <- cbf29ce484222325   # revert bad v2
```

Each line is `epoch action channel release <prev|-> reason`. The format is pure and
round-trip-tested (`audit_line` / `parse_audit_line`), and it fails closed the same way
`parse_channel` does — a line whose "release" field isn't a real 16-hex id is skipped, not
trusted. Appending needs the other new seam primitive, `assets::append_file` (open in
`app` mode); `--release-log [channel]` reads the log *forward* and optionally filters by
channel. `ponytail:` the reader loads the whole log and prints it — fine at learning
scale; if the log ever grows large, tail it instead of loading it whole.

Why record the reason as free text at the *end* of the line? So it can contain spaces
without quoting: the parser takes five fixed fields and treats the rest of the line as the
reason. Small format decisions like "the free-text field goes last" are what keep a
hand-rolled log parseable without a real serialization format.

## Gate 7: the predecessor is the rollback target

The most important column in that log is `prev`. Before any channel move, the code reads
what the channel *currently* points at and records it as the predecessor:

```cpp
const std::string prev = read_channel(channel).value_or("");
write_atomic(channel_path(channel), serialize_channel(hex));
record_audit(action, channel, hex, prev, reason);
```

This is what makes gate 7 fall out for free. When `v2` turns out bad, its audit line
already tells you the exact id it displaced — `c95febd882741b29`. And because releases are
*immutable* (Chapter 93), that id is still in the store, byte-for-byte, ready to be rolled
back to. Rollback isn't a reconstruction; it's aiming a pointer at an object that never
left. The smoke test proves the whole arc: publish v1, publish a bad v2 (its `prev` is
v1), roll back to v1 (its `prev` is v2) — and v1 is still present and playable.

## Channel semantics: development → preview → production

Chapter 93 shipped two channels; a real flow wants three, with a direction:

- **development** — where `--project-publish` lands by default. Your private latest.
- **preview** — promoted from development when it's worth *sharing* (the metric-P2 URL).
- **production** — promoted from preview when it's *live*.

Nothing enforces the ordering — `promote` and `rollback` take arbitrary (validated)
channel names, so an experiment can invent its own — but `--release-status` reports these
three, and publish defaulting to `development` encodes the intended "publish low, promote
up" habit. Promotion never repackages; it copies a pointer, so what you tested in
development is *bit-identical* to what goes live. That property — the same immutable id
riding up the channels — is the whole point of separating the immutable object from the
movable name, and it is what lets gate 6 (author-to-share under 15 minutes) ever be true:
promotion is a pointer write, not a rebuild.

## What is deliberately still deferred

The command/resource/release layer is now *stable* — which matters, because the roadmap's
top Horizon 1 risk is "UI work starts before command/resource behavior stabilizes." With
that stability in hand, the remaining Horizon 1 work is honestly the next thing to design,
not to bolt on:

- **The Hub/Studio shell** — a UI over exactly these commands (Projects, Create, Test,
  Build, Releases, Operate). It is a real subsystem that deserves its own design pass, and
  it can now be built *on top of* a release domain that won't shift under it.
- **A hosted artifact adapter** — the same store scheme behind an object/static host, so a
  preview gets a real URL. This is the ops-heavy, evidence-gated part; the local adapter
  proves the mechanics first.
- **BaaS HTTP-contract conformance** — lives in the separate `baas/` process, not the engine.

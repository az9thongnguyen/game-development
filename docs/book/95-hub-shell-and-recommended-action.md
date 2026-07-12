# Chapter 95 — The Hub: One View, and the Next Right Thing

> Code: `src/engine/hub/hub.{hpp,cpp}` (`HubView`, `HubChannel`, `recommend`)
> · `src/main.cpp` (`build_hub_view`, `hub_dashboard`, `--hub`) · `tests/test_hub.cpp`

By Chapter 94 the platform had a lot of verbs: create, inspect, package, publish, verify,
promote, rollback, status, log. Each answers one question. But a developer sitting down
to a project doesn't have one question — they have *"where is this, and what should I do
next?"* Answering that by running nine commands and holding the result in your head is
exactly the friction the roadmap's Hub shell exists to remove.

This chapter builds the Hub as a **view/controller**: it aggregates the whole project +
release domain into one picture, and — the part that makes it a controller and not just a
dashboard — it computes the single **next recommended action**. It does this headless and
testable; the graphical Scene is deferred, because the *decision* is the valuable part and
the pixels are not what needs proving.

## The one design decision: where does the "smarts" live?

A dashboard that only prints state is a `printf`. A controller that says "do X next" has
*logic* — and logic is what you test. So the chapter's whole architecture is one split:

- **`build_hub_view` (in `main.cpp`, impure):** does the I/O — load the manifest, validate
  it, resolve the content closure, hash the current source, read each channel pointer, and
  fill in a plain `HubView` struct. No decisions, just gathering.
- **`recommend(const HubView&)` (in `hub_core`, pure):** takes that gathered state and
  returns one string: the next action. No I/O, no rendering. This is the brain, and
  `test_hub.cpp` drives it through every state with hand-built `HubView`s — no filesystem,
  no window.

This is the same pure/impure seam as every other core in the codebase (`project_core`,
`resource_core`, `release_core`), applied one level up: the hub's *judgment* is pure even
though its *inputs* come from disk. If you can't unit-test "what should I do next?" without
a filesystem, you've put the smarts in the wrong place.

## The decision itself is a pipeline walk

`recommend` is deliberately a plain top-to-bottom walk of the promotion pipeline — the
first stage that isn't in sync wins:

```cpp
if (!v.shippable)                                  return "fix: " + first_problem;
if (!dev  || dev->release.empty()
          || !dev->matches_local)                  return "publish: source is not yet the development release";
if (!prev || prev->release != dev->release)        return "promote: development -> preview";
if (!prod || prod->release != prev->release)       return "promote: preview -> production";
return "in sync: production matches your source";
```

Two things in there earn their keep:

- **`shippable` is checked first.** If validation or dependency closure fails, no amount of
  publishing helps — the only useful next step is to fix the first problem, so that's what
  it says (and it surfaces the *specific* problem, not a generic "invalid"). A hub that
  said "publish" to a broken project would be actively misleading.
- **`matches_local` guards against stale bytes.** It isn't enough that `development` points
  at *some* release — it must point at *this source's* release. If you edited an asset
  since the last publish, `development`'s id no longer equals your source's package hash,
  `matches_local` is false, and the hub correctly says "publish" rather than waving you
  forward to promote bytes that are already out of date. This is the same content-hash
  identity from Chapter 91 doing a third job: not just closure, not just packaging, but
  *"is what's published still what I have?"*

The ordering — fix ▸ publish ▸ promote-to-preview ▸ promote-to-production ▸ in-sync — is
the whole `create → test → publish → operate` loop compressed into a single sentence that
changes as you walk it. The smoke test watches it advance: fresh project says *publish*,
after publishing it says *promote to preview*, after that *promote to production*, and
finally *in sync*.

## Why not a project browser?

The roadmap's Hub lists "Projects" as one of its areas, which tempts a directory-scanning
project list. The Hub here deliberately takes **one** project path and reports on it. That
is not a shortcut — it is the same "no collection database" discipline from Chapter 93:
enumerating projects by scanning a folder breaks the moment the store lives behind a web
VFS or a network, and it invites the artifact registry to quietly become a marketplace
(the roadmap's own stated risk). When a curated multi-project view is genuinely needed, it
will read an explicit index, not a glob. `ponytail:` one reference project, which is the
blend posture's "one reference game" anyway — breadth waits for a real second consumer.

## What this is, and what it is not

`--hub <project>` is the *first* Hub shell: the view/controller essence, headless. It is
honestly **not** the graphical Hub the roadmap ultimately wants — the docked, navigable UI
with thumbnails and panels. That is a real subsystem, and building it now would violate two
things at once: the roadmap's top risk ("UI work starts before command/resource behavior
stabilizes" — though, notably, that behavior *is* now stable) and the discipline of not
scaffolding a large UI without a design pass. What this chapter proves is the part that had
to be proven first: given the stabilized domain, the aggregate view and the next-action
decision are pure, small, and correct. The pixels can be drawn on top of that with
confidence whenever the graphical path (currently gated on the browser tooling) is opened.

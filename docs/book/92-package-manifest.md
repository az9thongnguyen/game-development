# Chapter 92 — The Package Manifest and the Release-ID Seed

> Code: `src/engine/resource/resource.{hpp,cpp}` (`PackagedResource`, `package_hash`,
> `build_package`) · `src/main.cpp` (`--project-package`) · `tests/test_resource.cpp`

Chapter 91 made a project *account for* its content: declare it, resolve it, hash each
piece. This chapter takes the last small step of the content flow — turn a resolved
project into a single **package manifest**: one deterministic document that says "this
release is exactly these bytes," with one combined hash that names it.

That combined hash is the seed of an *immutable release id*. Horizon 1 will publish
releases and promote or roll back between them; "preview parity" (metric P2) means
comparing what you previewed to what you published *by hash*. None of that publishing
machinery exists yet — but all of it references a package fingerprint, so we build the
fingerprint now, headless and testable, and let the pipeline arrive later.

## What "deterministic" has to mean

A package manifest is only useful if the same inputs always produce the same document and
the same hash — on any machine, in any order the assets happened to be declared. Two
properties make that true:

1. **Order independence.** `asset` lines in a project manifest are a list a human edits;
   swapping two lines must not mint a "different" package. So `package_hash` and
   `build_package` **sort the resources by path** before doing anything else.
2. **Content sensitivity.** Changing any resource's *path* or its *content* must change
   the package hash — otherwise the fingerprint can't detect drift.

```cpp
uint64_t package_hash(std::vector<PackagedResource> resources) {
    sort_by_path(resources);                       // order-independent
    std::string canon;
    for (const auto& r : resources)
        canon += r.path + " " + hash_hex(r.hash) + "\n";   // path = identity, hash = content
    return content_hash(std::vector<uint8_t>(canon.begin(), canon.end()));
}
```

The fingerprint is `content_hash` (Chapter 91) applied to a *canonical string* built from
the sorted `(path, content-hash)` pairs. Type is deliberately left out of the fingerprint:
a resource's identity is its path and its bytes; its `type` is metadata for tooling, not
part of "which bytes shipped." The tests pin all three properties — order-independence,
content-sensitivity, path-sensitivity — because a fingerprint that quietly ignores a
change is worse than none.

## The manifest text

```text
package1
project Creator Demo (FPS + Labs)
schema 1
entry fps
resource map maps/level_00.map f6edcf0ac960ed76
resource texture textures/wall_1.hrt 244d6ca9ecf5439e
resource texture textures/wall_2.hrt 25827ac49ca0c51a
resource texture textures/wall_3.hrt f5a2d10e20ab8340
packagehash c95febd882741b29
```

`build_package` emits identity, then the resources **sorted by path**, each with its
content hash, then the combined `packagehash`. It is plain text in the same hand-written
tradition as every other format in this codebase — a release manifest you can read,
diff, and check by eye. `--project-package <path>` prints it to stdout (and refuses, like
`--project`, if validation or dependency closure fails: you cannot package a project that
would not launch).

## Why a separate `package_hash` and `build_package`

They share the sort and could be one function, but they answer different questions.
`package_hash` answers "are these two packages the same?" — a cheap `uint64_t` compare a
release channel or preview-parity check needs without parsing text. `build_package`
answers "what *is* this package?" — the human- and tool-readable document. Keeping the
hash callable on its own means a future release service can compare fingerprints without
materializing or re-parsing manifests. One computes the id; the other renders the record.

## The boundary, again

Note what stayed pure: `resource_core` builds the manifest text and hash from plain data
(`PackagedResource` structs) with no `assets::`, no `Project` type, no window — so it is
tested with synthetic resources in microseconds. The impure `--project-package` in
`main.cpp` is the only part that reads files: it resolves the closure through `assets::`,
turns each into a `PackagedResource`, and hands the list to the pure builder. Identity is
a property of bytes and lives in a core; packaging-a-real-project-on-disk is a property of
the environment and lives at the seam. This is the same split as Chapters 90–91, held on
purpose.

## What we did not build

No file output yet — `--project-package` prints to stdout; a `> creator.package` redirect
is enough until a publisher consumes it. No license/provenance/tool-version fields (the
architecture lists them, but nothing reads them yet). No immutable *store* — this is the
fingerprint and the record, not the artifact registry that will hold them (Horizon 1). We
built the seed every one of those needs, and stopped.

## Exit criterion

`./build/demo --project-package projects/creator.gameproject` prints a stable manifest —
byte-identical across runs, resources sorted, ending in a `packagehash` — and refuses on a
project that fails closure. `test_resource` pins order-independence and both sensitivities.
Suite 49/49 green. The project can now be *named by its contents*, which is the one thing
an immutable release needs before it can exist.

## Exercises

1. Add a `tool <name> <version>` line to the package and include it in the fingerprint.
   Why does a *derived* asset's package need the generator version in its hash, while a
   *source* asset's does not?
2. Make `--project-package` accept an output path and write the manifest through a
   would-be `assets::`/filesystem writer. What must be true of the write so a half-written
   package can never be mistaken for a complete one (hint: Chapter's release "publish
   interrupted" rule)?
3. Two projects declare the same texture at the same path. Should they share a resource id
   in a future registry, or stay project-scoped? What does each choice cost when one
   project edits the texture?

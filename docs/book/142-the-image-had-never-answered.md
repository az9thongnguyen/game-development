# 142 — The image had never answered

Two things were planned for this slice: an OpenAPI description of `/v1` that cannot
drift from the router, and a CI job that builds the backend image and probes
`/healthz`. `PROJECT-BRIEF` had carried this line since chapter 107:

> **Docker container builds and answers `/healthz`** — ❌ **never verified.** The
> Drogon base image is `linux/amd64`; building under arm64 emulation exceeds the
> local time budget. Dependency-complete but unproven.

The first half went the way it was designed. The second half found that the image
had never served a request in its life.

---

## Part one: a description generated from the table that serves

A hand-written spec beside a hand-written router is two tables that agree on the day
they are written and never again. This repository has watched that happen twice —
an attribution ledger that was forgotten twenty times out of twenty-three (chapter
131) and a Postgres deploy path that was a sentence for eleven slices (chapter 141).
So the design constraint was: **it must not be possible for the document and the
server to disagree.**

`baas/openapi/spec.cc` is one table, one row per operation:

```cpp
{"POST", "/v1/leaderboards/{key}/match", "leaderboards",
 "Report a rated match. The client sends the OUTCOME, never a rating: the server "
 "does the Elo, so a ladder cannot be a number the client picked. …",
 Auth::ApiKeyUser, "{ opponent_id, result: win|loss|draw, match_id }",
 {{200, "…"}, {400, "…"}, {403, "the server has no record of matching these two"},
  {404, "…"}, kBadToken, kRateLimited}},
```

What is in the row is what a router *cannot* know: what the endpoint is for, which
credential opens it, and what the answers mean. Everything else is derived — path
parameters from the `{…}` in the path, the `operationId` from the method and the
path, the tag list from first appearance. A thing already written down once does not
get a second copy to forget.

Then two tests, closing two different gaps.

**Against the router.** `drogon::app().getHandlersInfo()` is the route table the
server actually built. `test_baas_openapi` compares that set to the table's, both
directions: a route added without a row is red, a row whose route is gone is red.

**Against the file.** `baas/openapi.json` is committed, and re-baking it must produce
the same bytes — the standard `.recipe`, `collection.json` and `reference.crep` are
already held to. An API change arrives as a diff somebody reads.

Two small things fell out of building it that way.

`/dashboard` used to be registered in `main.cc`, which meant `register_routes()` was
*almost* the whole route table — and "almost" is an exemption, and an exemption is
where the next undocumented route goes. It moved, its file path became an
`AppConfig` field, and now there is nothing to exclude.

And the WebSocket. Drogon lists `/v1/ws` **once per HTTP method** — eleven rows for
one route — because an upgrade is not really any of them. The first version of the
test hard-coded `GET /v1/ws` as a skip. That is a name in a test, and names in tests
rot. The description Drogon itself attaches to the handler says
`WebsocketController: web::rt::WsController`, so the rule went into `live_routes()`:
a WebSocket controller is reported once, as the GET it starts life as. The test has
no exemptions at all.

51 routes registered. 51 documented.

---

## Part two: `--seed` returned zero

`baas/ops/Dockerfile` has ended with this line since chapter 107:

```dockerfile
CMD ["--host", "0.0.0.0", "--port", "8080", "--db", "sqlite:///app/data/baas.db", "--seed"]
```

and `main.cc` had this:

```cpp
if (do_seed) {
    const std::string pk = web::db::seed(db);
    std::printf("seeded. project public_key = %s   secret_key = sk_demo_colony\n", pk.c_str());
    return 0;
}
```

The container seeded and exited. Every time. `docker-compose.yml` puts
`restart: unless-stopped` in front of it, so what shipped was a loop that seeded,
exited zero, restarted, seeded, exited zero — and never listened. The healthcheck
beside it, `curl -fsS http://localhost:8080/healthz`, could never have passed.

Run against the image, before the fix:

```
running: false  exit: 0
curl: (7) Failed to connect to 127.0.0.1 port 18101
seeded. project public_key = pk_demo_colony   secret_key = sk_demo_colony
```

The flag had two intents wearing one name. A person at a terminal types `--seed` to
print the demo key and get their prompt back; a container passes `--seed` to mean
"make sure the demo project exists **before you serve**". So it is two flags now:
`--seed` seeds and carries on, `--seed-only` seeds and stops.

And a smaller one, found by reading `docker logs` and seeing nothing at all: `stdout`
is block-buffered when it is a pipe, so the one line an operator needs — the demo
project's key — sat in the buffer for as long as the server ran, which is forever.
One `fflush`.

---

## Why no test could have caught it

Every test in this suite links `baas_core` and calls `register_routes()` itself. That
covers the whole server except the file that decides what the server *does on
startup*, and the bug was in `main()`. Ninety tests, and none of them had ever run
`main()`.

So `test_baas_boot` does. It spawns the real binary — `posix_spawn`, stdout to a
file, a free port — and asks it questions from outside:

- `--seed`: the process is **still running** after `/healthz` answers, it logged the
  seeded key *while running*, a guest can sign in against the seeded project (which
  needs the api key to resolve, a writable database and a working JWT signer), and
  the `/openapi.json` it serves is byte-identical to the committed file.
- `--seed-only`: exits 0 and prints the key. Both directions of the split, because a
  flag that always exits was the bug and a flag that never exits is a different one.
- `--openapi FILE`: writes the committed bytes and exits.

This is the fourth time the same lesson has been written down in this project.
Chapter 137: a browser check that did not survive a second game. Chapter 138: a
thousand replays in one process saying nothing about two toolchains. Chapter 139:
one process playing both sides never giving them the same team. Chapter 141: a
single-connection backend hiding three guarantees a pooled one does not give. And
now: a test harness that constructs the app itself can never see what the app's own
entry point does. **The proof does not transfer across the boundary you did not
cross.**

The CI job crosses it anyway, because a laptop that has to emulate amd64 will not do
this every time: `baas-docker` builds the image, starts it, asserts the container is
**still running** — the failure this job exists for looks like a missing container,
not a failed request — then probes `/healthz`, signs in a guest, and checks the
served document against the one in the repo.

---

## Mutations

Seventeen, all killed, against `test_baas_openapi` and `test_baas_boot` only.

The two that matter most are M1 and M2: putting `return 0` back where it was, and
removing the early return entirely so `--seed-only` never stops. Both die in
`test_baas_boot`, which is the whole reason it exists — before this slice both would
have survived the entire suite.

M2 killed the harness too, and that is worth writing down. `--seed-only` that never
stops makes `test_baas_boot` **hang** rather than fail, `subprocess.run(timeout=…)`
raises rather than returning a code, and the driver died with the mutation still in
the tree and an orphaned server holding a port. Every previous slice's harness had
only ever seen a mutation FAIL a test. A mutation can also make one wait forever, and
that is still a kill — it just has to be caught and named as one:

```python
except subprocess.TimeoutExpired:
    subprocess.run(["pkill", "-f", "build/baas/baas"])
    return "killed (hang)"
```

Third time this project has had to fix the harness rather than the code (chapters 133
and 137 were the other two), and the same rule applied each time: **restore by file
copy, `utime` afterwards, and print a post-restore baseline** — which is how the crash
was noticed at all.

---

## Files

- `baas/openapi/openapi.{h,cc}` — `spec`, `document`, `live_routes`, `documented_routes`
- `baas/openapi/spec.cc` — the table, 51 rows
- `baas/openapi.json` — generated, committed, byte-compared
- `baas/app_setup.cc` — `/openapi.json` and `/dashboard`
- `baas/main.cc` — `--seed` / `--seed-only`, `--openapi FILE`, one `fflush`
- `tests/test_baas_openapi.cc`, `tests/test_baas_boot.cc`
- `.github/workflows/ci.yml` — the `baas-docker` job
- `README.md`, `baas/ops/deploy.md` — the flag split

# Chapter 107 — A Backend Container, and the Build Split That Allowed It

> Code: `CMakeLists.txt` (`ENGINE_BUILD_DESKTOP` option, guarded SDL2 find + `demo` target) ·
> `baas/ops/Dockerfile` · `baas/ops/docker-compose.yml` · `.dockerignore` ·
> Runbook: `baas/ops/deploy.md`

Every prior Horizon 2 chapter changed the *server*. This one is about *shipping* it — and the
interesting obstacle turned out to be in the build system, not the container.

## The obstacle: configure required a graphics library

The BaaS is, by design, a separate process that links no engine code. But you could not *configure*
the project to build only the BaaS: the root `CMakeLists.txt` searched for SDL2 unconditionally,
`REQUIRED`, because the desktop `demo` executable needs it. So a server image — which never draws a
pixel — would still fail to configure unless SDL2 development packages were installed in it. The
coupling was subtle: not a link dependency (the `baas` binary never linked SDL2), but a *configure-time*
one. That is the kind of coupling that hides until you try to deploy.

## One option, two guards

The fix is a single option and two guards, mirroring how the project already treats Drogon (found →
build the BaaS, absent → skip it):

```cmake
option(ENGINE_BUILD_DESKTOP "Build the SDL2 desktop 'demo' executable" ON)
```

- The SDL2 search is guarded: when `ENGINE_BUILD_DESKTOP` is `OFF` (and not a web build), SDL2 is not
  searched for at all, and `SDL2_LINK` is empty.
- The `demo` target is guarded: it is created only `if(EMSCRIPTEN OR ENGINE_BUILD_DESKTOP)`.

Everything else — the SDL-free `*_core` libraries, every unit test, and the whole `baas/` subtree —
is untouched and builds either way. The change is verified in both directions, which is the only way
to trust a build-system edit:

```sh
cmake -B build                                    # default: "SDL2: found", demo builds
cmake -B b2 -DENGINE_BUILD_DESKTOP=OFF            # "skipping SDL2", baas builds, demo target absent
```

The default developer experience is identical (the option defaults to `ON`); only a deliberate
`OFF` unlocks the backend-only build. This is the smallest change that decouples the server from the
desktop — no new subdirectory, no split of the root list file, no duplicated target definitions.

## The container is now boring

With the split in place, the Dockerfile is unremarkable, which is the goal. A two-stage build uses
the official Drogon image for the toolchain, adds libsodium, and builds *only* the `baas` target with
`-DENGINE_BUILD_DESKTOP=OFF` — so no SDL2 package is ever installed. The runtime stage sits on the
same Drogon base (so every shared library the binary needs is already there) and copies in just the
binary and `assets/`. Secrets arrive from the environment; the SQLite database lives on a mounted
volume so it survives restarts and can be fed to the backup/restore drill from Chapter 98.

`docker compose -f baas/ops/docker-compose.yml up --build` brings it up with both secrets required
(the compose file fails fast if they are unset — no accidental production run on the insecure dev
defaults), port 8080 mapped, a persistent data volume, and a `/healthz` health check.

## What is verified, and what is not

Honesty about verification matters more here than a clean-sounding claim. What is verified:

- The **build-system split**, both directions, on the native toolchain: default configure finds SDL2
  and builds `demo`; `-DENGINE_BUILD_DESKTOP=OFF` skips SDL2 and builds `baas` with the `demo` target
  absent. This is the substantive engine-side change, and it is proven.
- Inside the Drogon container, configure reaches the split and **correctly skips SDL2** — confirming
  the whole point of the option carries into the image.
- The container's configure-time dependency gap was found by running it (`find_package(CURL)` from
  the SDK subtree) and fixed by adding `libcurl4-openssl-dev` alongside libsodium.

What is **not** verified in this repository's environment: a full green `docker build` and a running
container answering `/healthz`. The official Drogon image is `linux/amd64`, and building it here means
compiling under `arm64→amd64` emulation, which is too slow to finish inside the sandbox's time budget.
The Dockerfile is dependency-complete and uses only the verified split plus a standard two-stage
pattern, so it should build cleanly on a native-amd64 Docker host or in CI — but this document does not
claim a container was booted here, because it was not. This is the same posture as the PostgreSQL path
in Chapter 98: state what ran, and name what awaits an environment that can run it.

## The honest edge

Even fully built, this is a single-node, single-container deployment with SQLite — the right shape for
a small self-hosted game. What it is *not* is a scaled cluster. Horizontal scaling, managed Postgres,
TLS termination, and an orchestrator are integration concerns — commodity infrastructure the strategy
says to adopt when a reference game's traffic demands it, not to hand-build into the engine's
repository on momentum. The deployment runbook (`baas/ops/deploy.md`) says the same in operator terms:
here is the container, here is how to run and back it up, and here is the line past which you reach for
infrastructure rather than more code in this tree.

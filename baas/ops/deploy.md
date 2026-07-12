# Deploying the Game BaaS Backend (Horizon 2)

Operator runbook for running the BaaS as a container: the build-system split that makes a
backend-only image possible, how to build and run it, secrets, persistence, and how it ties into
the backup/restore drill.

## 1. The build-system split

The BaaS is a separate process that links no engine code — but until now, *configuring* the CMake
project required SDL2, because the root `CMakeLists.txt` searched for it unconditionally (for the
desktop `demo`). That coupled a server image to a desktop graphics library it never uses.

`-DENGINE_BUILD_DESKTOP=OFF` fixes it: with that flag, configure skips the SDL2 search **and** the
`demo` target entirely, building only the BaaS and the SDL-free `*_core` libraries and tests. This
is verified both ways:

```sh
cmake -B build                                  # default: SDL2 found, 'demo' builds
cmake -B build-backend -DENGINE_BUILD_DESKTOP=OFF   # skips SDL2; 'demo' target absent; baas builds
```

The container build uses `OFF`, so no SDL2 package is ever installed in the image.

## 2. Build the image

Build context is the **repo root** (the backend needs the root `CMakeLists.txt` + `baas/`):

```sh
docker build -f baas/ops/Dockerfile -t hand-baas .
```

It is a two-stage build: the [official Drogon image](https://hub.docker.com/r/drogonframework/drogon)
supplies Drogon and its toolchain, we add `libsodium` + `libcurl` (the SDK subtree's configure-time
dependency), build only the `baas` target with `ENGINE_BUILD_DESKTOP=OFF`, and copy the binary +
`assets/` into a runtime stage on the same Drogon base (so every shared library the binary needs is
already present).

> **Verification status.** The build-system split is verified on the native toolchain (both configs),
> and configure inside the Drogon container was run far enough to confirm SDL2 is skipped and to find
> + fix the missing `libcurl` dependency. A full green `docker build` was **not** completed in this
> repository's environment because the Drogon image is `linux/amd64` and builds here run under slow
> `arm64→amd64` emulation. Run this build on a native-amd64 Docker host or in CI to confirm end to end.

## 3. Run it

Secrets come from the environment — never baked into the image, never left at the insecure dev
defaults:

```sh
export BAAS_JWT_SECRET=$(openssl rand -hex 32)
export BAAS_ADMIN_SECRET=$(openssl rand -hex 32)
docker compose -f baas/ops/docker-compose.yml up --build
```

Compose maps port 8080, injects the two secrets (failing fast if they are unset), persists the
SQLite database in the `baas-data` volume, and health-checks `/healthz`. Verify:

```sh
curl -fsS localhost:8080/healthz            # {"status":"ok"}
```

## 4. Configuration surface

| Setting | Flag | Env | Default |
|---------|------|-----|---------|
| Bind host | `--host` | — | 127.0.0.1 (compose uses 0.0.0.0) |
| Port | `--port` | — | 8080 |
| Database URL | `--db` | — | `sqlite:///app/data/baas.db` |
| JWT secret | `--jwt-secret` | `BAAS_JWT_SECRET` | insecure dev default (warns) |
| Admin secret | `--admin-secret` | `BAAS_ADMIN_SECRET` | insecure dev default (warns) |
| Seed demo project | `--seed` | — | off |

## 5. Persistence & backups

The database is a single SQLite file under `/app/data` (a named volume). Back it up — and prove you
can restore — with the drill from the persistence runbook, pointed at the volume's file:

```sh
baas/ops/backup_restore_drill.sh sqlite:///path/to/baas-data/baas.db
```

See `docs/guides/backup-restore-drill.md` for the full failure-drill procedure and the documented
PostgreSQL production path (a Drogon-with-libpq build + `--db postgres://…`).

## 6. What this is not (yet)

This is a single-container, single-node deployment with SQLite — the right shape for a small
self-hosted game. Horizontal scale, a managed Postgres, TLS termination, and an orchestrator
(Kubernetes/Nomad) are integration concerns the strategy says to bring in from commodity infra when
a reference game's traffic actually needs them, not to hand-build here.

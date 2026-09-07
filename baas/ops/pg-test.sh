#!/usr/bin/env sh
# =============================================================================
#  baas/ops/pg-test.sh  —  run the BaaS suite against a REAL Postgres
# =============================================================================
#  The suite's default backend is SQLite, whose pool is one connection — which
#  makes every read-then-write in the codebase look atomic whether or not it is.
#  The row locks added in chapter 140 only mean something on a backend with a real
#  pool, so this script provides one.
#
#  It needs Docker and nothing else: the drogonframework/drogon image carries the
#  libpq-enabled Drogon that Homebrew's bottle does not, and postgres:16-alpine is
#  the database. The repo is mounted, so the build lands in build-pg/ (gitignored)
#  and nothing is copied anywhere.
#
#      sh baas/ops/pg-test.sh              # build + run every baas test on Postgres
#      sh baas/ops/pg-test.sh baas_purchase   # ...or one, by ctest regex
#
#  IT FAILS TODAY, and that is the point of it existing (chapter 140). The first time
#  this repository ever pointed its backend at Postgres, the very first statement
#  died:
#
#      ERROR:  syntax error at or near ","
#      LINE 1: INSERT INTO schema_migrations(version, name) VALUES(?,?)
#
#  Drogon does not translate `?` placeholders to `$1`, and every one of the 107
#  queries in baas/ is written with `?`. The schema is SQLite-shaped too — `id
#  INTEGER PRIMARY KEY` is an auto-increment there and a plain column in Postgres,
#  and `insertId()` needs a RETURNING clause. So "Postgres is a documented
#  deploy-time build" was a sentence, not a capability.
#
#  This script is therefore a REPRODUCTION, not a gate: it is what turns that
#  sentence into a failing command anybody can run. Making it pass is its own slice.
#  CI does not call it yet, deliberately — a red job nobody can fix teaches nothing.
# =============================================================================
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
NET=baas-pg-net
PG=baas-pg
PGPORT=5432
FILTER=${1:-}

cleanup() {
    docker rm -f "$PG" >/dev/null 2>&1 || true
    docker network rm "$NET" >/dev/null 2>&1 || true
}
trap cleanup EXIT

cleanup
docker network create "$NET" >/dev/null

echo "== postgres =="
docker run -d --name "$PG" --network "$NET" \
    -e POSTGRES_PASSWORD=baas -e POSTGRES_USER=baas -e POSTGRES_DB=baas \
    postgres:16-alpine >/dev/null

# Wait for it to accept connections. `pg_isready` rather than a sleep: a fixed sleep
# is either too short on a loaded machine or wasted on a fast one.
i=0
while [ "$i" -lt 60 ]; do
    if docker exec "$PG" pg_isready -U baas -d baas >/dev/null 2>&1; then break; fi
    i=$((i + 1)); sleep 1
done
[ "$i" -lt 60 ] || { echo "postgres never became ready"; exit 1; }
echo "ready after ${i}s"

echo "== build + test =="
# --platform linux/amd64: the drogon image is amd64-only. On Apple Silicon this runs
# under Rosetta at near-native speed; on an amd64 runner it is a no-op.
docker run --rm --network "$NET" --platform linux/amd64 \
    -v "$ROOT:/src" -w /src \
    -e BAAS_TEST_DB="postgres://baas:baas@$PG:$PGPORT/baas" \
    drogonframework/drogon:latest sh -c "
        set -e
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends libsodium-dev libcurl4-openssl-dev >/dev/null
        cmake -B build-pg -DCMAKE_BUILD_TYPE=Release -DENGINE_BUILD_DESKTOP=OFF
        cmake --build build-pg --target baas_tests -j\"\$(nproc)\"
        ctest --test-dir build-pg/baas --output-on-failure ${FILTER:+-R '$FILTER'}
    "

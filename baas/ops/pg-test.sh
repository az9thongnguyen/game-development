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
#  IT PASSED ON 2026-09-07, and it took four runs of this script to get there
#  (chapter 141). What it found, in order, is the whole story of the difference
#  between "the code compiles against both" and "it runs on both":
#
#    1. `?` is not a Postgres placeholder. Drogon does not translate it, so the very
#       first statement died — `INSERT INTO schema_migrations(version, name) VALUES(?,?)`.
#       Now `db::portable` does, along with AUTOID, CURRENT_TIMESTAMP and BYTELEN.
#    2. Drogon binds an integer in BINARY, sizeof(T) bytes, with no type OID. A C++
#       `long` into an INTEGER column is "incorrect binary data format in bind
#       parameter 1", fifty-eight times. Postgres parameters go as text now.
#    3. `FOR UPDATE` cannot lock a row that does not exist, so the FIRST write to a key
#       was still a race — chapter 140 had closed only the second one.
#    4. And the quietest of the four: Drogon ENQUEUES its COMMIT. With a pool of one the
#       next statement queues behind it; with a real pool it does not, and every value
#       came back one write stale.
#
#  None of those four could be seen on SQLite, and all four were in code with tests.
#  CI runs the whole baas suite twice now — once per backend — so number five, whatever
#  it is, gets found by the test that already covers the feature.
#
#  This script remains the way to run it locally, and the way to bisect a Postgres-only
#  failure without pushing.
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

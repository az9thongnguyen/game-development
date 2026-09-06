# Chapter 129 — The half of the suite that never ran

> Code: `.github/workflows/ci.yml` · `baas/CMakeLists.txt`

## Tóm tắt (VI)

`ctest` ở máy này báo **76 test**. CI báo **48**. Chênh lệch **28** là toàn bộ nửa
Drogon-gated — auth, JWT, leaderboard, cloud save, inventory, purchase, RBAC, live ops,
replays, tracing, và **ba bài end-to-end dựng server thật** — và chúng **tối ở CI từ ngày
được viết**, vì runner chỉ cài SDL2.

Công thức không mới: `baas/ops/Dockerfile` đã dựng đúng thứ này từ chương 107. Job mới
chính là **build stage của Dockerfile đó, dừng ở test thay vì ở binary**.

Ba điều đáng kể:

1. **Con số của tôi cũng sai — và tôi đo thay vì đếm.** Kế hoạch ghi "23 test"; đó là số
   **file** `test_baas_*.cc`, không phải số test đăng ký. Cấu hình lại với
   `-DCMAKE_DISABLE_FIND_PACKAGE_Drogon=ON` — đúng cái CI nhìn thấy — cho **48**, so với
   76 ⇒ **28**. File không phải test.
2. **Bắt CI chạy đã lộ ra một bug thật ngay lập tức.** `test_sdk_realtime_live` **không
   biên dịch** trên libcurl 7.81: nó dùng `curl_ws_recv`/`CURLWS_TEXT`, tức WebSocket API
   có từ 7.86. SDK thì đã xử lý đàng hoàng — không có header thì transport realtime biên
   dịch thành **stub** và in ra một dòng. Còn **test kiểm chính cái stub đó lại không
   gate**, nên nó kéo sập cả nửa `baas` của bản build. Không ai thấy vì **máy duy nhất
   từng dựng nửa này là một cái Mac có curl 8.x của Homebrew**.
3. **Và bên dưới là một phát hiện về sản phẩm, không phải về CI:** transport realtime
   ws:// native của SDK là **stub trên mọi bản Ubuntu 22.04** — kể cả **image container
   của chính backend này**. Người dùng Linux dựng SDK ở đó có REST và **không có
   realtime**, và điều duy nhất nói ra là một dòng `STATUS` lúc configure.

Kết quả: **27/27 xanh trong container, 51 giây**, đã chạy thật ở đúng image và đúng kiến
trúc của runner **trước khi** đẩy YAML lên.

---

## The number nobody had written down

The CI file said this, and it read like a design note:

> *"The Drogon-guarded `baas` target is skipped automatically when Drogon is absent."*

It is true. It is also the whole problem, because it does not say **how much** is skipped.
Measuring is one flag:

```sh
cmake -B build-nodrogon -DCMAKE_DISABLE_FIND_PACKAGE_Drogon=ON   # what CI configures
ctest --test-dir build-nodrogon -N | tail -1     # Total Tests: 48
ctest --test-dir build            -N | tail -1   # Total Tests: 76
```

Twenty-eight tests. Not a corner: `baas_auth`, `baas_jwt`, `baas_rbac`, `baas_purchase`,
`baas_cloudsave`, `baas_inventory`, `baas_secret_rotation`, `baas_idempotency`, and the
three that boot a real server and drive it with the real SDK (`sdk_live`, `farm_live`,
`sdk_realtime_live`). Every one of them about money, identity, or somebody else's data.

The plan for this slice said *23*. That was `ls tests/test_baas_*.cc | wc -l` — the number
of **files**. A file with a `foreach` in its CMake entry registers seven tests; a file that
is a helper registers none. Counting files to describe a suite is the same class of error
as counting lines to describe a program, and it is worth naming because it went into a
planning document unchallenged.

## The recipe already existed

`baas/ops/Dockerfile` has built exactly this since chapter 107:

```dockerfile
FROM drogonframework/drogon:latest AS build
RUN apt-get install -y libsodium-dev libcurl4-openssl-dev
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DENGINE_BUILD_DESKTOP=OFF \
 && cmake --build build --target baas
```

`-DENGINE_BUILD_DESKTOP=OFF` is the build-system split that lets a server image exist
without SDL2 at all. The CI job is that same stage, stopping at the tests.

Two small pieces of build system keep it from carrying a list:

```cmake
# baas/CMakeLists.txt — every target THIS directory defines, as one target.
get_property(baas_dir_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(baas_tests)
add_dependencies(baas_tests ${baas_dir_targets})
```

```sh
ctest --test-dir build-baas/baas     # exactly the tests that directory registers
```

The **directory is the grouping** — no regex over test names (two of them are called
`metrics` and `rate_limiter`, which no `baas_*` pattern would catch), and no hand-written
list of target names, which is a thing that goes stale the day somebody adds a test and
forgets it. Which is, precisely, how this half went dark.

## Running it found a bug in the first minute

```
/src/tests/test_sdk_realtime_live.cc:64:48: error: 'curl_ws_recv' was not declared
  in this scope; did you mean 'curl_easy_recv'?
```

libcurl's WebSocket API arrived in 7.86. Ubuntu 22.04 — the Drogon image's base — ships
**7.81**.

The interesting part is not the version. It is the **asymmetry**. `sdk/cpp/CMakeLists.txt`
had already thought about this:

```cmake
find_path(GBAAS_CURL_WS_INC curl/websockets.h ...)
if(GBAAS_CURL_WS_INC AND GBAAS_CURL_WS_LIB)
  target_compile_definitions(gbaas_sdk PRIVATE GBAAS_HAS_WS_CURL=1)
else()
  message(STATUS "gbaas_sdk: no WebSocket-capable libcurl; native realtime is a stub")
```

So the *transport* degrades gracefully and announces it. The *test that exercises the
degradation* was added unconditionally — and a test that will not compile does not fail
one test, it fails **the build**, and with it the other 27. The graceful path had never
been walked end to end, because the only machine that ever built this half was a Mac
whose Homebrew curl is 8.x.

The fix gates the test on the same two variables the transport is gated on, so the two
cannot disagree about whether ws:// exists:

```cmake
if(GBAAS_CURL_WS_INC AND GBAAS_CURL_WS_LIB)
  add_executable(test_sdk_realtime_live ...)
  add_test(NAME sdk_realtime_live COMMAND test_sdk_realtime_live)
else()
  message(STATUS "baas: SKIPPING sdk_realtime_live — this libcurl has no WebSocket API "
                 "(needs >= 7.86). The SDK's native realtime is a stub in this build "
                 "too, so the test would have nothing to drive.")
endif()
```

The message is not decoration. **A test that vanishes quietly is the exact failure this
slice exists to end**, so a skip has to be as loud as a failure — at configure time, and
again in the CI log, where the job prints what it could not build before it builds
anything.

## The finding underneath

Follow the same fact one step further and it stops being about CI.

The SDK's **native ws:// realtime transport is a stub on any Ubuntu 22.04 build** — which
includes `baas/ops/Dockerfile`, this project's own backend image. A Linux user who builds
the SDK there gets REST and no realtime, and the only thing that says so is one `STATUS`
line during configure. Nothing at runtime reports it; the handle exists and does nothing.

That is a product ceiling, not a CI one, and it is now written down rather than implied by
a build flag. Fixing it properly means either shipping a modern libcurl or writing the
WebSocket client by hand — and this repository has a hand-written HTTP server already, so
the second is not the joke it sounds like. Neither belongs in this slice.

## What the job asserts

```yaml
n=$(ctest --test-dir build-baas/baas -N | grep -cE 'Test +#')
[ "$n" -ge 27 ] || { echo "::error::only $n baas tests were registered"; exit 1; }
```

A floor, not an exact count: adding a test must not break CI, and losing the directory
must. **A green job that ran nothing is the failure mode this whole slice is about**, and
without this line it is indistinguishable from a green job that ran everything.

## Verified before it was pushed

The CI file used to carry this note at the top:

> *"Authored 2026-07-11. NOTE: written but not run in the authoring environment — its
> first green run is the owner's verification after push."*

That note is now scoped to the steps it still describes. This job was **run**: the same
image (`drogonframework/drogon:latest`), the same architecture as the runner (amd64, under
emulation on this machine), the same three commands.

```
-- gbaas_sdk: no WebSocket-capable libcurl; native realtime is a stub
-- Drogon: found (/usr/local/lib/cmake/Drogon) — building 'baas' backend
-- baas: SKIPPING sdk_realtime_live — this libcurl has no WebSocket API (needs >= 7.86)
...
100% tests passed, 0 tests failed out of 27
Total Test time (real) =  51.28 sec
```

51 seconds of tests, including two end-to-end runs that boot a Drogon server, sign in, and
drive it through the real SDK. The build in front of them is the slow part, and it is the
part CI parallelises for free.

## What was checked

| Claim | How |
|---|---|
| 28 tests were dark in CI, not 23 | configured with `-DCMAKE_DISABLE_FIND_PACKAGE_Drogon=ON` and diffed `ctest -N` against the normal build |
| the job builds in the runner's image | ran it in `drogonframework/drogon:latest`, amd64 |
| 27 of the 28 pass there | `100% tests passed, 0 tests failed out of 27` |
| the 28th is skipped, loudly | the configure line appears in the log, and the job prints skips before building |
| the job cannot silently run nothing | asserts at least 27 tests are registered |
| no list to go stale | `BUILDSYSTEM_TARGETS` for building, `--test-dir …/baas` for running |
| nothing regressed locally | full `ctest` still 76/76 on the machine that has a modern curl |

## Ceilings

- **`sdk_realtime_live` still runs on exactly one machine.** CI covers 27 of 28. The
  missing one needs libcurl ≥ 7.86, and the base image has 7.81.
- **The SDK's native realtime is a stub on Ubuntu 22.04**, silently at runtime. See above.
- **SQLite only.** These 27 run against SQLite with a pool of one, which is what makes the
  purchase path safe today (`inv_service.cc`) — the Postgres adapter and the `FOR UPDATE`
  it requires are still one slice, deliberately, because splitting them would ship a race.
- **No Docker job.** CI builds and tests the backend; it does not build the *image* or hit
  `/healthz`. `baas/ops/Dockerfile` is still verified by hand.
- **amd64 only, and emulated here.** The local verification ran the runner's architecture
  under emulation; a native amd64 run is CI's first.
- **These tests boot real servers on ephemeral ports.** They pass in a container; they are
  still the part of the suite most likely to be flaky under load.

# Map / Level Lab — Implementation Plan

> **For agentic workers:** TDD, one behaviour per commit. Steps use `- [ ]`.

**Goal:** `--maplab` authors a `fps::Map` tile grid → Save `.map` asset → `--fps` loads it.

**Architecture:** Reuse `fps::Map` as the one model. Pure `to_text`/`from_text` in `fps_core`
(shared); pure edit ops (`flood_fill`/`fill_rect`/`set_cell`) in `maplab_core`; asset IO only
in the demo-side scenes. Deferrals: iso consumption, resize, per-cell textures (spec §9).

**Tech Stack:** C++20, `fps::Map`, `ui::Context`, `assets::`, `Renderer2D`.

---

### Task 1: shared text serializer in `fps_core`

**Files:** Modify `src/games/fps/map.hpp`, `src/games/fps/map.cpp`, Test `tests/test_fps.cpp`

- [ ] **Step 1 — failing tests** (add to test_fps.cpp; call from `main`):

```cpp
static void test_map_serialize() {
    Map m; m.w = 3; m.h = 2; m.cells = {1,1,1, 1,0,2};
    const std::string s = to_text(m);
    auto r = from_text(s);
    CHECK(r && r->w == 3 && r->h == 2 && r->cells == m.cells);
    CHECK(to_text(*r) == s);                       // round-trip
    CHECK(!from_text("garbage"));                  // bad header -> nullopt
    CHECK(!from_text("fpsmap1\nsize 3 2\nrow 1 1 1\n"));  // too few rows -> nullopt
}
```

- [ ] **Step 2 — declare** in map.hpp (after `Map default_map();`, add `#include <string>` and `<optional>`):

```cpp
std::string          to_text(const Map& m);           // fpsmap1 text form
std::optional<Map>   from_text(const std::string& s); // nullopt if malformed
```

- [ ] **Step 3 — implement** in map.cpp:

```cpp
#include <optional>
#include <sstream>
#include <string>

std::string to_text(const Map& m) {
    std::string s = "fpsmap1\nsize " + std::to_string(m.w) + " " + std::to_string(m.h) + "\n";
    for (int y = 0; y < m.h; ++y) {
        s += "row";
        for (int x = 0; x < m.w; ++x) s += " " + std::to_string(int(m.cells[size_t(y) * m.w + x]));
        s += "\n";
    }
    return s;
}

std::optional<Map> from_text(const std::string& s) {
    std::istringstream in(s);
    std::string tok;
    if (!(in >> tok) || tok != "fpsmap1") return std::nullopt;
    if (!(in >> tok) || tok != "size")    return std::nullopt;
    Map m;
    if (!(in >> m.w >> m.h) || m.w <= 0 || m.h <= 0) return std::nullopt;
    m.cells.assign(size_t(m.w) * m.h, 0);
    for (int y = 0; y < m.h; ++y) {
        if (!(in >> tok) || tok != "row") return std::nullopt;
        for (int x = 0; x < m.w; ++x) {
            int v;
            if (!(in >> v) || v < 0 || v > 255) return std::nullopt;
            m.cells[size_t(y) * m.w + x] = uint8_t(v);
        }
    }
    return m;
}
```

- [ ] **Step 4 — run:** `ctest --test-dir build -R fps` → PASS
- [ ] **Step 5 — commit:** `feat(fps): shared fpsmap1 text serializer (to_text/from_text)`

### Task 2: `maplab_core` edit ops

**Files:** Create `src/games/maplab/edit.hpp`, `src/games/maplab/edit.cpp`, `tests/test_maplab.cpp`; Modify `CMakeLists.txt`

- [ ] **Step 1 — edit.hpp:**

```cpp
#pragma once
#include <cstdint>
#include "games/fps/map.hpp"

namespace maplab {
void set_cell (fps::Map& m, int x, int y, uint8_t id);
void fill_rect(fps::Map& m, int x0, int y0, int x1, int y1, uint8_t id);
void flood_fill(fps::Map& m, int x, int y, uint8_t id);   // 4-connected region replace
fps::Map bordered(int w, int h);                          // floor with a wall(1) border
}
```

- [ ] **Step 2 — edit.cpp:**

```cpp
#include "games/maplab/edit.hpp"
#include <vector>

namespace maplab {
static bool in(const fps::Map& m, int x, int y) { return x >= 0 && y >= 0 && x < m.w && y < m.h; }

void set_cell(fps::Map& m, int x, int y, uint8_t id) {
    if (in(m, x, y)) m.cells[size_t(y) * m.w + x] = id;
}
void fill_rect(fps::Map& m, int x0, int y0, int x1, int y1, uint8_t id) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) set_cell(m, x, y, id);
}
void flood_fill(fps::Map& m, int x, int y, uint8_t id) {
    if (!in(m, x, y)) return;
    const uint8_t from = m.cells[size_t(y) * m.w + x];
    if (from == id) return;                       // no-op guard (else infinite loop)
    std::vector<std::pair<int,int>> stack{{x, y}};
    while (!stack.empty()) {
        auto [cx, cy] = stack.back(); stack.pop_back();
        if (!in(m, cx, cy) || m.cells[size_t(cy) * m.w + cx] != from) continue;
        m.cells[size_t(cy) * m.w + cx] = id;
        stack.push_back({cx + 1, cy}); stack.push_back({cx - 1, cy});
        stack.push_back({cx, cy + 1}); stack.push_back({cx, cy - 1});
    }
}
fps::Map bordered(int w, int h) {
    fps::Map m; m.w = w; m.h = h; m.cells.assign(size_t(w) * h, 0);
    fill_rect(m, 0, 0, w - 1, 0, 1); fill_rect(m, 0, h - 1, w - 1, h - 1, 1);
    fill_rect(m, 0, 0, 0, h - 1, 1); fill_rect(m, w - 1, 0, w - 1, h - 1, 1);
    return m;
}
} // namespace maplab
```

- [ ] **Step 3 — test_maplab.cpp** (CHECK macro like test_fps):

```cpp
#include "games/maplab/edit.hpp"
#include <cstdio>
using namespace maplab;
static int g_failures = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); ++g_failures; } }while(0)

int main() {
    // bordered: edges are wall(1), interior floor(0)
    fps::Map m = bordered(5, 4);
    CHECK(m.at(0,0)==1 && m.at(4,3)==1 && m.at(2,1)==0);
    // flood_fill the interior floor -> id 7, stops at the border
    flood_fill(m, 2, 1, 7);
    CHECK(m.at(2,1)==7 && m.at(3,2)==7);          // interior recoloured
    CHECK(m.at(0,0)==1 && m.at(4,0)==1);          // border untouched
    // a wall splitting the interior confines the fill
    fps::Map w = bordered(5, 4);
    set_cell(w, 2, 1, 1); set_cell(w, 2, 2, 1);   // vertical wall at x=2
    flood_fill(w, 1, 1, 9);
    CHECK(w.at(1,1)==9 && w.at(1,2)==9);          // left pocket filled
    CHECK(w.at(3,1)==0 && w.at(3,2)==0);          // right pocket NOT reached
    // same-id flood is a no-op (no hang)
    flood_fill(w, 3, 1, 0); CHECK(w.at(3,1)==0);
    // fill_rect clamps out-of-bounds, set_cell bounds-safe (no crash)
    fill_rect(m, -3, -3, 100, 100, 2); CHECK(m.at(2,2)==2);
    set_cell(m, -1, -1, 5); set_cell(m, 999, 0, 5);
    if (g_failures==0) std::printf("maplab: all tests passed\n");
    else               std::printf("maplab: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
```

- [ ] **Step 4 — CMake:** add `maplab_core` (STATIC `src/games/maplab/edit.cpp`, link `fps_core engine_flags`,
  include `src`), `test_maplab` (link `maplab_core fps_core engine_flags`, `add_test`).
- [ ] **Step 5 — run:** `cmake -B build ... && ctest --test-dir build -R maplab` → PASS
- [ ] **Step 6 — commit:** `feat(maplab): edit-op core (flood_fill/fill_rect/set_cell/bordered)`

### Task 3: `--maplab` editor scene

**Files:** Create `src/games/maplab/maplab_scene.{hpp,cpp}`; Modify `CMakeLists.txt`, `src/main.cpp`

- [ ] **Step 1 — header** `maplab_scene.hpp`: members `fps::Map map_`, `ui::Context ui_`,
  `uint8_t brush_ = 1`, `bool fill_mode_ = false`, `int save_counter_ = 0`,
  `std::vector<std::string> collection_`, `int w_,h_`; methods `save()`, `load(name)`,
  `cell_at(mx,my,int&,int&)` (screen→cell), draw/interact in `render`.
- [ ] **Step 2 — cpp:** palette Floor/Wall/Room/Pillar (ids 0..3 with editor colours); compute
  cell pixel size from canvas minus a left gutter (160px); draw cells + grid + hovered highlight;
  left-drag `set_cell(map_, cx, cy, brush_)` (Paint) or press → `flood_fill` (Fill); UI panel with
  swatches, Paint/Fill toggle, New (`map_ = maplab::bordered(24,16)`), Save, collection Load list.
  Save: `assets::write_file("maps/level_%02d.map", to_text(map_))`. Load: `from_text(load_file)`.
- [ ] **Step 3 — CMake/main:** add `maplab_scene.cpp` to demo sources, `maplab_core` to demo link;
  `--maplab` branch in main.cpp (960×600, smooth, highdpi, supersample=kAA) + include.
- [ ] **Step 4 — build:** `cmake --build build` clean; `ctest` all green (scene isn't headless-tested).
- [ ] **Step 5 — commit:** `feat(maplab): --maplab tile editor (paint/fill/palette/save-load)`

### Task 4: fps consumes the Lab map + seed

**Files:** Modify `src/games/fps/raycast_scene.cpp`; Seed `assets/maps/level_00.map`

- [ ] **Step 1 — raycast_scene.cpp:** replace `map_(default_map())` with a helper that tries the
  asset: `map_(load_level())` where
  `static fps::Map load_level(){ auto b = assets::load_file("maps/level_00.map"); if(b){ auto m = fps::from_text(std::string(b->begin(),b->end())); if(m) return *m; } return fps::default_map(); }`
  (add `#include "engine/assets.hpp"`).
- [ ] **Step 2 — seed:** headless step writes `assets/maps/level_00.map` from a crafted `fps::Map`
  (a room-and-corridors level); verify `from_text` reloads it.
- [ ] **Step 3 — build + run all tests** green; commit `feat(fps): load a Lab map (maps/level_00.map) with default fallback` + seeded asset.

### Task 5: docs + merge

- [ ] Guidebook `docs/book/78-map-level-lab.md` (reuse-the-model decision, dense grid, flood fill,
  shared serializer, the produce→consume join, deferrals).
- [ ] README roadmap row + `--maplab` run-list line.
- [ ] ASan/UBSan: `cmake --build build-asan && ./build-asan/test_fps && ./build-asan/test_maplab`.
- [ ] `git checkout main && git merge --no-ff feat/map-level-lab`; update memory checkpoint.

## Self-review

- **Spec coverage:** §4 format→T1, §5 edit→T2, §6 editor→T3, §7 consume + §8 seed→T4, §10 tests→T1/T2.
- **Type consistency:** `to_text(const Map&)`/`from_text(const std::string&)→optional<Map>` identical
  in map.hpp, map.cpp, test_fps, both scenes; `flood_fill/fill_rect/set_cell/bordered` signatures
  match edit.hpp/edit.cpp/test/scene; `fps::Map` is the single model everywhere.
- **Placeholders:** none — every code step is complete.

# Textured Sprites — Implementation Plan

> **For agentic workers:** TDD, one behaviour per commit. Steps use `- [ ]`.

**Goal:** Sandbox actors can wear Texture Lab `.hrt` textures (flat colour stays the default).

**Architecture:** A texture is a *name* on the pure `Sprite`/`Archetype` (core stays IO-free);
the SDL-touching scene probes `textures/studio_NN.hrt`, caches decoded images, and blits them
scaled to each actor's body rect via a new `Renderer2D::blit_scaled`. Serializer round-trips a
`tex=` token. Deferrals: rotated blit, dir-scan discovery, tinting (see spec §2).

**Tech Stack:** C++20, `ecs::Registry`, `gfx::load_image`/`Image`/`Sprite`, `ui::Context`.

---

### Task 1: `tex` field on core structs + spawn copy

**Files:** Modify `src/games/sandbox/world.hpp`, `src/games/sandbox/world.cpp`, Test `tests/test_sandbox.cpp`

- [ ] **Step 1 — failing test** (append to test_sandbox.cpp, call from `main`):

```cpp
static void test_spawn_copies_texture() {
    World w; Archetype a; a.texture = "studio_03";
    ecs::Entity e = w.spawn(a, 0, 0);
    CHECK(w.reg.get<Sprite>(e)->texture == "studio_03");
    // untextured default is empty
    Archetype b; ecs::Entity e2 = w.spawn(b, 0, 0);
    CHECK(w.reg.get<Sprite>(e2)->texture.empty());
}
```

- [ ] **Step 2 — implement:** add `std::string texture;` to `struct Sprite` and `struct Archetype`
  in world.hpp; in `world.cpp` change the sprite add to `reg.add<Sprite>(e, {a.color, a.round, a.texture});`
- [ ] **Step 3 — run:** `ctest --test-dir build -R sandbox` → PASS
- [ ] **Step 4 — commit:** `feat: sandbox Sprite/Archetype carry a texture name`

### Task 2: serializer `tex=` token

**Files:** Modify `src/games/sandbox/serialize.cpp`, Test `tests/test_sandbox.cpp`

- [ ] **Step 1 — failing tests:**

```cpp
static void test_archetype_codec_texture() {
    Archetype a; a.texture = "studio_07"; a.mover = true; a.vx = 5;
    Archetype b = parse_archetype(archetype_tokens(a));
    CHECK(b.texture == "studio_07" && b.mover && b.vx == 5);
}
static void test_scene_roundtrip_texture() {
    World w; Archetype a; a.texture = "studio_02"; w.spawn(a, 10, 20);
    const std::string s = to_scene(w);
    CHECK(s.find("tex=studio_02") != std::string::npos);   // token present
    CHECK(to_scene(from_scene(s)) == s);                    // round-trips
    // untextured actor emits NO tex= token (format unchanged)
    World u; Archetype b; u.spawn(b, 0, 0);
    CHECK(to_scene(u).find("tex=") == std::string::npos);
}
```

- [ ] **Step 2 — implement:** in `archetype_tokens`, after the existing fields:
  `if (!a.texture.empty()) s += " tex=" + a.texture;`
  in `parse_archetype` loop, add branch: `else if (k == "tex") a.texture = v;`
  (`to_scene` already rebuilds the Archetype from `Sprite`; add `a.texture = s->texture;` where it reads Sprite.)
- [ ] **Step 3 — run:** `ctest --test-dir build -R sandbox` → PASS
- [ ] **Step 4 — commit:** `feat: serializer round-trips the tex= token`

### Task 3: `Renderer2D::blit_scaled`

**Files:** Modify `src/engine/renderer2d.hpp`, `src/engine/renderer2d.cpp`, Test `tests/test_aa.cpp`

- [ ] **Step 1 — failing test** (add a block in test_aa.cpp `main`):

```cpp
// --- blit_scaled: upscale fills, transparent texel skipped, downscale samples ---
{
    constexpr int W = 8, H = 8, BGc = 0xFF000000;
    std::vector<std::uint32_t> buf(W * H, BGc);
    platform::Framebuffer fb{buf.data(), W, H, W};
    Renderer2D r(fb, 1);
    std::uint32_t src4[4] = {0xFFFF0000, 0x00000000, 0xFF00FF00, 0xFF0000FF};  // TL red, TR clear
    gfx::Sprite s{src4, 2, 2};
    r.blit_scaled(s, 0, 0, 4, 4);                       // 2x2 -> 4x4
    CHECK(buf[0] == 0xFFFF0000u);                        // TL red block
    CHECK(buf[2] == BGc);                                // TR texel transparent -> bg shows
    CHECK(buf[2 * W + 0] == 0xFF00FF00u);               // BL green block (y=2)
    CHECK(r.width() == 8);                               // (sanity: ss=1)
}
```

- [ ] **Step 2 — declare** in renderer2d.hpp after `void blit(...)`:
  `void blit_scaled(const Sprite& s, int dx, int dy, int dw, int dh);  // nearest-neighbour resample`
- [ ] **Step 3 — implement** in renderer2d.cpp (near `blit`):

```cpp
void Renderer2D::blit_scaled(const Sprite& s, int dx, int dy, int dw, int dh) {
    if (!s.pixels || dw <= 0 || dh <= 0 || s.w <= 0 || s.h <= 0) return;
    for (int oy = 0; oy < dh; ++oy) {
        const int sy = oy * s.h / dh;
        for (int ox = 0; ox < dw; ++ox) {
            const int sx = ox * s.w / dw;
            const Color src = s.pixels[sy * s.w + sx];
            if (a_of(src) == 0) continue;
            const int bx = (dx + ox) * ss_, by = (dy + oy) * ss_;
            for (int py = 0; py < ss_; ++py)
                for (int px = 0; px < ss_; ++px) blend_cov(bx + px, by + py, src, 255);
        }
    }
}
```

- [ ] **Step 4 — run:** `ctest --test-dir build -R aa` → PASS
- [ ] **Step 5 — commit:** `feat: Renderer2D::blit_scaled (nearest-neighbour)`

### Task 4: scene — probe, cache, blit path, inspector button

**Files:** Modify `src/games/sandbox/sandbox_scene.hpp`, `src/games/sandbox/sandbox_scene.cpp`

- [ ] **Step 1 — header:** add includes `<unordered_map>`, `"engine/image.hpp"`; add members
  `std::unordered_map<std::string, gfx::Image> tex_;  std::vector<std::string> tex_names_;`
  and a method `void load_textures();`
- [ ] **Step 2 — load_textures()** (probe range, cross-platform):

```cpp
void SandboxScene::load_textures() {
    for (int i = 0; i < 32; ++i) {            // ponytail: fixed range = Lab's studio_NN naming; dir-scan later
        char name[16]; std::snprintf(name, sizeof(name), "studio_%02d", i);
        auto img = gfx::load_image(std::string("textures/") + name + ".hrt");
        if (img) { tex_names_.push_back(name); tex_[name] = std::move(*img); }
    }
}
```

- [ ] **Step 3 — call it** in the `if (!inited_)` block in `render()` (after bounds set).
- [ ] **Step 4 — draw path:** replace the flat-fill lines in the draw view with:

```cpp
auto it = s.texture.empty() ? tex_.end() : tex_.find(s.texture);
if (it != tex_.end()) {
    gfx::Sprite spr{it->second.pixels.data(), it->second.w, it->second.h};
    g.blit_scaled(spr, cx - dw / 2, cy - dh / 2, dw, dh);
} else if (s.round) g.fill_circle(cx, cy, dw / 2, s.color);
else                g.fill_rect(cx - dw / 2, cy - dh / 2, dw, dh, s.color);
```

- [ ] **Step 5 — inspector button** (after Recolor, before Delete):

```cpp
if (!tex_names_.empty()) {
    Sprite* s = world_.reg.get<Sprite>(sel_);
    char lbl[32]; std::snprintf(lbl, sizeof(lbl), "Tex: %s", s->texture.empty() ? "none" : s->texture.c_str());
    if (ui_.button(lbl)) {
        // cycle none -> names[0] -> ... -> none
        int idx = -1;
        for (size_t k = 0; k < tex_names_.size(); ++k) if (tex_names_[k] == s->texture) { idx = int(k); break; }
        ++idx;
        s->texture = (idx >= int(tex_names_.size())) ? std::string() : tex_names_[size_t(idx)];
    }
}
```

- [ ] **Step 6 — build + run demo smoke:** `cmake --build build` clean; `ctest` all green.
- [ ] **Step 7 — commit:** `feat: --sandbox blits Texture Lab textures on actors`

### Task 5: docs + merge

- [ ] Guidebook `docs/book/77-textured-sprites.md` (the join, probe rationale, blit_scaled, deferrals).
- [ ] README roadmap row.
- [ ] ASan/UBSan: `cmake --build build-asan && ./build-asan/test_sandbox && ./build-asan/test_aa`.
- [ ] `git checkout main && git merge --no-ff feat/sandbox-textured-sprites`.
- [ ] Update memory checkpoint.

## Self-review

- **Spec coverage:** §4 data→T1, §5 serializer→T2, §8 blit→T3, §6/§7/§9 scene→T4, §10 tests→T1-3+T3.
- **Type consistency:** `Sprite.texture`/`Archetype.texture` (std::string) used identically in
  world.cpp, serialize.cpp, scene; `blit_scaled(const Sprite&,int,int,int,int)` signature matches
  header, impl, test. `tex_`/`tex_names_` names consistent across header + cpp.
- **Placeholders:** none — every code step is complete.

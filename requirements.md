# Requirements — Game Engine (C/C++) + Game Collection

> Tài liệu này để đưa cho coding agent (Claude Code / Codex) thực thi. Đọc kỹ phần **Triết lý & Ràng buộc** trước khi sinh code, vì các ràng buộc về web-portability ảnh hưởng tới kiến trúc ngay từ Milestone 0.

---

## 1. Tổng quan & mục tiêu

Xây một **game engine viết tay bằng C/C++** và một **bộ sưu tập game + công cụ visualize** chạy trên engine đó. Thứ tự ưu tiên:

1. **Trước hết:** xây dựng game engine hoàn chỉnh (có document hướng dẫn từng bước đầy đủ, cùng làm với nhau)
2. **Sau đó:** tiến hành áp dụng game engine với game chess, (tôi muốn chess này chạy được 2 mode, 1 là GUI 2 là TUI)
3. **Sau đó:** mở rộng sang **FPS kiểu raycaster (Wolfenstein)**, **3D core thật sự**, và **isometric sim**.
4. **3D là trụ cột:** engine phải có **renderer 3D thật** (mesh, camera, sinh khối 3D, visualize), tái dùng được cho cả game lẫn các tool visualization về sau — KHÔNG phải demo dùng một lần.
5. **Cuối cùng:** port lên **web (WebAssembly)** mà không phải viết lại logic.

**Mục tiêu xuyên suốt và quan trọng nhất: HỌC ĐỂ HIỂU SÂU VÀ TỰ LÀM ĐƯỢC.** Vì vậy mọi phần lõi (renderer 2D + 3D, math, game logic, AI, geometry) đều **tự viết tay**, không dùng game framework/engine dựng sẵn. Khi có lựa chọn giữa "tiện" và "học được nhiều hơn", ưu tiên cái dạy được nhiều hơn — miễn là vẫn ra sản phẩm chạy được.

## 2. Triết lý & ràng buộc

- **Tự viết phần lõi để học:** math library, software renderer, rasterizer, game logic, AI — viết tay, KHÔNG dùng thư viện game engine có sẵn (không Unity/Godot/raylib-as-engine).
- **SDL2 là lớp nền MỎNG duy nhất được phép dùng**, và chỉ dùng cho: tạo window, lấy framebuffer/đẩy pixel lên màn hình, input thô, audio thô, timer. **KHÔNG** dùng các hàm vẽ cao cấp của SDL (SDL_Renderer primitives, SDL_image, v.v.) — mọi thứ vẽ phải tự viết vào framebuffer.
- **Web-portability bake sẵn từ M0** (xem mục 8). Cụ thể:
  - **Platform layer phải cô lập** sau một interface (`platform.h`), để sau thêm backend web là plug-in, không sửa code game.
  - **Game loop phải thiết kế dạng "tick function"** — một frame = một lời gọi hàm `engine_tick(dt)`, KHÔNG dùng `while(true)` blocking ở tầng cao. Lý do: web (Emscripten) yêu cầu `emscripten_set_main_loop`, không cho phép vòng lặp blocking.
  - **I/O asset đi qua abstraction** (`asset_load(path)`), không gọi `fopen` rải rác — vì web dùng virtual filesystem.
  - **Single-threaded trước**, không giả định threading (pthreads trên web cần flag đặc biệt).
- **Ngôn ngữ:** C/C++ tùy ý, không ràng buộc "C trước C++ sau".
- **Build:** dùng **CMake** để cùng một codebase build được desktop (native) và web (Emscripten) qua toolchain file.

## 3. Tech stack

| Thành phần             | Lựa chọn                                                                                                                                               |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Ngôn ngữ               | C/C++ (C++17 trở lên nếu dùng C++)                                                                                                                     |
| Platform layer         | SDL2 (chỉ window/input/audio/framebuffer/timer)                                                                                                        |
| Render (giai đoạn đầu) | **Software renderer tự viết** (CPU, ghi vào pixel buffer) — 2D và 3D                                                                                   |
| Render 3D              | **Software rasterizer tự viết** (tam giác + z-buffer + perspective). Chuyển sang OpenGL ES 3.0 (WebGL2-compatible) ở giai đoạn sau khi cần performance |
| Geometry               | Tự viết: mesh (vertex/index), sinh khối primitive (cube/sphere/plane/grid), transform                                                                  |
| Camera                 | Tự viết: orbit camera (cho visualize) + free/FPS camera (cho game)                                                                                     |
| Build system           | CMake                                                                                                                                                  |
| Web target             | Emscripten (WASM + SDL2 port của Emscripten)                                                                                                           |
| Math                   | Tự viết (vec2/3/4, mat4)                                                                                                                               |

## 4. Kiến trúc tổng thể

Engine chia thành các module rõ ràng, game KHÔNG được gọi trực tiếp xuống platform/SDL:

```
[ Games + Tools: chess / fps / iso / 3d-viz ]   <- chỉ gọi Engine API
            |
[ Engine core ]
   - app/loop      (game loop dạng tick, fixed timestep)
   - renderer2d    (software framebuffer: pixel/line/rect/sprite/text)
   - renderer3d    (software rasterizer: tam giác, z-buffer, perspective)
   - geometry      (mesh, sinh primitive cube/sphere/plane/grid, wireframe+solid)
   - camera        (orbit camera + free/FPS camera)
   - math          (vec, mat, trig helpers, quaternion nếu cần)
   - input         (trạng thái phím/chuột đã chuẩn hóa)
   - audio         (play sound/music)
   - assets        (load qua abstraction)
   - scene/state   (quản lý màn hình, chuyển state)
            |
[ Platform layer ]  (interface cố định: platform.h)
   - backend_sdl   (desktop)
   - backend_web   (Emscripten, thêm ở M5 — KHÔNG sửa tầng trên)
```

**Quy tắc bất biến:** code game và engine core chỉ phụ thuộc `platform.h`. Đổi backend (SDL desktop ↔ Emscripten web) không được động vào engine core hay game. **renderer2d và renderer3d cùng ghi vào một framebuffer** — nên 3D port lên web dễ như 2D (đẩy buffer lên canvas).

## 5. Cấu trúc thư mục đề xuất

```
/CMakeLists.txt
/cmake/emscripten.toolchain.cmake
/src
  /platform
    platform.h            # interface cố định
    backend_sdl.c/.cpp
    backend_web.c/.cpp     # thêm ở M5
  /engine
    app.c        loop + tick + fixed timestep
    renderer2d.c framebuffer, draw pixel/line/rect/blit/text
    renderer3d.c rasterize tam giác, z-buffer, perspective, wireframe+solid
    geometry.c   mesh, sinh primitive (cube/sphere/plane/grid), transform
    camera.c     orbit camera + free/FPS camera
    math.c       vec2/3/4, mat4, quaternion (nếu cần)
    input.c
    audio.c
    assets.c
  /games
    /chess
    /fps
    /iso
    /viz3d       # tool visualize / sandbox tạo khối 3D
  main.c          # chọn game, khởi tạo engine
/assets
/build            # (gitignore)
```

## 6. Lộ trình Milestone

| MS       | Nội dung                                                                                                                                                                                                                           | Deliverable                                                              |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| **M0**   | Engine foundation: window+framebuffer qua SDL, game loop tick + fixed timestep, math lib, draw cơ bản (pixel/line/rect/blit sprite/text bitmap), input                                                                             | Cửa sổ mở, vẽ được hình + chữ, FPS counter chạy                          |
| **M1**   | **Chess (desktop)** — game đầu tiên chạy được                                                                                                                                                                                      | Chơi được người-vs-người + AI minimax/alpha-beta                         |
| **M2**   | FPS raycaster (Wolfenstein-style) — bước đệm rẻ để hiểu phép chiếu trước khi vào 3D thật                                                                                                                                           | Đi lại trong mê cung có texture + sprite enemy                           |
| **M3**   | **3D core thật sự (trụ cột):** software rasterizer (tam giác + z-buffer + perspective), mesh, transform pipeline (model/view/projection), camera (orbit + free), sinh primitive (cube/sphere/plane/grid), chế độ wireframe + solid | Render & xoay được mesh 3D có chiều sâu đúng; load/sinh được khối cơ bản |
| **M3.5** | **3D visualization sandbox (`viz3d`):** dùng 3D core để tạo/đặt/biến đổi khối 3D, xoay-zoom-pan bằng chuột, hiển thị grid/axis — nền cho các tool visualize sau này                                                                | Tạo & thao tác khối 3D tương tác được                                    |
| **M4**   | Isometric sim (farm hoặc workspace builder)                                                                                                                                                                                        | Đặt tile, depth-sort đúng, A\* pathfinding, save/load                    |
| **M5**   | **Web port** qua Emscripten                                                                                                                                                                                                        | Chess (tối thiểu) chạy trong browser, không sửa engine core              |

**M0 + M1 là phần phải làm trước và làm kỹ.** Riêng **M3 (3D core) là trụ cột bắt buộc** vì mọi việc visualize/tạo khối 3D về sau đều dựa vào nó — nếu visualization là ưu tiên của bạn, có thể đẩy M3 lên ngay sau M1. Các milestone còn lại là mở rộng.

## 7. Yêu cầu chi tiết — M0 & M1 (ưu tiên)

### M0 — Engine foundation

- `platform.h` định nghĩa interface: init/shutdown, lấy framebuffer (con trỏ + width/height/pitch), present (đẩy buffer lên màn hình), poll input, thời gian (delta time), audio cơ bản.
- `backend_sdl`: hiện thực `platform.h` bằng SDL2. Framebuffer là mảng pixel ARGB/RGBA, present bằng cách upload lên 1 SDL_Texture rồi blit (đây là cách duy nhất dùng SDL_Renderer cho phép — chỉ để present buffer, không vẽ primitive bằng SDL).
- Game loop: **fixed timestep** (ví dụ update ở 60Hz cố định, render tách riêng), được gói trong hàm `engine_tick()` để M5 gắn vào Emscripten dễ dàng.
- Renderer (software, tự viết): `clear`, `set_pixel`, `draw_line`, `draw_rect`/`fill_rect`, `blit_sprite` (có alpha), `draw_text` (bitmap font tự nhúng). Có clipping cơ bản.
- Math: `vec2/vec3/vec4`, `mat4`, các phép cơ bản (cộng/trừ/nhân/dot/cross/normalize, mat4 identity/translate/rotate/scale/**perspective/lookat/viewport**). Đây là nền cho cả 2D lẫn 3D core ở M3 — viết chắc tay từ đầu.
- Input: chuẩn hóa trạng thái phím (down/pressed/released) + chuột (vị trí, nút). Game đọc qua engine, không đọc SDL trực tiếp.
- **Acceptance:** mở cửa sổ, vẽ vài hình + text + FPS counter, đóng cửa sổ sạch sẽ, không memory leak.

### M1 — Chess (desktop)

- Vẽ bàn cờ 8x8 + quân (sprite tự vẽ/bitmap) hoàn toàn bằng software renderer.
- Luật đầy đủ: nước đi từng quân, nhập thành, bắt tốt qua đường (en passant), phong cấp, chiếu, chiếu hết, hòa cờ (stalemate). Highlight nước đi hợp lệ.
- Điều khiển chuột để chọn & di chuyển quân.
- **AI: minimax + alpha-beta pruning** tự viết, có ít nhất 2–3 mức độ khó (theo độ sâu tìm kiếm), hàm lượng giá vật chất + vị trí cơ bản.
- Chế độ: Người-vs-Người và Người-vs-AI.
- (Tùy chọn để sau) lưu/tải ván cờ.
- **Acceptance:** chơi trọn ván đúng luật với AI, không crash, không nước đi sai luật.

## 8. Yêu cầu cho các game sau (tóm tắt)

- **M2 FPS raycaster:** bản đồ lưới 2D, raycasting theo từng cột pixel, texture mapping tay, sprite enemy (billboarding), collision trên lưới, audio bắn/bước chân. Real-time, fixed timestep. Đây là bước đệm hiểu phép chiếu **trước** khi vào 3D thật ở M3.
- **M3 3D core (trụ cột):** tự viết **rasterize tam giác có z-buffer**, **perspective projection**, **transform pipeline** đầy đủ (model → view → projection → viewport). `geometry`: cấu trúc mesh (vertex + index), hàm **sinh primitive** (cube, sphere, plane, grid). `camera`: **orbit camera** (xoay quanh tâm, dùng cho visualize) và **free/FPS camera**. Hỗ trợ cả **wireframe** và **solid** (flat/Gouraud shading cơ bản). Backface culling. Vẽ trục toạ độ + grid để định hướng. Mục tiêu: đây là subsystem **dùng lại được lâu dài**, không phải demo.
- **M3.5 viz3d sandbox:** dùng M3 để **tạo, đặt, biến đổi (translate/rotate/scale) các khối 3D** trong không gian; điều khiển camera xoay/zoom/pan bằng chuột; bật/tắt wireframe, grid, axis. Đây là nền cho các công cụ visualization bạn muốn làm sau. **Acceptance:** tạo và thao tác được vài khối 3D tương tác mượt, camera điều khiển trực quan.
- **M4 isometric sim:** tile map, **depth sorting** chuẩn, **A\*** pathfinding, entity management (cân nhắc ECS nhỏ khi entity tăng), UI đặt/chọn object, save/load qua serialization. Chủ đề khởi đầu: **farm sim** hoặc **workspace builder**.

## 9. Non-functional / quy tắc kỹ thuật

- **Web-ready ngay từ M0:** không vòng lặp blocking ở tầng cao; mọi I/O qua `assets`; single-threaded; không gọi SDL ngoài backend.
- **Build cả 2 target:** CMake desktop (native + SDL2) và web (Emscripten toolchain, dùng SDL2 port của Emscripten qua flag `-s USE_SDL=2`). Software renderer port lên web bằng cách đẩy framebuffer lên `<canvas>`.
- **Performance:** M0–M1 giữ 60 FPS thoải mái; renderer tránh cấp phát mỗi frame.
- **Code style:** module tách bạch, header gọn, không phụ thuộc vòng. Game → Engine → Platform một chiều.
- **Không leak:** quản lý lifetime rõ ràng; nếu C++ thì RAII, nếu C thì init/shutdown từng module.

## 10. Acceptance tổng

- [ ] M0: cửa sổ + software render + input + loop tick chạy ổn, không leak.
- [ ] M1: chess chơi được đúng luật vs AI alpha-beta, desktop.
- [ ] M3: **3D core thật sự** — render/xoay mesh có z-buffer + perspective đúng, sinh được primitive, camera orbit + free hoạt động (subsystem tái dùng được, không phải demo).
- [ ] M3.5: tạo & thao tác khối 3D tương tác được trong sandbox visualize.
- [ ] Kiến trúc cho phép thêm backend web ở M5 **mà không sửa** engine core/game.
- [ ] CMake build được native; cấu trúc sẵn sàng cho Emscripten.

---

### Ghi chú cho coding agent

Bắt đầu bằng **M0 rồi M1**, dừng lại cho review trước khi sang M2. Ở M0, ưu tiên dựng đúng `platform.h` interface và game loop dạng tick — đây là quyết định kiến trúc khó sửa về sau và là chìa khóa để port web. Đừng tối ưu sớm; ưu tiên code rõ ràng, dễ học.

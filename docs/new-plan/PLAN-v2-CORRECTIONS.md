# Đính chính PLAN v2 — đối chiếu code, 2026-09-06

`PLAN-v2.md` được viết ngày 5/9 và **tự ghi ở dòng 6**:

> *"GitHub chặn duyệt thư mục/raw → **tôi chưa đọc** `src/engine/ui/*`, `theme.hpp`,
> `studio_shell_scene.cpp`, `farm_scene.cpp`, `baas/dashboard`. Mục nào suy từ cấu trúc
> chứ không từ code, tôi đánh `[cấu trúc]`."*

Và §7 của nó là một checklist shell **để chạy trước khi bắt đầu T1**. File này là kết quả
của việc chạy đúng checklist đó, cộng vài mục nữa, trên cây code tại `main` sau chương
127.

**Kết luận một dòng:** T1 và T2 — hai slice PLAN v2 xếp đầu tiên và ước lượng M + L — **đã
xong khoảng 85%**. Làm theo nguyên văn sẽ viết lại một lớp UI đã tồn tại và đã có test.
Đổi lại, có ba khoảng trống PLAN v2 không nêu, và vài mục nó cho là nhỏ thì lớn hơn.

Đây là **lần thứ hai** cùng một chuyện xảy ra: `SPEC.md` §0 đã là một bản đính chính cho
`PLAN.md`, cũng vì lý do y hệt. Bài học ghi ở đây một lần: **một kế hoạch viết mà không
đọc `src/` sẽ sai ở chỗ "cái gì đã tồn tại", không sai ở hướng.** Hướng của cả hai bản
đều đúng.

---

## 1. Đã có rồi — PLAN v2 giả định "chưa có"

| PLAN v2 nói thiếu | Thực tế | Bằng chứng |
|---|---|---|
| T1 · UTF-8, `publish???dev` | **có** — glyph theo codepoint, decoder riêng, tofu thay `'?'` | `src/engine/text/utf8.hpp`; `Font::glyph(int, char32_t)` `font.hpp:54`; cache `unordered_map<char32_t,Entry>` `font.cpp:35`; `utf8_next` `font.cpp:147` |
| T1 · clip stack | **có** | `renderer2d.hpp:79-80` `push_clip` / `pop_clip` |
| T1 U2 · "chữ Studio bị cắt phần trên" | **đã sửa** — nguyên nhân thật không phải baseline mà là `--shell`/`--hub-ui` thiếu `cfg.supersample` | ghi trong `SPEC.md` §0.3, đã áp dụng |
| T1 U3 · "shadow panel là rect đen lệch" | **sai** — soft shadow nhiều lớp alpha, `ui::panel` gọi đúng | `renderer2d.hpp:68` `drop_shadow`; `ui.cpp` `Context::panel` |
| T2 · layout engine | **có** — cursor một trục, slot/slot_end/slot_rest/cell/skip, stack sâu 8 | `ui.hpp:113-120`; impl `ui.cpp:244-320` |
| T2 · tooltip / toast / modal confirm | **có** — và `confirm` đã **bắt buộc nhập reason** | `ui.hpp:141,145,160` |
| T2 · text_input + clipboard qua seam | **có** — caret/selection theo biên UTF-8, clipboard là hai `std::function` tiêm vào | `ui.hpp:169,173`; `platform.hpp:94-95`; `input.hpp:75` |
| T2 · id stack, focus, Tab nav, focus ring | **có** | `ui.hpp:85-87,193-197`; `tab_order_` trong `ui.cpp::end()` |
| T2 · badge / tabs / list_item / scroll | **có** | `ui.hpp:134,179,183,186` |
| T2 · platform seam thiếu cursor/wheel/mods/resizable/text | **có đủ** | `platform.hpp:47` `resizable`, `:88` `set_cursor`, `:94-95` clipboard; `input.hpp:58` `mods`, `:68` `wheel_x/y`, `:75` `text[kTextMax]` |
| U4 · "Hub là dòng text thô" | **sai** — ba card kênh, rail 3px màu kênh, badge `in sync` / `behind` / `MISSING` / `unset` | `hub_panel.cpp:63` `card()`, `:167` ba kênh, `:188` badge, `:27-30` bảng trạng thái |
| U6 · publish/promote không confirm | **có** | `studio_shell_scene.cpp:539,550` `ui_.confirm("hubop", …)` |
| U14 · không toast cho `OpResult` | **có** | `workspace_host.cpp` flash; `studio_shell_scene.cpp` toast |
| T6 · "CI chỉ cài SDL2, không build web" | **sai** — đã có job `web-build` (emsdk) **và** assert bundle không rò state máy | `.github/workflows/ci.yml:127-150` |
| §0.1 `tilemap_core` | **đúng, đủ** | `src/engine/tilemap/{map2,tileset,camera2d,autotile,map_edit}.cpp` |

**Hệ quả cho lộ trình:** T1 gần như rỗng. T2 còn **đúng hai món**: `splitter()` + lưu
layout, và status bar dạng segment (hôm nay `Workspace::status()` trả về một câu nối
chuỗi). Cả hai là nợ nhỏ → gom vào slice dọn cuối, không phải slice mở màn.

---

## 2. Thiếu thật — giữ nguyên, hoặc lớn hơn PLAN v2 nghĩ

| Món | Trạng thái | Bằng chứng |
|---|---|---|
| **Touch trên web** | **0 dòng** trong `shell.html`; không set SDL touch hint ở nhánh Emscripten | `grep -c "touch\|pointer" web/shell.html` = 0; không có `SDL_HINT_TOUCH_MOUSE_EVENTS` trong `backend_sdl.cpp` |
| **Trang web vẫn là debug shell** | font mono, panel `#log` luôn hiện, 123 dòng | `web/shell.html:22,32-33,41` |
| **CI không chạy 28 test BaaS** | không có Drogon trong CI → cả bộ **tối ở CI** (chúng vẫn chạy ở máy này) | `ctest -N` = **76**; cấu hình lại với `-DCMAKE_DISABLE_FIND_PACKAGE_Drogon=ON` (đúng cái CI thấy) = **48**. Hiệu số là 28, không phải 23 — 23 là số *file* `test_baas_*.cc`, không phải số test đăng ký |
| **`fpsmap1` vẫn sống** | hai định dạng map song song | `fps/map.{hpp,cpp}`, `raycast_scene.cpp`, `map_workspace.hpp`, `main.cpp`, `assets/maps/level_00.map` |
| **Số lab là 13, không phải 9** | `scene pixel map texture editor fx light audio anim 3d viz3d iso colony` | `labs()` trong `src/main.cpp` |
| **IntGrid + rule engine** | không tồn tại | `grep -rn intgrid src/` = rỗng |
| **`.pack` + attribution tự sinh** | `ATTRIBUTION.md` gõ tay — nó đang là *một luật phải nhớ* trong `CLAUDE.md` | `assets/ATTRIBUTION.md`; không có format `.pack` |
| **Mixer** | không tồn tại | `grep -rn mixer src/` = rỗng |
| **Tạo file / asset mới** | workspace chỉ sửa được texture manifest **đã khai** | trần ghi ở chương 127 |
| **`splitter` + lưu layout** | không tồn tại | chỉ một comment ở `platform.hpp:84` |
| **ADR** | không có `docs/adr/` | `ls docs/` |
| **Farm content mỏng hơn brief gợi ý** | **3 crop**; trường `season` được parse rồi **không call site nào đọc** | `assets/farm/crops.def`; `defs.cpp:68,103` ghi `Crop::season`, `grep -rn season src/games/farm` không có nơi dùng |
| Game 2 (Creatures), PvP realtime | chưa có | — |

---

## 3. Mặt bằng đã đổi từ ngày PLAN v2 được viết (5/9)

PLAN v2 nói *"chapter tiếp từ số hiện tại (≥124 — kiểm tra trong repo)"*. Bốn chương đã
vào từ lúc đó, và hai trong số chúng đổi thứ tự ưu tiên:

| Chương | Nội dung | Ảnh hưởng lên PLAN v2 |
|---|---|---|
| 124 | Điều khiển trên màn hình cho farm (d-pad + 2 nút) | tiền đề của T6 |
| 125 | `.pix` — cửa thứ ba vào `.hrt`, art tự vẽ; farm path autotile 16 mảnh | T5 chỉ còn thiếu IntGrid + rule, không thiếu autotile |
| 126 | **Mọi động từ với tới được bằng ngón tay**; tìm ra một khoá cứng khi nói chuyện với NPC | T6 giờ là bước *tiếp theo tự nhiên*, không phải bước xa |
| 127 | Mixer màu HSV/hex trong Pixel workspace — mở trần "chỉ tô được màu ảnh đã có" | trần còn lại đúng một cái: **không tạo được file mới** |

Cộng lại: **T6 (web) đã chín, và nó là điểm dừng show được mà chính PLAN v2 §0.11 đặt
ra** — trong khi T1/T2 thì đã qua.

---

## 4. Thứ tự thay thế cho §4 của PLAN v2

Giữ nguyên toàn bộ *nội dung* T3–T10; chỉ bỏ T1/T2 và xếp lại. Nguyên tắc: (a) có người
tiêu thụ ngay trước hạ tầng đẹp; (b) đóng chỗ **tối** (test không chạy, claim chưa xác
minh) sớm; (c) nợ kiến trúc trước tính năng mới; (d) rủi ro cao nhất sau cùng.

| # | Slice | ~ PLAN v2 | Size |
|---|---|---|---|
| S19 | Trang web thật + **chứng minh chạm** | T6 (nửa đầu) | M |
| S20 | CI chạy 28 test BaaS (container Drogon) | T9a | S |
| S21 | Collection page + `cover`/`summary` + README template | T6 (nửa sau) | S/M |
| S22 | Tạo asset mới + `.pack` + ATTRIBUTION tự sinh + asset card | T4 + trần ch.127 | M |
| S23 | Kết thúc migration map: hấp thụ Map Lab, giết `fpsmap1` | T3 (nửa đầu) | L |
| S24 | Hấp thụ 4 lab hiệu ứng thành component của Scene | T3 (nửa sau) | M |
| S25 | IntGrid + rule autotile trong Map workspace | T5 (nửa đầu) | L |
| S26 | Mixer workspace | T5 (nửa sau) | L |
| S27 | Creatures — game thứ hai (MVP) | T7 | XL |
| S28 | Replay + PvP realtime + ELO | T8 | L |
| S29 | OPS còn lại (Postgres **cùng** TOCTOU, OpenAPI, healthz) | T9 | M |
| S30 | Dọn nợ nhỏ + ADR chỉ mục | T2 phần dư | M |

Điểm dừng show được: **sau S21** (mở link trên điện thoại, chọn game, chơi) và **sau
S28** (hai game + PvP).

---

## 5. Trả lời §8 của PLAN v2

1. **Thứ tự** — web (T6) lên đầu, không phải T1/T2. Lý do ở §3.
2. **Xoá `iso` lab sau migration?** — chưa quyết ở S23; `iso` còn là game M4 có save/load
   riêng, nên khả năng cao chuyển thành `entry` trong manifest chứ không xoá.
3. **Tên game 2** — **Creatures** (đã chốt). Font pixel: chưa chốt, để S27.
4. **ADR** — có, nhưng ở S30 và ở dạng **chỉ mục trỏ vào chapter đã có**, không viết prose
   mới: 128 chương đã là hồ sơ quyết định, cái thiếu là mục lục theo *quyết định* thay vì
   theo *thời gian*.

---

## 6. Ghi chú phương pháp

Ba mục trong §1 (`U3`, `U4`, `T6 CI`) là những chỗ PLAN v2 **khẳng định có lỗi** dựa trên
ảnh chụp cũ hoặc suy luận từ `CMakeLists.txt`. Chúng không sai vì đọc thiếu — chúng sai vì
**bằng chứng đã cũ**. Nên quy ước cho mọi bản kế hoạch sau: mục nào dựa trên ảnh chụp phải
ghi **ngày chụp**, và một mục "đã sửa rồi" phải được kiểm lại bằng một khung render mới
trước khi thành việc.

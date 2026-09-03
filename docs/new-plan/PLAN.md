# PLAN — Cải tổ UI/UX và hoàn thiện game-development

**Ngày:** 2026-09-04 · **Trạng thái gốc:** `main` @ `81d3a81` (282 commits, 61 test suites xanh)
**Đọc kèm:** `SPEC.md` (spec kỹ thuật từng slice). File này trả lời *làm gì, theo thứ tự nào, xong khi nào*; `SPEC.md` trả lời *làm như thế nào*.

---

## 0. Tóm tắt cho người vội

Project đã có nền engine + platform spine + BaaS vững, nhưng **ba thứ đang kéo toàn bộ giá trị xuống**:

1. **UI/UX** ở mọi lớp (thư viện `ui`, Studio/Hub shell, HUD game, `shell.html`, dashboard BaaS) đang ở mức "in text ra màn hình": không có hệ thống thiết kế, font rasterize thô, không layout engine, tương tác chỉ có phím tắt, popup/shadow vẽ sai, glyph `→` hiện thành `???`.
2. **29 mode CLI rời rạc** — 12 Lab/playground là "motion without connection" (brief §7 đã tự nhận). Không có sản phẩm nào để show.
3. **Chỉ một game (`fps`) đi qua platform**. Không có game nào đủ hay để người ngoài muốn chơi.

Plan này xử lý theo đúng posture **blend** của `docs/strategy/04`: xen kẽ một slice *nền UI/plumbing* với một slice *runtime/game*, mỗi slice có test + chapter + tiêu chí xong. Kết quả cuối lộ trình:

- Một **Studio duy nhất** (thay cho `--studio/--sandbox/--maplab/--fx/--light/--anim/--audio/--editor/--hub-ui/--shell`) với design system riêng, undo/redo, asset browser, command palette, embedded Play, Release workspace.
- Một **lớp runtime dùng chung cho game 2D tile-based** (`tilemap_core`, `camera2d`, `scene_stack`, `dialogue_core`, `save_core`, `input_map`).
- **Hai game hoàn chỉnh** đi trọn golden path và tiêu thụ BaaS thật: **Farm** (Stardew-like, top-down) và **Creature RPG** (Pokémon-like, có PvP realtime). **Terraria-like** là stretch.
- **Web shell + Collection page + dashboard** đẹp, chơi được trên trình duyệt kể cả mobile, sẵn sàng mở open source.
- Đóng các ô ❌ trong ledger (Docker, CI, Postgres) ở mức ưu tiên thấp, xen vào khi cần "đổi gió".

---

## 1. Giả định & quyết định đã chốt (từ trao đổi)

| # | Quyết định | Lý do |
|---|---|---|
| A1 | **Studio/Hub chạy 1280×720 logical, integer-scale lên HiDPI.** Game giữ framebuffer riêng: 480×270 (fps, demo), **640×360** cho game 2D mới. | Screenshot cho thấy Studio đang chạy ở độ phân giải thấp rồi upscale → chữ to, thô, ít chỗ. Game retro thì thấp là đúng. |
| A2 | **100% CPU framebuffer, không GPU, không blur.** "Elevation" làm bằng border + 1–2 rect alpha. | Bạn xác nhận không cần GPU. Blur CPU ở 1280×720 mỗi frame là lãng phí. |
| A3 | **Không icon.** Thứ bậc thị giác làm bằng typography, màu, spacing, badge chữ. | Bạn yêu cầu. Cũng tránh phải vẽ/quản lý icon atlas. |
| A4 | **Game mới là top-down 2D tile-based**, không isometric. `iso_core` giữ lại làm projection tuỳ chọn của `tilemap_core`. | Stardew + Pokémon đều top-down → **một** tilemap runtime phục vụ cả hai game; asset pixel-art top-down ngoài cũng phổ biến nhất. |
| A5 | **Nới Rule 3 của brief** (không thêm game genre mới) *với điều kiện*: mỗi game mới là một `.gameproject`, đi qua `launch_entry`, và tiêu thụ ≥ 3 BaaS service. Không thêm `--flag` mới cho game. | Bạn bí ý tưởng game và cần sản phẩm show; điều kiện này giữ đúng tinh thần Rule 1. |
| A6 | **Chấp nhận asset pixel-art ngoài có license mở** (CC0/OFL), ghi vào `ASSETS-LICENSE.md`. Studio phải đủ tốt để *tự tạo* asset tương đương ở phase sau. | Bạn đồng ý. |
| A7 | **Xoá mode cũ sau khi Studio hấp thụ xong** — pure `*_core` lib và chapter giữ nguyên, chỉ scene wrapper + flag bị xoá. Chapter cũ thêm dòng "đã hấp thụ vào Studio ở ch. NN". | Bạn sẵn sàng consolidate; learning value nằm ở core + chapter, không ở flag. |
| A8 | **Deliverable: native + WASM song song.** Mọi slice phải build cả hai; WASM là kênh chia sẻ chính. | Bạn yêu cầu. |
| A9 | **Chapter mới viết tiếng Anh (khớp repo) + có mục "Tóm tắt tiếng Việt" ở đầu.** PLAN/SPEC này tiếng Việt. | Repo docs đang tiếng Anh; bạn học bằng tiếng Việt. |
| A10 | **Track C (Docker/CI/Postgres/TOCTOU) giữ trong plan, ưu tiên thấp**, chèn khi cần slice "plumbing" theo blend. | Bạn yêu cầu. |
| A11 | Ước lượng dùng **T-shirt size** (S/M/L/XL) thay vì tuần. | Bạn không quan tâm thời gian, chỉ quan tâm kết quả. |

Nếu giả định nào sai, sửa ở đây trước, rồi sửa SPEC.

---

## 2. Chẩn đoán UI/UX hiện tại (từ screenshot + code layout)

Tôi chưa đọc code `ui.cpp`, chỉ suy từ ảnh và brief. Những lỗi/thiếu **nhìn thấy được**:

**Studio shell (`--shell`)**
- Nav rail: chữ "Studio" bị **cắt phần trên** → bug tính baseline/ascent khi vẽ text hoặc clip rect sai.
- Hotkey bar: `publish???dev` → glyph `→` không có trong bảng glyph rasterize; không có fallback, không decode UTF-8 đúng.
- Toàn bộ nội dung Hub là **dòng text thô** (`development c95febd… [present, ==source]`). Người dùng phải *đọc* thay vì *nhìn*: không card, không badge, không màu cho channel, không nút bấm; hành động chính chỉ tồn tại dưới dạng phím tắt viết ở đáy màn hình.
- Không có trạng thái hover/focus/active; không tooltip; không confirm trước publish/promote (hành động không thể đảo — nguy hiểm).
- Một size chữ duy nhất (ngoài title). Không spacing scale. Contrast tạm ổn nhưng không có hierarchy.

**Sandbox (`--sandbox`)**
- Panel nổi có **shadow là một rect đen lệch** (vẽ solid rect offset, không alpha, không clip) → nhìn như lỗi render.
- Panel palette **trống khi Play** — mất ngữ cảnh, không có inspector, không có gì cho biết actor đang chọn là gì.
- Nút `Stop` màu xanh nhạt phẳng, không cùng ngôn ngữ với phần còn lại.
- Status bar là **một câu dài** ghép nhiều lệnh bằng dấu `-`.
- Actor là hình khối màu — chấp nhận được cho sandbox, nhưng không có outline chọn, không grid, không snap, không zoom/pan.

**Suy ra thiếu ở tầng thư viện `ui`**
- Không có **draw list phân lớp** (base / overlay / tooltip) → popup không thể đè đúng, shadow vẽ trực tiếp.
- Không có **layout engine** → mọi widget đặt bằng toạ độ tay → không responsive khi đổi resolution.
- Không có **id stack** ổn định cho widget stateful (text input, tree open state).
- Không có **theme/tokens**.
- Không có **glyph atlas** (mỗi frame rasterize lại? — cần xác minh) và không có type scale.
- Không có **clipboard, cursor, text-input event** ở platform seam.

Đây đúng là gap #4 trong brief: "Studio shell là frame, không phải authoring product".

---

## 3. Mục tiêu đo được (exit criteria của cả lộ trình)

| Mục tiêu | Đo bằng |
|---|---|
| G1 — Một Studio thay ≥ 10 mode cũ | `demo --help` còn ≤ 8 mode; mọi thao tác Lab cũ làm được trong Studio; chapter cũ có dòng chuyển hướng. |
| G2 — Mọi hành động Studio có undo + command tương đương | `commands_core` registry: mỗi command có test; `--cmd <id>` chạy được từ CLI (Rule 7/8). |
| G3 — Không còn text-only screen | Golden test cho từng workspace; checklist heuristics (§SPEC 2.9) pass. |
| G4 — Hai game đi trọn golden path | `assets/projects/farm.gameproject`, `creatures.gameproject`: create → inspect → publish → promote → verify xanh trong CI; chơi được native + WASM. |
| G5 — BaaS có consumer thật | Farm dùng cloud save + remote config + analytics + live event; Creature dùng cloud save + replay + realtime + leaderboard. |
| G6 — Web chia sẻ được | `demo.html?mode=collection` mở trang Collection; mỗi game chơi được bằng bàn phím và touch. |
| G7 — Sẵn sàng open source | README có GIF, `ASSETS-LICENSE.md`, `CONTRIBUTING.md`, CI xanh **đã xác minh**, Docker `/healthz` **đã xác minh**. |
| G8 — Learning không bị hy sinh | Mỗi slice có chapter; `docs/adr/` tồn tại với ≥ 5 ADR; PROJECT-BRIEF + CLAUDE.md cập nhật cuối mỗi slice. |

---

## 4. Lộ trình theo slice (thứ tự thực thi)

Quy ước: **[UI]** = nền UI/Studio, **[RT]** = runtime/game, **[WEB]**, **[OPS]** = Track C. Luân phiên đúng blend. Mỗi slice = một feature branch, merge `--no-ff`. Cột "Chapter" là số dự kiến, tiếp nối 107.

| # | Slice | Loại | Size | Chapter | Phụ thuộc |
|---|---|---|---|---|---|
| S1 | Text & font atlas, UTF-8, HiDPI logical size, sửa 4 bug nhìn thấy | UI | M | 108 | — |
| S2 | `ui` v2: theme tokens, draw list phân lớp, layout engine, bộ widget, id stack, clipboard/cursor/text-input ở platform seam | UI | L | 109 | S1 |
| S3 | `tilemap_core` v2 (layers/collision/triggers/autotile) + `camera2d` + migrate Map Lab format | RT | M | 110 | — |
| S4 | Studio unified shell: workspace model, `commands_core` registry, `CommandStack` undo/redo, autosave/recovery, hấp thụ Sandbox + Map Lab | UI | L | 111–112 | S2, S3 |
| S5 | **Farm — vertical slice** (đi lại, cuốc/tưới/thu hoạch, ngày-đêm, 1 NPC, save/load, `.gameproject`) | RT | L | 113 | S3 |
| S6 | Studio: Asset browser, Validation panel, **Release workspace** (thay Hub-UI), embedded Play viewport, toast/confirm/command palette; xoá `--hub-ui --shell --editor --sandbox --maplab` | UI | L | 114 | S4 |
| S7 | Studio asset tooling: Pixel editor, Autotile rule editor, Texture Lab v2 (palette, dither, outline), Sprite/Anim editor; hấp thụ `--studio --fx --light --anim --audio` | UI | XL | 115–116 | S6 |
| S8 | **Farm v1**: crops/seasons/shop/2 NPC + friendship/fishing/weather + BaaS (cloud save, remote config, analytics, live event) + HUD theo design system | RT | L | 117 | S5, S6 |
| S9 | Web: `shell.html` redesign, Collection page, touch controls, dashboard BaaS redesign, README/GIF/license audit | WEB | M | 118 | S2 |
| S10 | **Creature RPG — MVP**: overworld, encounter, battle turn-based, party, catch, save | RT | XL | 119–120 | S3, S4 |
| S11 | Creature RPG: battle replay → BaaS replays; PvP realtime (deterministic sim, seed share); leaderboard ELO | RT | L | 121 | S10 |
| S12 | OPS: CI xác minh thật (gh auth), Docker `/healthz` trên amd64 runner, Postgres adapter, TOCTOU purchase dưới một transaction | OPS | M | 122 | — (chèn bất kỳ lúc nào cần "đổi gió") |
| S13 | Stretch: Terraria-like world (chunked, noise, jobs lighting, platformer physics); Chess online qua realtime | RT | XL | 123+ | S3, S11 |

**Điểm dừng có thể show được** (milestone demo):
- Sau **S2**: Studio hiện tại nhìn "ra sản phẩm" dù chức năng chưa đổi.
- Sau **S6**: Studio là authoring tool thật, golden path chạy bằng chuột.
- Sau **S8**: Game đầu tiên chơi được, có cloud save, mở open source được rồi.
- Sau **S11**: Hai game, có PvP. Đủ điều kiện bắt đầu đồng hồ 4 tuần của Horizon 2.

---

## 5. Chi tiết từng slice — mục tiêu, deliverable, tiêu chí xong

### S1 — Text & font atlas, HiDPI, sửa bug nhìn thấy `[UI · M · ch.108]`

**Mục tiêu.** Chữ đẹp, đúng, rẻ; Studio có chỗ để bày layout.

**Deliverable**
- `text_core`: `FontAtlas` cache glyph theo (font, size) — rasterize **một lần** vào atlas 8-bit, blit từ atlas; UTF-8 decoder; glyph range: ASCII, Latin-1, **Latin Extended Additional (tiếng Việt)**, Arrows `U+2190–2199`, fallback tofu.
- Bundle 2 font OFL: UI (đề xuất *Inter*) và pixel (đề xuất *m5x7* hoặc *monogram*; kiểm tra license trước khi commit). Ghi `assets/fonts/LICENSE-*.txt`.
- `platform::Config` thêm `logical_w/h`, `scale_mode` (integer | letterbox), resize callback; Studio 1280×720; hi-DPI mac ra 2× sắc nét.
- Sửa: chữ cắt trên (baseline), `???` (arrow), shadow rect lệch (tạm thay bằng border cho đến S2), palette trống khi Play (tạm hiện read-only).

**Test.** `test_text`: decode UTF-8 (`→`, `ế`), atlas không rasterize lần 2 cho glyph đã có, đo `measure_text` khớp với vị trí vẽ. Golden: một màn "type specimen" 5 size.

**Xong khi.** Studio ở 1280×720 hiện đúng chữ, không `???`, không cắt; fps của Studio không giảm so với trước (đo bằng `--bench-ui` in ra ms/frame).

---

### S2 — `ui` v2: design system + layout + widget `[UI · L · ch.109]`

**Mục tiêu.** Thư viện UI đủ để xây Studio và mọi HUD mà không đặt toạ độ tay.

**Deliverable** (API chi tiết ở SPEC §2)
- `ui::Theme` tokens (màu/spacing/radius/type scale) — một struct, một file `theme_dark.hpp`, sau này thêm `theme_hud.hpp`.
- `DrawList` phân lớp (Base/Panel/Overlay/Tooltip) + clip stack; `Renderer2D` thêm `fill_rect_alpha`, `fill_round_rect`, `stroke_round_rect`, `push/pop_clip`.
- Layout: `begin_row/column(gap,pad,align)`, `next_slot(size, flex)`, `begin_panel`, `splitter`, `scroll_region`.
- Id stack (hash chuỗi + parent) cho widget stateful.
- Widget: `button` (primary/secondary/ghost/danger, có tooltip + hotkey hiển thị), `toggle`, `slider`, `text_input` (caret, selection, clipboard), `dropdown`, `tabs`, `tree_node`, `list_item` (virtualized), `table`, `badge`, `progress`, `tooltip`, `toast`, `modal/confirm`, `command_palette`.
- Keyboard: Tab/Shift-Tab focus ring, Enter/Space kích hoạt, Esc đóng popup.
- Platform seam: `set_cursor`, `clipboard_get/set`, `text_input` event, mouse wheel, key repeat.
- Rewrite `--shell` Hub tab bằng widget mới (chưa đổi chức năng) làm mẫu.

**Test.** `test_ui_layout` (rect tính ra đúng ở 3 kích thước), `test_ui_widgets` (state machine: hover→press→release→click; text input nhập/xoá/select/paste), mở rộng `test_ui_golden` cho mỗi widget.

**Xong khi.** Hub tab của Studio hiện 3 channel card + nút hành động chính, không còn dòng text thô; mọi widget có 4 state; checklist §SPEC 2.9 pass.

---

### S3 — `tilemap_core` v2 + `camera2d` `[RT · M · ch.110]`

**Mục tiêu.** Một runtime tile dùng chung cho Map Lab, Farm, Creature RPG, (và Terraria sau này).

**Deliverable**
- Format `map2` (text, versioned): tileset refs, N layer (`ground/decor/overlay/collision`), entity layer với props, trigger rects. Migration `fpsmap1 → map2` + test.
- Autotile 47-blob rule evaluation (rule data trong tileset def; editor ở S7).
- `camera2d`: follow target, deadzone, smoothing (dùng `tween_core`), bounds clamp, integer-pixel snap.
- Render: layer draw với culling theo viewport; y-sort cho entity layer.
- `--fps` vẫn load được map cũ (qua migration).

**Test.** `test_tilemap`: load/save round-trip, migration, autotile với 5 case, collision query, camera clamp.

**Xong khi.** Map Lab (còn là scene cũ) đọc/ghi `map2`; `--fps` chạy bằng map migrate; CI golden path không đổi.

---

### S4 — Studio unified shell + commands + undo `[UI · L · ch.111–112]`

**Mục tiêu.** Một cửa sổ, nhiều workspace, mọi thao tác undo được, mọi thao tác có command id.

**Deliverable**
- `studio_core`: `Workspace` interface, `EditorDocument` + `CommandStack` (`Command{apply, revert, label, merge_key}`), dirty flag, autosave `.autosave` + recovery prompt.
- `commands_core`: registry `Command{id, title, hotkey, run(args)->OpResult}`; `main.cpp` thêm `--cmd <id> [args]` gọi cùng hàm; CLI verb cũ (`--project-publish`…) trở thành alias sang registry. **Đây là hiện thực hoá Rule 8 ở mức code**.
- Layout Studio: top bar (project name · channel badge · Play/Stop) · nav rail trái (workspace) · canvas giữa · inspector phải · panel dưới (console/validation) · status bar. Splitter kéo được, layout lưu vào `assets/studio.layout`.
- Hấp thụ **Sandbox** → workspace *Scene*; **Map Lab** → workspace *Map*. Cả hai có inspector, undo, zoom/pan (wheel/space-drag), grid + snap, selection outline, multi-select drag.

**Test.** `test_studio_core`: undo/redo/merge, autosave/recovery; `test_commands`: mọi command đăng ký có handler, `--cmd` và keypress gọi cùng hàm (kiểm bằng counter).

**Xong khi.** `--sandbox`, `--maplab` xoá được (flag trả lỗi "moved to --studio"); mọi thao tác Scene/Map undo được.

---

### S5 — Farm vertical slice `[RT · L · ch.113]`

**Mục tiêu.** Game thứ hai đi qua platform, chơi được 10 phút, chứng minh runtime layer đủ.

**Deliverable** (design đầy đủ ở SPEC §4)
- `farm_core` (SDL-free): world clock, energy, tool actions, crop growth (1 loại), 1 NPC đi theo schedule + dialogue, save/load versioned.
- `scene_stack` (overworld/menu/dialogue push-pop), `dialogue_core` (script text → box typewriter), `input_map` (keyboard + gamepad + virtual touch), `save_core` (schema version + migration hook).
- Map `farm_home.map2` vẽ trong Studio Map workspace bằng tileset ngoài (CC0).
- `assets/projects/farm.gameproject`, entry `farm` trong `launch_entry`.

**Test.** `test_farm_sim`: mô phỏng 3 ngày deterministic, crop lớn đúng lịch, energy hết → ngủ; save round-trip.

**Xong khi.** `demo --project projects/farm.gameproject` chạy native + WASM; publish → promote xanh.

---

### S6 — Studio: Asset browser, Validation, Release workspace, Play viewport `[UI · L · ch.114]`

**Deliverable**
- **Asset browser**: quét manifest + asset root; badge theo type; thumbnail render bằng core tương ứng; kéo asset vào Map/Scene; "Add to project" ghi dòng `asset` vào manifest (qua `project_core`).
- **Validation panel**: chạy doctor + resource closure; mỗi issue có nút "jump to".
- **Release workspace** (thay Hub-UI): pipeline dev→preview→prod dạng card, badge màu channel, diff package (file thay đổi), nút hành động chính từ `hub_core::recommend()`, **confirm modal bắt buộc nhập reason**, audit timeline.
- **Play viewport**: `SubFramebuffer` target cho `Renderer2D`, chạy scene của `entry` ở fixed timestep trong canvas; Play/Pause/Step/Stop (Stop = restore snapshot).
- Command palette (`Ctrl+K`) liệt kê toàn bộ registry; toast cho kết quả `OpResult`.
- Xoá `--hub-ui --shell --editor`.

**Test.** golden cho Release workspace ở 3 trạng thái (in-sync / drift / missing); `test_hub` không đổi vì view model chung.

**Xong khi.** Golden path làm hoàn toàn bằng chuột trong Studio và audit.log ghi đúng; `--hub` headless vẫn in cùng nội dung.

---

### S7 — Studio asset tooling `[UI · XL · ch.115–116]`

**Mục tiêu.** "Mini studio tạo được asset đẹp như bên ngoài" — đây là slice học sâu nhất về đồ hoạ 2D.

**Deliverable**
- **Pixel editor**: canvas zoom 1–32×, brush/eraser/fill/line/rect/eyedropper, layer (≤ 8), palette panel (import `.hex`/GPL, lock palette), onion-skin cho anim, export PNG (encoder tự viết — đã có decoder), symmetry, dither brush.
- **Autotile rule editor**: vẽ 47 tile theo template blob, preview trên map mẫu.
- **Texture Lab v2**: pipeline node-lite (noise → palette quantize → ordered dither → outline → emboss) tái sử dụng recipe `.hrt`; export tile/tileset.
- **Sprite/Anim editor**: cắt sheet, đặt tên clip, fps, pivot, hitbox; format `.anim` versioned.
- **FX / Light / Audio** thành panel trong Scene workspace (emitter inspector, light inspector, audio clip trigger) — xoá 4 flag.

**Test.** `test_pixel_editor` (ops trên bitmap deterministic, undo), `test_png_encoder` (round-trip với decoder), `test_texture_lab` mở rộng.

**Xong khi.** Vẽ được một tileset 16×16 + một sprite 4-frame hoàn toàn trong Studio, dùng ngay trong Farm.

---

### S8 — Farm v1 + BaaS + HUD `[RT · L · ch.117]`

**Deliverable** (SPEC §4): 8 crop / 4 mùa, shop, 2 NPC friendship + gift, fishing minigame, weather (mưa = particles + light giảm), mine 5 tầng (optional), HUD theo `theme_hud` (hotbar, clock, energy, dialogue box 9-slice). BaaS: cloud save slot, remote config (giá, growth days), analytics (`day_end`, `harvest`, `sale`), live event "Harvest Festival" bật bằng dashboard.

**Xong khi.** Chơi được 28 ngày in-game; save đồng bộ native ↔ web qua cloud save; đổi giá bằng dashboard thấy ngay trong game.

---

### S9 — Web: shell, Collection, touch, dashboard, open-source prep `[WEB · M · ch.118]`

**Deliverable** (SPEC §6–7): `shell.html` mới (canvas integer-scale, fullscreen, loading bar, ?mode=), trang **Collection** liệt kê `.gameproject` với ảnh/mô tả/nút Play; virtual d-pad + 2 nút cho mobile qua `input_map`; dashboard BaaS viết lại bằng HTML/CSS vanilla dùng cùng token (CSS variables); README + GIF + `ASSETS-LICENSE.md` + `CONTRIBUTING.md`.

**Xong khi.** Mở link trên điện thoại chơi được Farm; dashboard đọc được trên mobile.

---

### S10 — Creature RPG MVP `[RT · XL · ch.119–120]`

**Deliverable** (SPEC §5): `creature_core`: grid movement, encounter table theo tile, 18 creature / 6 type / 30 move (data text), battle turn-based (speed order, damage formula, status 3 loại), AI đơn giản, catch, party 6 + PC box, level/evolve, 3 town map + 1 route + 1 "gym"; save/cloud save.

**Xong khi.** Chơi 30 phút, bắt được, thắng gym; deterministic battle với seed (chuẩn bị cho S11).

---

### S11 — Replay + PvP realtime + leaderboard `[RT · L · ch.121]`

**Deliverable.** Battle log → BaaS replays (xem lại trong game); PvP: matchmaking → room → hai client trao đổi lệnh mỗi lượt, cùng seed, cùng sim → kết quả khớp (assert hash state mỗi lượt, desync → report); ELO qua leaderboard; analytics `battle_end`.

**Xong khi.** Hai trình duyệt đấu nhau qua BaaS thật; replay xem lại đúng từng lượt.

---

### S12 — OPS (chèn linh hoạt) `[OPS · M · ch.122]`

`gh auth` + chạy CI thật, đọc log, sửa; Docker build trên GitHub runner amd64 (CI job riêng) và assert `/healthz`; Postgres adapter sau interface `IStore` (SQLite giữ cho local), migration chạy cả hai; purchase: affordability + debit trong **một** `BEGIN IMMEDIATE`/`SELECT … FOR UPDATE`. Cập nhật ledger §8 brief từ ❌ → ✅ **chỉ khi đã chạy**.

---

### S13 — Stretch

- **Terraria-like** ("Deep"): thế giới 2048×512 tile chunk 32×32, gen bằng noise của Texture Lab, ánh sáng flood-fill chạy trên `jobs_core`, platformer trên `physics_core`, craft, chia sẻ world qua asset registry.
- **Chess online**: room realtime + ELO; tái dùng `chess_core` nguyên vẹn.

---

## 6. Nguyên tắc thực thi (bổ sung cho CLAUDE.md)

1. **Một slice = một branch = một chapter = cập nhật PROJECT-BRIEF §7/§8 + CLAUDE.md mode list.** Không merge nếu thiếu một trong bốn.
2. **Mỗi widget/screen mới phải qua checklist heuristics** (SPEC §2.9) trước khi coi là xong.
3. **Không vẽ toạ độ tay trong Studio** sau S2. HUD game được phép nếu qua `theme_hud`.
4. **Golden image test là bắt buộc cho workspace và HUD**; lưu ở `tests/golden/`, diff tolerance 0.
5. **Mọi format mới có version + migration test** (map2, anim, save).
6. **Asset ngoài** phải vào `ASSETS-LICENSE.md` cùng commit.
7. **ADR trước khi code** cho: UI v2 (ADR-001), resolution/scale (002), map2 (003), commands registry (004), save schema (005), PvP determinism (006).
8. **Khi mệt plumbing → làm game; khi mệt game → làm OPS.** Blend là để dự án sống, không phải để tuân thủ.

---

## 7. Rủi ro & cách xử

| Rủi ro | Xử |
|---|---|
| S2 phình to (UI framework là hố đen) | Chốt danh sách widget trong SPEC §2.6, không thêm cho đến S6. Widget thiếu → dùng `button` + `text` tạm. |
| CPU 1280×720 chậm (blit text, alpha rect) | Đo từ S1 bằng `--bench-ui`; dirty-rect chưa cần; nếu > 8 ms/frame → giảm về 1024×576 hoặc cache panel tĩnh vào `SubFramebuffer`. |
| Farm + Creature cùng lúc quá rộng cho một người | Farm S5→S8 xong hẳn rồi mới S10. Không chạy song song. |
| Asset ngoài lẫn license | `ASSETS-LICENSE.md` là gate của review; chỉ CC0/OFL/CC-BY. |
| Xoá mode cũ làm mất demo chapter | Mỗi chapter cũ thêm "Reproduce today: open Studio → workspace X". Core + test giữ nguyên. |
| PvP desync | Sim thuần integer, không float trong battle; hash state mỗi lượt; test replay 1000 trận random seed. |
| Open source sớm → nhận issue mà không kịp | Mở sau S8 với README nói rõ "learning project, no support SLA". |

---

## 8. Việc cần bạn quyết trước khi bắt đầu S1

1. Tên hai game (tôi đang dùng placeholder *Farm* / *Creatures*).
2. Font: *Inter* + *m5x7* OK không, hay bạn thích font khác? (Cần license OFL/CC0.)
3. Studio 1280×720 hay 1024×576 (nếu máy bạn yếu)?
4. Có muốn tôi viết ADR-001…006 luôn sau khi bạn duyệt SPEC không?

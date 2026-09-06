# SPEC — Đặc tả kỹ thuật chi tiết

**Đi kèm:** `PLAN.md` (thứ tự và tiêu chí xong). Số mục dưới đây được PLAN tham chiếu (vd "SPEC §2.9").
**Cập nhật 2026-09-06:** các delta từ `RESEARCH-kenney-assets.md` đã được nhập vào §3.4
(`.pack`), §3.9 (**Mixer**), §7.2 (README template) — xem `PLAN-v2-CORRECTIONS.md` §4 để
biết mỗi mục rơi vào slice nào.

**Trạng thái:** bản gốc được viết **trước khi đọc `src/`**. Ngày 2026-09-04 toàn bộ
spec đã được đối chiếu với code; §0 dưới đây liệt kê mọi chỗ sai và cách sửa. Các
đoạn đã kiểm được đánh dấu `[verified]`, các đoạn bị sửa đánh dấu `[corrected]`.
Tên hàm/struct vẫn là **đề xuất API** — khi triển khai, **giữ tên hiện có** nếu đã
tồn tại và chỉ mở rộng.

---

## 0. Đối chiếu code — 2026-09-04 `[corrected]`

Spec gốc giả định lớp UI gần như trống. **Không đúng.** Chương 68–71 đã xây font
AA, anti-aliasing (SSAA + Wu + coverage), design system với token, và golden test.
Những gì spec đề nghị "xây mới" phần lớn là **mở rộng** cái đang có.

### 0.1 Đã tồn tại — không xây lại

| Spec nói thiếu | Thực tế | Bằng chứng |
|---|---|---|
| theme tokens (§2.1) | **có** — surfaces `bg/elevated/titlebar/border`, control-state `ctrl/ctrl_hover/ctrl_press/ctrl_disabled/track`, text `text/text_dim/text_muted/on_accent`, `accent{,_hover,_press}`, `success/warn/danger`, spacing 4·8·12·16·24, type scale 12·14·16·20·28, radius 6/10, `Shadow` | `src/engine/ui/theme.hpp` · ch. 71 |
| glyph atlas (§2.8) | **có** — cache theo pixel-size, 95 glyph ASCII rasterize một lần rồi tái dùng | `src/engine/text/font.cpp:39-79` |
| `fill_round_rect`/`stroke_round_rect` (§2.4) | **có** — `fill_round_rect`, `draw_round_rect`, `fill_circle`, `draw_circle`, coverage AA giải tích | `renderer2d.hpp:55-58` |
| shadow đúng (§2.3) | **có** — `drop_shadow()` nhiều lớp alpha; `ui::panel()` gọi nó | `renderer2d.hpp:64`, `ui.cpp:155` |
| alpha blend (§2.4 `fill_rect_alpha`) | **có** — `blend_pixel`, `blend_cov`, `gfx::blend()` source-over | `color.hpp:28-37` |
| blit coverage 8-bit (§2.4) | **có**, nhưng chỉ inline trong `draw_text` — chưa có API chung | `renderer2d.cpp:365` |
| render ra off-screen (§3.7 `SubFramebuffer`) | **có sẵn** — `Renderer2D` nhận `platform::Framebuffer` bất kỳ, kể cả buffer do caller cấp | `test_ui_golden.cpp:45-49` |
| font Inter + mono | **có sẵn** `assets/fonts/Inter.ttf`, `JetBrainsMono.ttf` **kèm OFL** | `assets/fonts/` |
| golden test | **có** `test_ui_golden`; `test_theme` đã kiểm contrast | `tests/` |

**Hệ quả quan trọng nhất — KHÔNG đổi tên token.** §2.1 đề xuất bảng tên mới
(`bg.base`, `text.primary`, `accent.hover`…). Đổi tên chạm 9 scene đang dùng
`theme.hpp` mà không thêm giá trị nào. **Giữ nguyên tên hiện có, chỉ THÊM** token
còn thiếu: `scrim`, `border_strong`, `info`, biến thể `.soft` (@15%), và 3 màu
channel `chan_dev/chan_preview/chan_prod`.

### 0.2 Thiếu thật — đây mới là việc của S2

- `ui::Input` chỉ `{mx, my, down, pressed, released}` (`ui.hpp:24`) → **không có bàn
  phím, focus, wheel, modifier**. Tab-navigation và `Ctrl+K` bất khả thi hôm nay.
- **Không có id stack** — id là hash một lần của label; ceiling ghi rõ tại `ui.hpp:50-53`.
- **Không có clip/scissor** ở bất kỳ đâu trong `Renderer2D`. Ràng buộc duy nhất là
  test mép framebuffer trong `blend_cov` (`renderer2d.cpp:43`). Panel **không** cắt
  nội dung tràn.
- **Không có layer/draw list** — mọi widget vẽ thẳng, đồng bộ. Không có z-order.
- **Không có** scroll region, text input widget, clipboard.
- **Không có UTF-8 decode** — xem §0.3.
- **Widget hiện có đúng 4**: `button`, `checkbox`, `slider`, `label` (+ `panel`).

### 0.3 Bốn bug trong screenshot — nguyên nhân thật `[corrected]`

Bảng §2.10 đoán sai 3/4.

| Bug | §2.10 đoán | Nguyên nhân thật |
|---|---|---|
| Chữ "Studio" thô, như bị cắt | sai baseline / clip | **Bác bỏ bằng số đo.** `Inter.ttf` tại px=20: `ascent≈16px`, `capHeight≈12px` → đỉnh "S" nằm **dưới** mép trên line box 4px; `draw_text` cài đúng hợp đồng "`y` = top của line" (`renderer2d.cpp:347-370`, `base = y*ss + ascent`). Không có đường nào cắt. **Nguyên nhân là nhoè**: `--shell` và `--hub-ui` là **hai scene windowed duy nhất thiếu `cfg.supersample = kAA`** (15 scene khác đều có) → không SSAA, cộng `smooth=true` + HiDPI → SDL nội suy tuyến tính khi upscale. `main.cpp:434-441, 447-454` |
| `publish???dev` | thiếu glyph U+2192 | **`font.cpp:19-23`** — `index_of(char)` ép **mọi byte ngoài [32,126] thành `'?'`**; `renderer2d.cpp:313-315` làm y hệt cho đường 8x8. "→" là 3 byte UTF-8 (`E2 86 92`) → **đúng ba dấu `?`**. Không phải thiếu glyph — **thiếu UTF-8 decode**. Cũng là lý do tiếng Việt không hiện được. Chuỗi lỗi nằm ở **hai** chỗ: `studio_shell_scene.cpp:137` và `hub_scene.cpp:61`. |
| Shadow panel lệch | solid rect offset, không alpha | `drop_shadow` là soft shadow nhiều lớp alpha thật, `ui::panel` gọi đúng (`ui.cpp:155`). **Xem lại ở scale đúng sau khi sửa supersample trước khi coi là bug.** |
| Hub/Studio như "in text ra màn hình" | thiếu widget | **`studio_shell` và `hub` không dùng `ui::Context` chút nào** — gọi thẳng `g.draw_text` với **24 và 9 literal màu thô**, không include `theme.hpp`. 9 scene khác đều dùng `ui::Context`. Đây là lý do hai màn đó lệch hẳn phần còn lại — không phải vì thư viện thiếu widget. |

### 0.4 Đề xuất thừa, sai tên, hoặc mâu thuẫn `[corrected]`

- **Atlas 1024×1024 + shelf packer (§2.8)** — **bỏ**. Đây là CPU blit, không có
  texture GPU để pack. Chỉ cần đổi khoá cache từ `char` sang codepoint.
- **Golden test "so byte, tolerance 0" (§9)** — **mâu thuẫn với quyết định đã ghi**
  tại `test_ui_golden.cpp:9-12`: cố ý *không* hash pixel vì AA làm tròn khác nhau
  giữa compiler/kiến trúc (native vs web). **Giữ kiểu invariant** (màu vùng đặc
  khớp token + có pixel AA), không quay lại byte-diff.
- **`tools/contrast.py` (§2.9 mục 10)** — **bỏ**: `test_theme` đã kiểm contrast, và
  repo không có toolchain Python.
- **`OpResult` (§3.3)** — đã tồn tại `engine::OpResult{ok, message}` tại
  `release/ops.hpp:18-21`. Dùng lại.
- **`hub_core::model()` mới (§3.6)** — **không cần**. `HubView`/`HubChannel` đã là
  struct (`hub/hub.hpp:20-36`); `hub_lines(HubView)` chỉ là bộ format. Chỉ cần
  **thêm trường** vào `HubView` nếu workspace cần.
- **`.hrt` là "recipe" (§3.9)** — **sai tên**. `.recipe` là sidecar `key=value`
  (`studio/recipe.cpp:10-25`); `.hrt` là format raster (`HRT1 | BE w | BE h | RGBA8`,
  `image.hpp:6-9`). Phải viết "**`.recipe` v2**".
- **"PNG — encoder tự viết, đã có decoder" (§3.9)** — **sai**: repo **không có cả
  encoder lẫn decoder PNG**; `third_party/` chỉ có `stb_truetype.h`. → Pixel editor
  dùng `.hrt` (đã có, đã test, `--anim`/`--sandbox` đã tiêu thụ). PNG chỉ làm khi
  thật sự cần trao đổi ra ngoài, và khi đó là **hai** bài chứ không phải một.
- **`IStore` (§11)** — **không tồn tại**; service gọi thẳng `drogon::orm::DbClient`
  (`baas/db/db.h:38-39`).
- **TOCTOU purchase (§11)** — hôm nay **an toàn** vì SQLite pool = 1; comment
  `ponytail:` tại `inv_service.cc:127-131` nói rõ nó **chỉ mở lại khi Postgres pool > 1**.
  → **`FOR UPDATE` phải đi CÙNG slice Postgres**, không phải một mục riêng sau đó.
- **`App` giữ MỘT Scene** (`app.hpp:28`), không setter, không stack, không sub-scene
  → §3.7 (Play viewport) và §4.3 (`scene_stack`) đều phải **sửa `App`**, không chỉ thêm lib.
- **Bug nhỏ chưa ai để ý**: `text_width()` chia nguyên cho `ss_`
  (`renderer2d.cpp:341-345`) trong khi `draw_text` giữ pixel vật lý → lệch tới
  `ss_-1` px khi căn giữa. Sửa ở S1.
- **Ceiling cần ghi**: `Font::sizes` map **không bao giờ evict** — vô hại với type
  scale cố định, nhưng một slider chỉnh cỡ chữ sẽ làm nó phình vô hạn.

### 0.5 Những chỗ spec đúng — giữ nguyên `[verified]`

Không có `tilemap_core` dùng chung: **hai grid độc lập** — `fps::Map` (uint8 + text
`fpsmap1`) và `iso::TileMap` (enum `Terrain`, không có text I/O riêng); maplab
**không có** layer / collision mask / trigger / tileset ảnh / autotile · **không có
camera 2D** trong `engine/` (`camera.hpp` chỉ 3D; `iso::Camera2D` là pan 2 float,
iso-specific) · **không có** format `.anim`, **không có** preset particle trên đĩa ·
manifest **không có** `cover`/`summary` (schema 1) · `release_ops` **không có**
`status()`/`log()` — chúng chỉ là hàm local trong `main.cpp:260-288`, nên Release
workspace sẽ phải nâng chúng lên core · SDK **không có** wrapper store/purchase dù
BaaS có `/v1/store/*` · `kKnownEntries = {"fps"}` · sandbox **không có** undo và
**không có** zoom/pan (nhưng **đã có** selection + inspector — §2 nói sai chỗ này).

---

## 1. Ràng buộc bất biến áp dụng cho toàn spec

- Giữ nguyên 10 ràng buộc trong PROJECT-BRIEF §2. Cụ thể cho spec này:
  - Mọi thứ vẽ vào CPU framebuffer qua `Renderer2D`. Không blur, không gradient nhiều bước; alpha blend được phép.
  - Mọi thứ viết ra đĩa qua `assets::`. Format mới đều là **text versioned** (dòng đầu `<name> <version>`), trừ PNG.
  - Mọi thao tác trong Studio = một `Command` trong `commands_core`; CLI `--cmd` gọi cùng hàm.
  - Game mới = `.gameproject` + entry trong `launch_entry`. Không flag mới.
- Kích thước logical:
  - Studio: **1280×720**, resizable; `scale_mode = integer` trên HiDPI (2× → 2560×1440 pixel thật nhưng vẫn 1280×720 logical).
  - Game 2D mới: **640×360**, cố định, letterbox. Retro cũ: 480×270.
- Frame budget Studio: ≤ 8 ms render ở 1280×720 trên máy dev; đo bằng `--bench-ui` (S1).

---

## 2. `ui` v2 — Design system và thư viện immediate-mode

### 2.1 Tokens màu (dark, mặc định) `[corrected — xem §0.1]`

> **KHÔNG áp dụng bảng dưới đây như một bảng thay thế.** `theme.hpp` đã tồn tại và
> 9 scene đang dùng tên hiện có (`bg`, `elevated`, `ctrl`, `text`, `text_dim`,
> `accent`, `success`, `warn`, `danger`…). Đổi tên là churn thuần. **Giữ tên cũ,
> chỉ thêm** những token thật sự còn thiếu: `scrim`, `border_strong`, `info`, biến
> thể `.soft` (@15%), và `chan_dev`/`chan_preview`/`chan_prod`. Bảng dưới giữ lại
> làm *tham chiếu giá trị màu*, không phải tham chiếu tên.


Đặt trong `src/engine/ui/theme.hpp` dưới dạng `struct Theme` với các trường `Color` (RGBA8). Không hard-code màu ở bất kỳ widget nào.

| Token | Hex | Dùng cho |
|---|---|---|
| `bg.base` | `#0F1115` | Nền cửa sổ |
| `bg.surface` | `#161A22` | Panel, nav rail |
| `bg.raised` | `#1E2430` | Card, input nền |
| `bg.overlay` | `#252C3A` | Popup, dropdown, tooltip |
| `bg.scrim` | `#000000` @ 55% | Lớp mờ sau modal |
| `border.subtle` | `#2A3140` | Viền panel/card |
| `border.strong` | `#3A4356` | Viền input, splitter |
| `text.primary` | `#E6E9EF` | Chữ chính |
| `text.secondary` | `#A5ADBD` | Nhãn, mô tả |
| `text.muted` | `#6B7488` | Placeholder, hint |
| `accent` | `#7AA2F7` | Nút primary, focus ring, link |
| `accent.hover` | `#8FB3FF` | |
| `accent.active` | `#5E8AE6` | |
| `accent.soft` | `#7AA2F7` @ 18% | Selection, row đang chọn |
| `success` | `#7FD1A0` | |
| `warning` | `#E5B567` | |
| `danger` | `#E06C75` | Nút destructive, lỗi |
| `info` | `#67C7E5` | |
| `chan.dev` | `#7AA2F7` | Badge channel development |
| `chan.preview` | `#C792EA` | Badge channel preview |
| `chan.prod` | `#7FD1A0` | Badge channel production |

Mỗi tông `success/warning/danger/info` có biến thể `.soft` (@ 15%) cho nền badge/toast.

### 2.2 Typography

- Font UI: **Inter** (OFL) `[verify license file]`, file `assets/fonts/Inter-Regular.ttf`, `Inter-Medium.ttf` (dùng Medium cho title/button).
- Font pixel (HUD/game): **m5x7** hoặc **monogram** (free) `[verify]`.
- Type scale (px, logical) — `Theme::type`:

| Tên | Size | Line-height | Weight | Dùng |
|---|---|---|---|---|
| `caption` | 11 | 15 | Regular | hint, hotkey, footer |
| `body` | 13 | 18 | Regular | mặc định |
| `body_md` | 13 | 18 | Medium | label, nút |
| `title` | 16 | 22 | Medium | tên panel |
| `heading` | 20 | 28 | Medium | tên workspace/project |
| `display` | 28 | 36 | Medium | màn Home |
| `mono` | 12 | 16 | Regular | hash, path, log (dùng Inter + tabular figures; hoặc bundle *JetBrains Mono* OFL) |

- Số hash (`c95febd882741b29`) hiển thị **8 ký tự đầu + tooltip full**, font `mono`, kèm nút copy.

### 2.3 Spacing, radius, elevation `[corrected]`

> Spacing 4·8·12·16·24, radius 6/10, và `drop_shadow()` nhiều lớp alpha **đã tồn
> tại** (`theme.hpp`, `renderer2d.hpp:64`). Chỉ còn thiếu `scrim` cho modal và
> quy ước 3 mức elevation. Chiều cao control chuẩn bên dưới là mới, giữ nguyên.


- Spacing scale: `1=4, 2=8, 3=12, 4=16, 5=24, 6=32` (px logical). Padding panel = 4 (16px), gap widget = 2 (8px), gap section = 5.
- Radius: `sm=3` (badge, input), `md=6` (button, card), `lg=10` (modal).
- Elevation (CPU, không blur):
  - `level1` (card): border `border.subtle`.
  - `level2` (popup): border `border.strong` + rect `#000` @ 35% offset (0, +2) **vẽ trong layer Overlay, có clip** → sửa lỗi shadow lệch ở Sandbox.
  - `level3` (modal): scrim toàn màn + level2.
- Focus ring: 2px `accent` ngoài viền, radius +2.
- Chiều cao control chuẩn: button/input 28px; row list 24px; nav item 32px; top bar 40px; status bar 24px.

### 2.4 Kiến trúc render: DrawList phân lớp + clip stack `[corrected]`

> **Thu hẹp phạm vi.** `fill_round_rect`/`draw_round_rect`/`fill_circle`/
> `draw_circle`/`drop_shadow`/alpha-blend/blit coverage 8-bit **đã có**
> (`renderer2d.hpp:47-76`). Hai thứ thật sự thiếu là **clip stack** và **thứ tự
> vẽ overlay**.
>
> **Và không làm DrawList đầy đủ ở S2.** Vấn đề thật là popup/tooltip/modal khai
> báo sớm bị widget khai báo sau vẽ đè. Bản nhỏ nhất giải đúng nó là **hoãn riêng
> phần overlay** sang cuối `end()` — không cần biến mọi widget thành `DrawCmd`.
> Nâng lên draw list phân lớp đầy đủ chỉ khi bản hoãn-overlay chứng minh là hụt.


```cpp
namespace ui {
enum class Layer : uint8_t { Base, Panel, Overlay, Tooltip };   // vẽ theo thứ tự tăng dần
struct DrawCmd { Layer layer; Rect clip; /* variant */ ... };
struct DrawList { std::vector<DrawCmd> cmds; void sort_stable_by_layer(); };
void push_clip(Ctx&, Rect); void pop_clip(Ctx&);
void end_frame(Ctx&, Renderer2D&);   // sort → replay vào framebuffer
}
```

- Widget **không vẽ trực tiếp** vào `Renderer2D`; chúng append `DrawCmd`. `end_frame` sort ổn định theo layer rồi replay. Đây là fix gốc cho popup/tooltip/shadow.
- `Renderer2D` thêm: `fill_rect_alpha(Rect, Color)`, `fill_round_rect(Rect, r, Color)`, `stroke_round_rect(Rect, r, w, Color)`, `set_clip(Rect)`, `blit_glyph(atlas, glyph, x, y, Color)` (8-bit coverage → alpha blend với màu chữ).

### 2.5 Id stack, state, input

- `ui::Id = uint64_t` = FNV-1a(label) ⊕ parent id (stack). `push_id(const char*)`/`push_id(int)`/`pop_id()` như Dear ImGui. Widget cùng label trong cùng parent bắt buộc `push_id`.
- State giữ trong `Ctx`: `hot`, `active`, `focused` (keyboard), `open_popups`, `text_input_state{caret, sel_a, sel_b, scroll}`, `scroll_offsets` map<Id,float>, `tree_open` map<Id,bool>.
- Input snapshot từ platform: mouse pos/buttons/wheel, keys down/pressed/released **với repeat**, `text_input` (chuỗi UTF-8 nhận trong frame), modifiers.
- Focus: `Tab`/`Shift+Tab` đi theo thứ tự khai báo trong frame; `Enter`/`Space` kích hoạt; `Esc` đóng popup/modal đang mở (ưu tiên cái mở sau).

### 2.6 Bộ widget (chốt cho S2 — không thêm trước S6)

```cpp
// layout
void begin_column(Ctx&, LayoutOpts{gap, pad, align});   // align: Start|Center|End|Stretch
void begin_row(Ctx&, LayoutOpts);
void end_layout(Ctx&);
Rect next_slot(Ctx&, Size{w,h}, float flex = 0);   // h/w = 0 → theo nội dung; flex>0 → chia phần dư
void spacer(Ctx&, float px = 0);                   // px=0 → flex 1
void begin_panel(Ctx&, const char* id, PanelOpts{title, elevation, pad});  void end_panel(Ctx&);
float splitter(Ctx&, const char* id, Axis, float value, float min, float max);
void begin_scroll(Ctx&, const char* id, Size); void end_scroll(Ctx&);

// text
void text(Ctx&, const char*, TextStyle = Body, Color = text.primary);
void text_mono(Ctx&, const char*);
void label_value(Ctx&, const char* label, const char* value);   // hàng "label   value" căn cột

// controls
bool button(Ctx&, const char* label, ButtonKind = Secondary, const ButtonOpts& = {});
//  ButtonKind: Primary | Secondary | Ghost | Danger ; ButtonOpts{tooltip, hotkey, disabled, width}
bool toggle(Ctx&, const char* label, bool& v);
bool slider(Ctx&, const char* label, float& v, float min, float max, const char* fmt = "%.2f");
bool slider_int(Ctx&, const char* label, int& v, int min, int max);
bool text_input(Ctx&, const char* id, std::string& v, TextInputOpts{placeholder, mono, multiline, max_len});
bool dropdown(Ctx&, const char* id, int& index, std::span<const char* const> items);
int  tabs(Ctx&, const char* id, std::span<const char* const> labels, int current);
bool tree_node(Ctx&, const char* label, TreeOpts{leaf, selected, badge});
bool list_item(Ctx&, const char* label, bool selected, ListOpts{secondary, badge, badge_tone});
void table_begin(Ctx&, const char* id, std::span<const Column>); void table_row(...); void table_end(Ctx&);
void badge(Ctx&, const char* text, Tone);                   // Tone: Neutral|Info|Success|Warning|Danger|Dev|Preview|Prod
void progress(Ctx&, float v01, const char* label = nullptr);
void tooltip_last(Ctx&, const char* text);                  // gắn vào widget vừa khai báo

// overlays
void toast(Ctx&, Tone, const char* msg, float seconds = 4);
bool modal_begin(Ctx&, const char* id, const char* title, bool& open); void modal_end(Ctx&);
ConfirmResult confirm(Ctx&, const char* id, ConfirmOpts{title, body, confirm_label, danger, require_reason});
bool command_palette(Ctx&, std::span<const CommandInfo>, CommandId& out);  // Ctrl+K, fuzzy filter
```

Mỗi control có 5 trạng thái vẽ: `normal / hover / active / focused / disabled`. Nút Primary: nền `accent`, chữ `bg.base`; Secondary: nền `bg.raised` + border; Ghost: trong suốt, hover `bg.raised`; Danger: nền `danger.soft`, chữ `danger`, hover nền `danger`.

### 2.7 Platform seam bổ sung (`platform.hpp`)

```cpp
enum class Cursor { Arrow, Hand, IBeam, ResizeH, ResizeV, Move, Crosshair };
void set_cursor(Cursor);
std::string clipboard_get(); void clipboard_set(std::string_view);
struct Config { ..., int logical_w, logical_h; ScaleMode scale_mode; bool resizable; };
// Input thêm: std::string text_input; int wheel_dx, wheel_dy; key repeat flags; bool window_resized; int win_w, win_h;
```

SDL: `SDL_StartTextInput`, `SDL_SetCursor`, `SDL_GetClipboardText`. Web: clipboard qua `navigator.clipboard` (EM_JS) — fallback no-op nếu bị chặn; cursor qua CSS `canvas.style.cursor`.

### 2.8 `text_core` — FontAtlas `[corrected]`

> **Bỏ atlas 1024×1024 + shelf packer.** Đây là CPU blit — không có texture GPU để
> pack, và cache per-size hiện tại (`font.cpp:39-79`) đã đúng. Việc thật sự cần:
> (a) **UTF-8 decode**, (b) đổi khoá cache từ `char` sang **codepoint**
> (`unordered_map<char32_t, Glyph>`), giữ ASCII rasterize sẵn và non-ASCII lazy,
> (c) tofu = ô rỗng thay vì `'?'`, (d) sửa lệch làm tròn `text_width` ÷ `ss_`.
>
> Mục "Fix bug cắt chữ" bên dưới **không cần** — hợp đồng "`y` = top của line box"
> đã được cài đúng (`renderer2d.cpp:347-370`); xem §0.3.


- `FontAtlas{ font, size, bitmap 8-bit (1024×1024, grow ×2), glyphs: map<codepoint, Glyph{x,y,w,h,bearing,advance}> }`.
- Rasterize lazily khi gặp codepoint mới (`stbtt_MakeCodepointBitmap`), pack bằng shelf packer tự viết.
- `measure(text) -> {w, h, baseline}`; `draw(list, text, x, y_baseline, style, color)`; wrap word theo max width; ellipsis `…` khi tràn (dùng cho path dài).
- UTF-8 decode chuẩn (reject overlong, thay ký tự lỗi bằng `U+FFFD`). Tofu = rect rỗng 1px.
- **Fix bug cắt chữ**: mọi hàm vẽ nhận `y` là **top của line box**; baseline = `y + ascent*scale`; clip rect bao gồm cả descent.

### 2.9 Checklist heuristics (bắt buộc pass cho mỗi màn/widget)

1. Có thể làm mọi việc bằng chuột **và** bằng bàn phím.
2. Hành động không thể đảo (publish/promote/rollback/delete) có confirm + reason.
3. Hành động chính của màn là **nút Primary duy nhất**, ở vị trí nhất quán (góc phải top bar hoặc cuối card).
4. Mọi trạng thái hệ thống hiện bằng **badge màu + chữ**, không chỉ chữ.
5. Hash/path dài: rút gọn + tooltip + copy.
6. Kết quả mọi command hiện bằng toast (success/fail + lý do).
7. Không có text > 80 ký tự trên một dòng không wrap.
8. Không có widget nào đặt bằng toạ độ tuyệt đối trong Studio.
9. Hover có tooltip nếu label < 3 từ hoặc có hotkey.
10. Contrast text/nền ≥ 4.5:1 (tính bằng script `tools/contrast.py` — bổ sung).

### 2.10 Sửa 4 bug nhìn thấy (S1) `[corrected — bảng dưới đoán sai 3/4]`

> **Dùng §0.3 thay cho bảng này.** Tóm tắt: bug #1 là thiếu `supersample = kAA`
> (2 dòng trong `main.cpp`), không phải baseline; bug #2 là thiếu UTF-8 decode,
> không phải thiếu glyph; bug #3 nhiều khả năng không phải bug; bug #4 là do
> `studio_shell`/`hub` không dùng `ui::Context`, không phải thiếu widget.


| Bug | Nguyên nhân dự đoán | Fix |
|---|---|---|
| "Studio" bị cắt phần trên | vẽ text ở `y - ascent` hoặc clip panel không tính ascent | §2.8 quy ước `y` = top line box |
| `publish???dev` | glyph U+2192 không có; string literal có UTF-8 nhưng render theo byte | UTF-8 decode + range Arrows |
| Shadow panel lệch | fill solid rect offset trước khi vẽ panel, không alpha, không clip | Elevation level2 trong layer Overlay |
| Palette trống khi Play | panel palette bị `if(!playing)` | Khi Play: palette disabled + inspector read-only hiện actor đang chọn |

---

## 3. Studio thống nhất

### 3.1 Cấu trúc màn

```
┌ Top bar 40px: [Project ▾ farm] [badge dev c95febd8] ........ [▶ Play] [⏸] [■] [Ctrl+K] ┐
│ Nav 56px │ Canvas (workspace chính)                    │ Inspector 300px (splitter) │
│ Home     │                                              │                             │
│ Scene    │                                              │                             │
│ Map      │                                              │                             │
│ Texture  │                                              │                             │
│ Sprite   │                                              │                             │
│ Release  │                                              │                             │
│──────────┼──────────────────────────────────────────────┴─────────────────────────────│
│ Bottom 180px (splitter): tabs [Console] [Validation] [Assets] [Audit]                │
├ Status 24px: [mode] [zoom 200%] [cursor 12,34] [autosaved 10s ago] ..... [12.3 ms] ┤
```

(Ký hiệu ▶ ⏸ ■ chỉ minh hoạ; không dùng icon — thay bằng chữ `Play` `Pause` `Stop`.)

Layout lưu `assets/studio.layout` (text: `layout 1`, `inspector_w 300`, `bottom_h 180`, `last_workspace map`).

### 3.2 `studio_core` `[corrected — tên thật là `document_core`]`

Thư viện này **đã tồn tại** với tên `document_core` (`src/engine/document/`), và
`Workspace` đã tồn tại là `studioshell::Workspace` (`src/games/studio_shell/workspace.hpp`)
với chữ ký khác đề xuất dưới đây — khi triển khai, **đối chiếu interface thật**, đừng đổi
tên cái đang chạy.


```cpp
struct Command { std::string label; std::function<void()> apply, revert; uint64_t merge_key = 0; };
class CommandStack { void push_apply(Command); bool undo(); bool redo(); bool merge_with_last(); size_t depth(); };
struct EditorDocument { std::string path; bool dirty; CommandStack stack; virtual void serialize(std::string&) = 0; virtual bool deserialize(std::string_view) = 0; };
```

- Autosave mỗi 30 s nếu dirty → `<path>.autosave` qua `assets::write_file`; mở document thấy `.autosave` mới hơn file → prompt "Recover?".
- `merge_key`: các lệnh liên tiếp cùng key (vd kéo slider, kéo actor) gộp thành một bước undo.
- `Workspace` interface:

```cpp
struct Workspace { virtual const char* name() = 0; virtual void update(Ctx&, float dt) = 0; virtual void render_canvas(Ctx&, Rect) = 0; virtual void render_inspector(Ctx&) = 0; virtual void commands(std::vector<CommandInfo>&) = 0; virtual void on_asset_dropped(const AssetRef&, Vec2) {} };
```

### 3.3 `commands_core` registry

```cpp
struct CommandInfo { const char* id; const char* title; const char* hotkey; const char* workspace; /* null = global */ };
// `engine::OpResult{ok, message}` ĐÃ TỒN TẠI — release/ops.hpp:18-21. Dùng lại. [verified]
using Handler = std::function<OpResult(std::span<const std::string> args)>;
void register_command(CommandInfo, Handler);
OpResult run(std::string_view id, std::span<const std::string> args);
std::span<const CommandInfo> all();
```

- `main.cpp`: `--cmd project.publish <proj> development "reason"` gọi `run`. Các flag cũ (`--project-publish`…) giữ như alias 1 dòng gọi `run` → CI không đổi.
- Id đặt tên `<domain>.<verb>`: `project.new/inspect/package/publish/verify`, `release.promote/rollback/status/log`, `hub.recommend`, `scene.play/stop/step`, `edit.undo/redo`, `map.fill/paint`, `asset.add/remove`.
- Test: mỗi `CommandInfo` có handler; `--cmd` và keypress tăng cùng counter (mock).

### 3.4 Asset browser

- Nguồn: (a) dòng `asset` trong manifest (đã declared), (b) file dưới asset root chưa declared (badge `undeclared`).
- Type → badge: `map` `hrt` `png` `anim` `fx` `snd` `font` `def`.
- Thumbnail 64×64 render bằng core tương ứng vào `SubFramebuffer`, cache theo content hash.
- Kéo asset vào canvas → `Workspace::on_asset_dropped`. Chuột phải: Add to project / Remove / Reveal path / Copy path.
- Search box lọc theo tên.

**Format `.pack` (thêm 2026-09-06, RESEARCH-K §3.1).** Một pack asset ngoài mang theo
provenance của chính nó, thay vì để người import nhớ:

```
pack 1
name Tiny Farm
author Kenney
source https://kenney.nl/assets/tiny-farm
license CC0-1.0
version 1.0
category 2D
series Tiny
tags farm rpg map pixel
tile 16
preview preview.png
file tilemap.png tileset
file characters.png sheet fw=16 fh=16
```

- Card của asset browser hiển thị badge `category / series / license`, tile size, số file.
- `--cmd asset.license-report` **sinh** `assets/ATTRIBUTION.md` từ mọi `.pack` dưới asset
  root. Hôm nay file đó gõ tay và `CLAUDE.md` biến nó thành *một luật phải nhớ*; sinh ra
  thì không quên được. Art tự vẽ (`.recipe` / `.pix`) giữ nguyên dòng tay — provenance của
  chúng là chính file source.
- `resource_core` tính hash cả `.pack` → đổi pack thì đổi release id, đúng triết lý
  content-addressed.

### 3.5 Validation panel

Chạy `project_core` doctor + `resource_core` closure khi: mở project, sau mỗi save, khi bấm Validate. Mỗi issue: `severity badge · message · [Jump]`. Jump chuyển workspace và chọn asset/actor liên quan.

### 3.6 Release workspace (thay Hub-UI)

- Ba **channel card** theo hàng ngang, mũi tên chữ `→` giữa các card: `development → preview → production`. Card: badge channel, hash rút gọn + copy, trạng thái (`present · ==source` → badge success "in sync"; `drift` → warning; `missing` → danger), thời điểm audit gần nhất.
- Card "Source" bên trái: packagehash hiện tại, số resource, nút **Package** (preview manifest).
- Nút Primary = `hub_core::recommend()` (vd "Publish to development"). Nút khác là Secondary. Rollback là Danger.
- Modal xác nhận: title, diff (danh sách file thay đổi giữa source và channel — lấy từ hai package manifest), **text input reason bắt buộc**, checkbox "I understand production is affected" khi promote/rollback production.
- Audit timeline: bảng `time · op · channel · from → to · reason`, mới nhất trên cùng.
- Toàn bộ dữ liệu từ `hub_core` **đã là struct rồi** `[corrected]`: `HubView` +
  `HubChannel` (`hub/hub.hpp:20-36`), `hub_lines(HubView)` chỉ là bộ format và
  `build_hub_view()` là bộ lắp. **Không cần `model()` mới** — chỉ thêm trường vào
  `HubView` nếu workspace cần. Việc thật sự thiếu: `release_ops` **không có**
  `status()`/`log()` (chúng là hàm local trong `main.cpp:260-288`) → nâng lên core
  để workspace và CLI không sinh bản sao thứ hai.

### 3.7 Play viewport

- `SubFramebuffer` = `Image` + `Renderer2D` trỏ vào nó. `App` cho phép "scene con": Studio giữ `std::unique_ptr<Scene> playing`, gọi `update(1/60)` theo accumulator riêng, `render` vào sub-fb, blit scaled (integer) vào canvas rect.
- Play = snapshot document (`serialize`) → build scene từ `launch_entry(entry)`; Stop = huỷ scene + `deserialize` snapshot. Pause/Step (một tick).
- Input chuyển vào scene con chỉ khi canvas có focus (click vào canvas); `Esc` trả focus về Studio.

### 3.8 Scene workspace (từ Sandbox) & Map workspace (từ Map Lab)

Chung: zoom 25–800% (wheel), pan (space+drag / chuột giữa), grid + snap toggle, selection outline `accent` 1px + handle, multi-select drag rect, `Delete`, `Ctrl+D` duplicate, arrow key nudge, inspector đổi props qua `Command` (undo được).
Map: tool `paint/erase/fill/rect/pick`, layer list (ẩn/khoá), tileset panel (chọn tile, autotile toggle), entity tool, trigger tool.

### 3.9 Asset tooling (S7)

**Mixer** `[thêm 2026-09-06, RESEARCH-K §3.2 — ưu tiên TRƯỚC phần còn lại của Pixel
editor]` — sinh sprite từ *bộ phận + hoán màu theo index + frame*, data-driven:

```
mixer 1
name creature
size 32 32
frames 4 fps 6
palette base #3a2f2a #6b4f3a #c58f5a #e9d7b8
slot body   layer 0 required
slot wings  layer 3 optional
part body slime parts/body_slime.hrt anchor 16 24
rule wings requires body:not slime
```

- UI: tab theo slot, lưới part có preview, ramp palette bên phải, Random / seed, xem frame.
- Output: `.hrt` (+ `.anim`) + một dòng `asset` vào manifest; **`.mix` là *source*** — nằm
  ngoài manifest, giống `.recipe` và `.pix`, và một test re-bake so byte.
- Đây sẽ là **cửa offline thứ tư** vào `.hrt` (`--cmd asset.mix`). Câu "đúng ba cửa" trong
  `CLAUDE.md` phải được sửa **có chủ ý** trong cùng slice, kèm đúng hai luật của ba cửa
  kia: một dòng `ATTRIBUTION.md`, và một test bake-lại-so-byte.
- Ràng buộc **là** tính năng (Kenney Shape 64×64/16 màu): người dùng không cần vẽ giỏi,
  cần một bộ part tốt.

**Pixel editor** `[corrected]` — document **`.hrt`** (+ sidecar `.pxd` giữ
layer/palette, text versioned). **Không dùng PNG**: repo không có encoder *lẫn*
decoder PNG (`third_party/` chỉ có `stb_truetype.h`); `.hrt` thì đã có codec, đã
test, và `--anim`/`--sandbox` đã tiêu thụ. PNG là một bài học riêng, làm khi cần
trao đổi ra ngoài.
Tools: pencil (size 1–4), eraser, fill (contiguous/global), line, rect (fill/stroke), ellipse, eyedropper (Alt), select rect + move, mirror X/Y, dither pencil (checker 50%). Layer ≤ 8: opacity, visible, lock. Palette panel: import `.hex`/`.gpl`, lock palette (fill quantize về palette gần nhất), tối đa 64 màu. Onion skin ±1 frame khi mở sheet có `.anim`. Export PNG bằng **encoder tự viết** (zlib stored/fixed-Huffman đủ; deflate động là bài học tuỳ chọn).

**Autotile rule editor** — template blob 47 hiện dạng lưới có nhãn bitmask; click ô để gán tile từ tileset; preview map 8×8 vẽ nhanh để thấy kết quả; lưu vào tileset def (`tileset 1` … `autotile <name> <47 ids>`).

**Texture Lab v2** `[corrected — `.recipe`, không phải `.hrt`]` — `.recipe` hiện
là `key=value` phẳng (`studio/recipe.cpp:10-25`); `.hrt` là format **raster**
(`HRT1 | BE w | BE h | RGBA8`). `.recipe` v2 là danh sách bước: `noise value|perlin|fbm <params>` → `quantize <palette>` → `dither bayer4|bayer8|none` → `outline <color> <thick>` → `emboss <strength>` → `tile <w> <h>`. Mọi bước deterministic theo seed. Export: tile đơn, tileset 4×4 biến thể, sheet anim (nước) như cũ.

**Sprite/Anim editor** — `.anim 1`: `sheet <png> <fw> <fh>`, `clip <name> <fps> <loop> <frame ids>`, `pivot <x> <y>`, `hitbox <clip> <frame> <x y w h>`. Preview play, chọn clip, kéo pivot.

**FX/Light/Audio** → inspector component trên actor trong Scene workspace: `Emitter{preset, rate, life, spread, color}`, `Light{radius, color, flicker}`, `Sound{clip, trigger: on_spawn|on_overlap}`.

---

## 4. Lớp runtime dùng chung cho game 2D

### 4.1 `tilemap_core` — format `map2`

```
map2 1
name farm_home
size 64 48
tile 16
tileset ground tilesets/farm_ground.tsdef
tileset decor  tilesets/farm_decor.tsdef
layer ground  tiles ground   <64*48 ids, dòng theo hàng, 0 = trống>
layer decor   tiles decor    <...>
layer overlay tiles decor    <...>              # vẽ trên entity
layer collide mask           <64*48 0/1>
entity npc_anna 12 7 sched=anna.sched dialog=anna.dlg
entity spawn_player 5 5
trigger door_house 10 3 2 1 target=house.map2 at=3 9
trigger encounter_grass 20 20 8 6 table=route1
```

Tileset def `.tsdef`: `tileset 1`, `image <png>`, `tile 16`, `autotile <name> <47 ids>`, `anim <id> <frames> <fps>`, `flag <id> water|solid|grass`.
Migration `fpsmap1 → map2`: ground = wall ids, collide = wall≠0, entity spawn.

API: `load/save`, `get(layer,x,y)`, `set` (trả `Command`-friendly diff), `solid(x,y)`, `triggers_at(rect)`, `autotile_resolve(layer, x, y)` (đọc 8 hàng xóm, tra bảng 47), `draw(list|Renderer2D, camera, layers)` với culling.

### 4.2 `camera2d`
`Camera2D{pos, zoom, bounds, deadzone, smooth}`; `follow(target, dt)` dùng exponential smoothing; `snap()` làm tròn về pixel để tránh rung; `world_to_screen/screen_to_world`.

### 4.3 `scene_stack`
`push(std::unique_ptr<Scene>)`, `pop()`, `replace()`; chỉ scene trên cùng nhận input; scene dưới vẫn render nếu `translucent()` (menu pause đè lên game). Tương thích `App` hiện tại: `App` giữ một `SceneStack` thay vì một `Scene` `[verify]`.

### 4.4 `dialogue_core`
Format `.dlg 1`: `node <id>`, `say <speaker> <text>`, `choice <text> -> <node>`, `set <var> <val>`, `if <var> == <val> -> <node>`, `end`. Runtime: iterator, typewriter (chars/s), biến trong `SaveState.vars`. Test: chạy script, chọn nhánh, kiểm biến.

### 4.5 `save_core`
`SaveState{ version, game_id, vars map<string,string>, sections map<string,string> }` text versioned; `migrate(from, to)` hook chain; local slot `saves/<game>/<slot>.sav`; cloud: đẩy nguyên text lên cloud save của BaaS với `If-Match` version; conflict → hỏi "keep local / keep cloud". Autosave khi ngủ/đổi map.

### 4.6 `input_map`
Action-based: `Move{x,y}`, `A`, `B`, `Menu`, `Start`. Nguồn: keyboard (WASD/arrows, Z/X hoặc Space/Esc), gamepad (SDL) `[verify platform seam có gamepad]`, **virtual touch** (web: d-pad + 2 nút vẽ bằng HTML overlay, gửi vào platform input qua EM_JS). Rebind lưu `assets/input.cfg`.

### 4.7 `theme_hud` + HUD components (dùng `ui` v2 với DrawList riêng, font pixel)
Tokens HUD: nền panel `#1B1B2B` @ 90%, viền 1px `#F4F1DE`, chữ `#F4F1DE`, nhấn `#F2CC8F`, nguy `#E07A5F`, tốt `#81B29A`. 9-slice panel từ `hud/panel.png` (16×16, 5px corner). Components: `hotbar(items, selected)`, `bar(value, max, color, label)`, `clock(day, time, season)`, `dialogue_box(speaker, text, typed_chars, choices)`, `toast_hud`, `menu_list`, `minimap`. Golden test cho từng cái ở 640×360.

---

## 5. Game 1 — Farm (Stardew-like, top-down)

### 5.1 Fantasy & loop
Thừa kế mảnh đất bỏ hoang; mỗi ngày quản lý **năng lượng** và **thời gian** để trồng trọt, bán, làm thân với dân làng, mở rộng. Loop 12 phút thật = 1 ngày (06:00 → 02:00), có mùa 28 ngày.

### 5.2 Hệ thống (MVP → v1)

| Hệ | MVP (S5) | v1 (S8) |
|---|---|---|
| Di chuyển | 4 hướng, va chạm tile | chạy (Shift), tương tác hướng nhìn |
| Thời gian | clock, ngủ để qua ngày, energy 100, hết → ngất về nhà | mùa, thời tiết (nắng/mưa; mưa tự tưới, particles + light giảm) |
| Nông | hoe/water/plant/harvest, 1 crop (parsnip 4 ngày) | 8 crop / 4 mùa, tưới héo sau 2 ngày, chất lượng ngẫu nhiên, scarecrow |
| Kinh tế | ship box: bán lúc ngủ | shop Pierre-like: mua hạt/công cụ nâng cấp; giá từ **remote config** |
| NPC | 1 NPC, schedule 3 điểm, dialogue 5 câu | 2 NPC, friendship 0–10 tim, quà thích/ghét, dialogue theo tim, sự kiện tim 4 |
| Kho | 24 ô, hotbar 8 | rương, sort |
| Câu cá | — | minigame thanh căng: giữ A để nâng thanh, cá di chuyển sin+noise, giữ cá trong thanh 3 s |
| Mỏ | — | (optional) 5 tầng procedural bằng noise Texture Lab, đá/quặng, 1 loại slime, sword |
| Save | local slot | cloud save, sync native↔web, conflict prompt |
| LiveOps | — | analytics `day_end{gold, crops, energy_used}`, `harvest{crop}`, `sale{item, price}`; live event `festival` (ngày 14: NPC tụ ở quảng trường, bán giá ×1.5) |

### 5.3 Data files (text versioned trong `assets/farm/`)
- `crops.def`: `crop parsnip season=spring days=4 stages=5 sell=35 seed=20 regrow=0`
- `items.def`: `item hoe type=tool tier=1`, `item parsnip type=crop sell=35 edible=25`
- `npc/anna.sched`: `spring mon 06:00 home | 09:00 shop | 17:00 square | 21:00 home`
- `npc/anna.dlg`, `npc/anna.gifts`: `love parsnip ; hate stone`
- `shop.def`: giá gốc, override bằng remote config key `farm.price.<item>`

### 5.4 Mô phỏng deterministic (`farm_core`)
`World{clock, weather, tiles: map<pos, Soil{tilled, watered, crop, stage, days_since_water}>, inventory, npcs, vars}`; `tick(dt)` chỉ đổi clock; `end_day(rng)` xử lý growth/weather/shop restock. Mọi random qua `Rng` seeded từ `(save_seed, day)` → test `test_farm_sim` mô phỏng 28 ngày và so hash.

### 5.5 Map & asset
Map: `farm_home` 64×48, `town` 48×40, `house` 16×12, `shop` 12×10 (+ `mine_N` gen). Tileset ngoài CC0 (đề xuất: bộ farm/RPG 16×16 phổ biến trên OpenGameArt/itch, ghi license). Mục tiêu S7: thay dần bằng tileset vẽ trong Studio.

### 5.6 Controls
Keyboard: arrows/WASD, `Z/Space` = dùng công cụ/tương tác, `X/Esc` = menu, `1–8` hotbar, `E` kho. Touch: d-pad + A/B + tap hotbar.

---

## 6. Game 2 — Creature RPG (Pokémon-like)

### 6.1 Loop
Đi qua route → gặp creature trong cỏ → đánh/bắt → luyện party → thắng gym → mở vùng mới. MVP: 3 town, 2 route, 1 gym, 18 creature.

### 6.2 Data (`assets/creatures/`)
- `types.def`: 6 type `fire water grass electric rock normal` + bảng hiệu quả 6×6 (0.5/1/2).
- `species.def`: `species 001 emberpup type=fire hp=39 atk=52 def=43 spd=65 exp=fast evolve=016@16 moves=1:tackle,5:ember,12:bite catch=190`
- `moves.def`: `move ember type=fire power=40 acc=100 pp=25 effect=burn:10`
- `encounters.def`: `table route1 grass 001:20:2-4 004:20:2-4 007:10:3-5`
- `trainers.def`, `gym.def`.

### 6.3 Battle sim (`creature_core::battle`) — **toàn số nguyên, deterministic**
- State: hai bên `Party`, active index, weather (bỏ ở MVP), turn count, `Rng` (xorshift64 seeded).
- Turn: mỗi bên chọn `Action{Move i | Switch j | Item k | Run}` → resolve theo priority rồi speed (tie → rng); damage = `((2*L/5+2)*P*A/D)/50+2` × STAB 1.5 × type × random 85–100% (integer math). Status: burn/paralyze/sleep.
- `step(state, actionA, actionB) -> Events[]` (dùng cho UI animation + log). `hash(state)` FNV-1a toàn bộ state.
- Replay = `seed + list<(actionA, actionB)>`; test replay 1000 trận random → hash cuối khớp khi chạy lại.
- AI: chọn move damage cao nhất theo type, đổi khi HP < 20% nếu có lợi.

### 6.4 Overworld
Grid 16px, di chuyển từng ô với tween; `map2` trigger `encounter_grass` gọi bảng; NPC trainer có tầm nhìn (line) → battle; healing center; PC box (30 ô); save = `SaveState` + party.

### 6.5 PvP realtime (S11)
- Matchmaking BaaS → room. Handshake: cả hai gửi `party_hash`; server (hoặc host) phát `seed`.
- Mỗi lượt: gửi `{turn, action, state_hash_before}`; nhận đủ 2 → cả hai `step`; nếu `state_hash` khác → desync → gửi `analytics desync` + huỷ trận. Timeout 30 s/lượt → auto "struggle".
- Kết quả → leaderboard ELO (K=32, tính client rồi server xác nhận bằng replay `[đơn giản hoá: server tin client, ghi audit]`).
- Replay trận PvP đẩy lên `/v1/replays` (≤ 512 KiB đủ vì chỉ actions).

### 6.6 HUD
Battle screen 640×360: sprite hai bên, HP bar, menu 4 ô (Fight/Bag/Party/Run) → 4 move với PP/type badge, log typewriter. Overworld: dialogue box, menu list.

---

## 7. Web: `shell.html`, Collection, touch

### 7.1 `shell.html`
- Layout: header 48px (tên project, mode selector `<select>`, nút Fullscreen, link GitHub) · canvas giữa integer-scale tối đa vừa viewport, nền `bg.base` · footer hint phím.
- Loading: progress bar (Emscripten `Module.setStatus`), ẩn canvas cho đến khi `onRuntimeInitialized`.
- `?mode=<flag>&project=<path>` như hiện tại; `?mode=collection` → §7.2.
- CSS variables đúng token §2.1 (một file `web/tokens.css` dùng chung với dashboard).
- Touch: khi `pointer: coarse` → overlay d-pad trái, A/B phải, nút Menu; sự kiện gọi `Module._input_virtual(action, down)` (export C) → platform input.

### 7.2 Collection page (server-rendered bởi `webserver` tự viết hoặc static)
- Nguồn: quét `assets/projects/*.gameproject` + `cover` (thêm dòng `cover <png>` và `summary "<text>"` vào manifest schema v2 với migration).
- Card: cover 320×180, tên, summary, badge channel production hash, nút **Play** (→ `demo.html?mode=project&project=…&channel=production`), nút "Read chapter".
- Trang này cũng là landing của README (link trong README).
- **README template kiểu Starter Kit** (RESEARCH-K §3.3) cho *mỗi* `.gameproject`, render
  trên trang chi tiết: `Features` (5–8 gạch đầu dòng, chỉ thứ chơi được) · `Screenshot/GIF`
  · `Controls` (bảng Key | Touch | Command) · `Instructions` (thêm crop/creature ở file
  nào, thêm map ở workspace nào, save/cloud save, publish) · `License` (code MIT · asset
  xem `ATTRIBUTION.md`) · link chapter tương ứng.

### 7.3 Webserver
Thêm: `Content-Encoding: gzip` nếu có file `.gz` sẵn bên cạnh (pre-compressed ở build, không nén runtime); cache header cho `.wasm`; route `/collection`. Test: `test_webserver` mở rộng.

---

## 8. Dashboard BaaS (HTML/CSS/JS vanilla, không framework)

- `web/tokens.css` dùng chung; layout sidebar 220px + content; responsive < 800px → sidebar thành top tabs.
- Trang: **Overview** (health, releases/channels từ platform, request/s, error rate từ `/metrics`), **Players** (bảng, search), **Leaderboard**, **Config** (bảng key/value, edit inline, nút Revert với audit), **Analytics** (per release: bảng + biểu đồ bar/line vẽ bằng `<canvas>` tự viết — không lib), **Events** (live events: tạo/sửa/bật), **Replays** (list, tải), **Audit**, **Settings** (secret rotation, RBAC).
- Form: mọi hành động sửa đổi có confirm + reason (khớp Studio). Toast góc phải.
- Auth: giữ hai cấp; UI hiện rõ đang ở cấp nào (badge).

---

## 9. Testing

| Suite | Kiểm gì |
|---|---|
| `test_text` | UTF-8, atlas cache, measure vs draw, wrap/ellipsis |
| `test_ui_layout` | rect từ row/column/flex/splitter ở 3 kích thước |
| `test_ui_widgets` | state machine click/focus/text input/clipboard; id stack collision |
| `test_ui_golden` (mở rộng) | type specimen, mỗi widget × 5 state, Release workspace × 3 trạng thái, HUD components |
| `test_studio_core` | undo/redo/merge, autosave/recovery |
| `test_commands` | registry đầy đủ, `--cmd` ≡ keypress |
| `test_tilemap` | round-trip, migration fpsmap1, autotile, collision, camera |
| `test_dialogue`, `test_save` | script, migration chain |
| `test_farm_sim` | 28 ngày deterministic hash |
| `test_battle` | 1000 replay deterministic; bảng type; formula edge |
| `test_pixel_editor`, `test_png_encoder` | ops + round-trip |
| CI | thêm job: build web + upload screenshot golden làm artifact; job Docker amd64 `/healthz` (S12) |

Golden `[corrected]`: **không so byte.** `test_ui_golden.cpp:9-12` đã ghi rõ lý do
cố ý *không* hash pixel — AA làm tròn khác nhau giữa compiler/kiến trúc (native vs
web), nên hash không portable. Giữ kiểu hiện có: **màu vùng đặc phải khớp token +
phải tồn tại pixel AA**, cộng dump PPM để soi bằng mắt. Mở rộng theo widget/màn,
không đổi cơ chế.

---

## 10. Docs, ADR, chapter

- Tạo `docs/adr/` với template `NNN-title.md` (Context / Decision / Consequences / Ceiling). ADR-001 UI v2 & DrawList · 002 Resolution & scale · 003 map2 · 004 commands registry · 005 save schema · 006 PvP determinism.
- Chapter 108–122 theo bảng PLAN §4; mỗi chapter mở đầu bằng `## Tóm tắt (VI)` 10–15 dòng, phần còn lại tiếng Anh; cuối chương "What is verified, and what is not".
- Chapter cũ bị hấp thụ (sandbox, maplab, studio, fx, light, anim, audio, hub-ui, shell, editor) thêm khối `> Moved: this lives in Studio → <workspace> since ch. NNN.`
- `CLAUDE.md`: cập nhật bảng mode (còn: default, `--gui/--tui`, `--fps`, `--3d/--viz3d`, `--studio`, `--project`, headless verbs, `--cmd`, `--runner`) và mục "Studio workspaces".
- `PROJECT-BRIEF.md`: §3 số liệu, §4 Block 2 bảng mode, §7 thêm slices, §8 ledger, §10 gaps (xoá #4, #5 khi xong).
- `README.md` (open source): GIF Studio + GIF hai game, "Play in browser" link, kiến trúc 1 hình, "learning project" disclaimer, license code (đề xuất MIT) + `ASSETS-LICENSE.md`.

---

## 11. Track C — OPS (S12)

- **CI**: `gh auth login`; chạy workflow; sửa đến xanh; thêm badge vào README. Ghi vào ledger ngày xác minh.
- **Docker**: job CI `docker build` trên `ubuntu-latest` (amd64) + `curl /healthz`; cache layer. Local arm64 vẫn có thể skip.
- **Postgres** `[corrected]`: `IStore` **không tồn tại** — service gọi thẳng
  `drogon::orm::DbClient` (`baas/db/db.h:38-39`), nên phải *tạo* seam trước. 2 adapter; `docker-compose` thêm service `postgres`; migration runner chạy cả hai; test `baas_*` chạy 2 lần với env `STORE=sqlite|postgres` (Postgres chỉ trong CI).
- **TOCTOU purchase** `[corrected — phải đi CÙNG bước Postgres]`: hôm nay **an
  toàn** vì SQLite pool = 1 (comment `ponytail:` tại `inv_service.cc:127-131`); lỗ
  hổng chỉ mở lại khi pool > 1. Nên đây **không phải mục riêng sau Postgres** — nó
  là điều kiện để bật Postgres. SQLite `BEGIN IMMEDIATE` + `UPDATE wallet SET balance=balance-? WHERE user=? AND balance>=?` kiểm `changes()==1` rồi grant, cùng transaction; Postgres `SELECT … FOR UPDATE`. Test concurrency: 50 luồng mua cùng lúc với balance đủ cho 10.

---

## 12. Thứ tự file/thư mục mới (để dễ hình dung diff)

```
src/engine/ui/            theme.hpp theme_dark.hpp theme_hud.hpp drawlist.* layout.* widgets.* ids.*
src/engine/text/          font_atlas.* utf8.*
src/engine/commands/      registry.*                       (commands_core)
src/engine/tilemap/       map2.* tileset.* autotile.* camera2d.*   (tilemap_core)
src/engine/scene_stack.*  dialogue/  save/  input_map/
src/games/studio/         studio_scene.* workspaces/{home,scene,map,texture,sprite,release}.* tools/{pixel_editor,autotile_editor,anim_editor}.*
src/games/farm/           farm_core/ (sim) + scene/ (render, HUD)
src/games/creatures/      creature_core/ (data, battle) + scene/
assets/projects/          farm.gameproject creatures.gameproject
assets/farm/ assets/creatures/ assets/tilesets/ assets/hud/ assets/fonts/
web/                      shell.html tokens.css collection.html dashboard/{index.html, app.js, ...}
docs/adr/                 001..006
tests/golden/
ASSETS-LICENSE.md CONTRIBUTING.md
```

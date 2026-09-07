# PROGRESS — trạng thái thực thi lộ trình

> Đọc file này **trước** khi làm tiếp. `PLAN.md` = làm gì và theo thứ tự nào ·
> `SPEC.md` = làm như thế nào (đọc §0 trước — nó liệt kê chỗ bản gốc đoán sai) ·
> file này = **đã tới đâu, cái gì đã CHẠY, việc kế tiếp là gì**.
>
> Quy tắc ghi: chỉ đánh ✅ cho thứ **đã chạy và thấy kết quả**. Thứ mới viết ra mà
> chưa chạy thì ghi ⚠️ và nói rõ chưa chạy ở đâu. Đây là cùng một kỷ luật với
> `PROJECT-BRIEF.md §8` và mục "What is verified, and what is not" của các chương.

**Baseline khi bắt đầu:** `main` @ `7dfcac9`, 61/61 test xanh (đã chạy 2026-09-04, 24 s).

---

## Bảng slice

| Slice | Tên | Trạng thái | Branch | Chapter |
|---|---|---|---|---|
| Phase 0 | Housekeeping repo | ✅ xong | *(main)* | — |
| Phase A | Sửa PLAN/SPEC theo code | ✅ xong | `docs/new-plan-corrections` | — |
| S1 | UTF-8 + độ nét + Studio dùng design system | ✅ xong | `feat/s1-text-utf8` | 108 |
| S2 | `ui` v2 (seam → clip → overlay → id → focus → layout → widget) | ✅ xong | `feat/s2-ui-v2` | 109 |
| S3 | `tilemap_core` v2 + `camera2d` | ✅ xong | `feat/s3-tilemap` | 110 |
| S4a | `commands_core` + undo/autosave (nền) | ✅ xong | `feat/s4-commands` | 111 |
| S4b | Map workspace + command palette (consumer) | ✅ xong | `feat/s4b-map-workspace` | 112 |
| S5 | Farm — vertical slice | ✅ xong | `feat/s5-farm` | 113 |
| S6 | Asset browser · Validation panel · audit log trong cửa sổ | ✅ xong | `feat/s6-project-workspace` | 114 |
| S6b | Play viewport + `FixedStep` + một bảng entry | ✅ xong | `feat/s6b-play-viewport` | 115 |
| S7 | Sandbox → workspace thứ hai + `Workspace` interface (D23) | ✅ xong | `feat/s7-workspaces` | 116 |
| S8 | Farm v1 + BaaS + HUD | ⬜ chưa | | 117 |
| S9 | Web: shell, Collection, touch, dashboard | ⬜ chưa | | 118 |
| S10 | Creature RPG — MVP | ⬜ chưa | | 119–120 |
| S11 | Replay + PvP realtime + leaderboard | ⬜ chưa | | 121 |
| S12 | OPS (CI · Docker · Postgres **+ FOR UPDATE cùng nhau**) | ⬜ chưa | | 122 |
| S13 | Stretch (Terraria-like · Chess online) | ⬜ chưa | | 123+ |

---

## Nhật ký

### Phase 0 — Housekeeping ✅ 2026-09-04

- `.codegraph/` vào `.gitignore`; xoá file rác `resume` — `5f59c02`.
- Commit công việc treo từ session trước: Guide tab của shell + `PROJECT-BRIEF.md`
  + bản sửa độ chính xác của `CLAUDE.md` — merge `7dfcac9`.
- Xoá **23 branch đã merge** + gỡ **1 worktree treo**
  (`~/Documents/Codex/…/game-platform-strategy`, branch
  `docs/game-platform-strategy-2026`). Đã kiểm: branch đó không có nội dung riêng —
  `main` chứa đủ và mới hơn. Còn lại đúng `main`.
- ✅ **Đã chạy:** `ctest` 61/61 xanh trước khi động vào code.

### Phase A — Sửa PLAN/SPEC ✅ 2026-09-04

Bản gốc viết trước khi đọc `src/`. Đã đối chiếu toàn bộ và sửa tại chỗ; xem
`SPEC.md §0` cho danh sách đầy đủ. Bốn sửa quan trọng nhất:

1. **Design system đã tồn tại** (ch. 68–71: font AA, SSAA + Wu + coverage, token,
   rounded rect, soft shadow, golden test). Không xây lại, và **không đổi tên token**.
2. **3/4 bug chẩn đoán sai.** Chữ thô = thiếu `supersample` ở 2 scene, không phải
   baseline. `???` = thiếu UTF-8 decode, không phải thiếu glyph. "Nhìn như in text"
   = `studio_shell`/`hub` không dùng `ui::Context`, không phải thiếu widget.
3. **S1 nhỏ hơn, S2 lớn hơn** ước lượng gốc.
4. **Postgres + TOCTOU `FOR UPDATE` phải đi cùng slice** — lỗ hổng chỉ mở lại khi
   pool > 1.

### S1 — UTF-8, độ nét, Studio dùng design system ✅ 2026-09-04 · chương 108

Merge `feat/s1-text-utf8`. Bốn commit:

| Commit | Việc |
|---|---|
| `085c76a` | `--shell`/`--hub-ui` thiếu `supersample = kAA` — hai scene windowed duy nhất. `--shell` lên 1280×720; chiều cao panel lấy từ framebuffer thay vì hằng số phải khớp tay với `main.cpp`. |
| `386e41c` | `engine/text/utf8.hpp` + `Font` khoá theo codepoint. Byte hỏng → U+FFFD **và chỉ nuốt 1 byte**; overlong/surrogate bị từ chối. Codepoint không có outline → ô rỗng thay vì `'?'`. Sửa lệch làm tròn `text_width` ÷ `ss_`. |
| `7c1d3aa` | `engine::next_action` (quyết định dạng value) + `hubui::draw_hub_panel` dùng chung cho `--hub-ui` và tab Hub. Xoá 33 literal màu. `test_shell_golden` chạy cả shell không cần cửa sổ. |
| `c11f529` | `--bench-ui` — số đo trước khi S2 làm UI phình. |

**✅ Đã chạy và thấy kết quả:**
- `ctest` **62/62 xanh** (thêm `shell_golden`).
- Render shell ra ngoài màn hình ở đúng 1280×720×ss2 rồi **soi ảnh**: `→`, `…`,
  `—`, `–`, `×` đều hiện đúng. Không còn `???`.
- Web build (Emscripten) vẫn xanh — `demo.js` + `demo.wasm` sinh ra bình thường.
- `--bench-ui` sau khi thêm warm-up, 5 lần chạy: **Release ss=2 median 2.4–4.4 ms**
  (khoảng 1/3 ngân sách 8 ms); Debug ss=2 **11–18 ms** (vượt ngân sách — nhưng Debug
  không phải cấu hình ship). Số đo đầu tiên (4.63 ms) **sai** vì tính cả frame khởi
  động; đã sửa.

**⚠️ Chưa chạy / chưa xác minh:**
- **Chưa mở cửa sổ thật.** Môi trường này không có quyền screen-capture, nên mọi
  khẳng định thị giác dựa trên bản render ngoài màn hình (cùng scene, cùng renderer,
  cùng kích thước và supersample — nhưng **không** qua đường `present()` của SDL).
- **Số tuyệt đối không tin được quá ~2×** — máy laptop arm64, scheduler + nhiệt làm
  median dao động giữa các lần chạy. Chỉ **tỉ lệ** là ổn định: ss=2 ≈ 4× ss=1 (nên
  tải là fill-bound, không phải logic-bound), Debug ≈ 4–5× Release. Cách dùng đúng
  ở S2: chạy lại `--bench-ui` **cùng máy, cùng phiên**, trước/sau, rồi so tỉ lệ.
- Chưa thử chữ CJK thật (không bundle font CJK); không có shaping/kerning/bidi —
  đây là bộ vẽ theo codepoint, không phải text shaper.

### S2 — `ui` v2 ✅ 2026-09-04 · chương 109

Merge `feat/s2-ui-v2`. Bảy commit, đúng thứ tự bắt buộc (mỗi bước mở khoá bước sau):

| Commit | Việc |
|---|---|
| `46726d8` | Platform seam: `Key` đủ bảng chữ cái + Home/End/PageUp/Down + F1–F12; `Mods`; `SDL_TEXTINPUT`; wheel; key repeat; `resizable` + `quit_on_escape`. |
| `825ef38` | Test shell ở 4 kích thước cửa sổ — bắt được hàng nút tràn khỏi mép phải dưới ~1000px. |
| `6b9aa91` | `set_cursor` + clipboard ở seam. |
| `2706a80` | Clip stack trong `Renderer2D` — **giao**, không thay. |
| `2f9adf8` | Id stack, focus bàn phím, input dạng **ý định** (`ui_input.hpp` là nơi duy nhất biết Cmd vs Ctrl). |
| `f1255f6` | Badge, overlay hoãn, `begin_inert`, modal confirm + `fill_rect_blend` + token còn thiếu. |
| `ed8bb05` | `text_input` (caret đi theo ranh giới UTF-8), scroll, tabs, list row. |
| `5cee7a0` | Viết lại Hub panel + Studio shell: layout engine, confirm **bắt buộc nhập lý do**, toast, copy hash. |

**✅ Đã chạy và thấy kết quả:**
- `ctest` **62/62 xanh**.
- **Mutation test** (không chỉ tick xanh): thay bước ranh giới UTF-8 bằng `at-1` → 4 assertion đỏ;
  bỏ kiểm `shift` trong di chuyển caret → 1 assertion đỏ. Test có thật.
- Render màn confirm ra ngoài màn hình ở đúng 1280×720×2 và **soi ảnh**, kèm
  **negative control**: có lý do → nút accent; vừa mở, chưa nhập → nút disabled.
- Web build (Emscripten) vẫn xanh.
- `--bench-ui` cùng máy cùng phiên, **trước → sau S2**: Release ss=2 `2.4–4.4 ms → 1.4–2.6 ms`.
  Toàn bộ tầng UI mới **không tốn gì đo được** — tải là fill-bound, đúng như ch.108 kết luận.

**⚠️ Chưa chạy / chưa xác minh:**
- **Chưa mở cửa sổ thật**; cũng **chưa resize thật** (không có quyền Accessibility để lái
  cửa sổ). Phần kiểm được là scene tự bố cục đúng ở 4 kích thước framebuffer.
- Clipboard **chưa chạy với clipboard thật của OS** — widget test bằng fake tiêm vào.
- `set_cursor` **chưa có consumer** (splitter chưa tồn tại).
- Chưa thử IME / gõ không phải Latin.

### S3 — `tilemap_core` + `camera2d` ✅ 2026-09-04 · chương 110

Merge `feat/s3-tilemap`.

- **`map2`**: layer có tên (tiles hoặc mask), entity (điểm + props `key=value`),
  trigger (rect + props), tileset ref. Tile id là **int32** (trần 255 của `fpsmap1`
  vô hình cho tới lúc vượt, và mở rộng sau lại tốn thêm một migration).
- **`load()` tự nhận magic** → đọc cả `map2` lẫn `fpsmap1`. Migration tách `id` của
  `fpsmap1` (vốn gộp *hình dạng* + *có đặc không*) thành layer `wall` + mask `collide`;
  dòng `spawn` thành entity.
- **Từ chối file version tương lai.** Parser cũ đọc schema mới = âm thầm mất trường
  rồi ghi mất mát đó xuống đĩa.
- **`Camera2D`**: deadzone, smoothing **độc lập framerate**, clamp bounds (thế giới
  nhỏ hơn viewport thì **căn giữa**, không ghim góc), snap pixel nguyên, culling.
- **Autotile 47-blob**: bảng **sinh bằng liệt kê 256 mask**, nên 47 là *kết quả*.
  **Chưa** làm định dạng file tileset — nó đi cùng editor (S7).

**✅ Đã chạy:**
- `ctest` **63/63 xanh**.
- `--fps` **đã chuyển sang** `fps::from_shared_text` → migration được chạy bởi app và
  bởi CI golden path, không chỉ bởi unit test. `test_fps` đọc **level thật trong repo**
  bằng cả hai đường và so từng ô + spawn → giống hệt.
- **Mutation test**: lerp theo frame thay vì theo giây → đỏ test framerate;
  bỏ một quy tắc đường chéo → autotile count lệch khỏi 47, đỏ 40+ assertion.
- Web build xanh.

**⚠️ Chưa xác minh:**
- Chưa nhìn `--fps` chạy thật qua đường mới (không mở được cửa sổ).
- **Chưa có file `map2` nào do người viết ra** — Map Lab vẫn ghi `fpsmap1`. Layer /
  trigger / entity đã chứng minh parse + query được, **chưa** chứng minh là thứ một
  editor sinh ra.
- `iso::TileMap` **chưa** migrate (không có text format để migrate, và consumer đang chạy).
- **Chưa có gì render map2** — culling + y-sort đi cùng game đầu tiên cần chúng.

### S4a — `commands_core` + undo/autosave ✅ 2026-09-04 · chương 111

Merge `feat/s4-commands`. **Tách S4 làm đôi**: đây là phần nền (registry + undo +
autosave). Phần hấp thụ Sandbox/Map Lab vào Studio là **S4b**, chương 112 — tách ra
để không merge một slice nửa vời.

- **`commands_core`**: registry `{id, title, hotkey, args_help}` + handler trả
  `engine::OpResult`. `--cmd <id> [args]`; không id thì liệt kê. **Flag cũ giờ là
  alias thật** (`--project-publish` gọi `cmd::run("project.publish")`), nên "CLI verb
  và nút bấm là cùng một code" do compiler bảo đảm, không phải do tài liệu khẳng định.
- **Nối alias lộ ra lỗ hổng thật**: CLI mặc định lý do = chuỗi rỗng → audit log có
  dòng **không lý do**. Đã chặn: mọi tham số của lệnh mutating phải có và **không rỗng**.
- **`status()` / `log()`** nâng từ `main.cpp` vào `release_ops_core`, trả **dữ liệu**.
  `main.cpp` giữ định dạng cột riêng — không phải trùng lặp: nguồn dữ liệu một chỗ,
  trình bày là việc của caller (đúng như `ops.hpp` viết từ đầu).
- **`document_core`** (tên `document` vì `studio_core` đã là Texture Lab):
  `CommandStack` (apply khi push · sửa mới xoá nhánh redo · gộp gesture giữ **revert
  đầu** + **apply cuối** · **dirty là vị trí, không phải cờ**) và autosave/recovery
  (**đề nghị**, không tự áp dụng · autosave trùng nội dung = rác, không hỏi · autosave
  rỗng = coi như không có).

**✅ Đã chạy:** `ctest` **65/65**; `--cmd` liệt kê + chạy thật; flag cũ hành xử y hệt;
publish thiếu lý do bị từ chối (exit 1); web build xanh.

**⚠️ Chưa xác minh — nói thẳng:** **chưa có gì trong Studio dùng những thứ này.**
Registry có 5 lệnh và chưa có palette; `CommandStack` chưa có workspace nào push vào;
autosave chưa có timer nào chạy. Đây đúng là "motion without connection" — chấp nhận
được **chỉ vì** consumer là S4b ngay kế tiếp, không phải một hy vọng.

### S4b — Map workspace + command palette ✅ 2026-09-04 · chương 112

Merge `feat/s4b-map-workspace`. Đây là **consumer** của S3 + S4a — đóng đúng cái nợ
mà chương 111 đã ghi thẳng ra.

- **`map_edit_core`**: mọi thao tác sửa ô trả về một `doc::Command`. Một **nét vẽ =
  MỘT bước undo**, nhưng **không dùng `merge_key`**: stack gộp bằng "revert đầu +
  apply cuối", đúng cho cú kéo mà trạng thái cuối bao trùm, **sai** cho cú kéo tích
  luỹ — revert đầu chỉ khôi phục ô đầu tiên. `Stroke` tích luỹ rồi đẩy vào stack
  một lần, mang theo giá trị cũ của **từng ô**.
- **Studio mở thẳng vào workspace Map**: paint/rect/flood, layer + mask collision,
  chuột phải xoá, wheel zoom, kéo chuột giữa để pan, `Cmd+Z`, `Cmd+S`, autosave theo
  đồng hồ, hỏi recovery khi mở.
- **Nav rail rời khỏi phím mũi tên** sang `Cmd+1..5` — canvas cần mũi tên, và "một
  phím mang hai nghĩa thì chẳng mang nghĩa nào".
- **`Cmd+K`** liệt kê `cmd::all()` (khớp **subsequence**, giữ thứ tự đăng ký). Workspace
  đăng ký `map.*` **gắn với chính nó** và **gỡ đăng ký trong destructor** — handler
  bắt `this` mà sống lâu hơn `this` là một cú gọi vào bộ nhớ đã giải phóng.

**✅ Đã chạy:**
- `ctest` **68/68 xanh**; **ASan + UBSan sạch** toàn bộ suite.
- **Mutation test**: revert ghi trạng thái sau, tắt lọc no-op, bỏ kiểm giá trị của
  flood — mỗi cái đều làm test đỏ.
- Render màn Map + palette ra ngoài màn hình ở 1280×720×ss2 và **soi ảnh**.
- `--bench-ui` Release ss=2 **1.2–1.4 ms** — không đổi so với sau S2.
- Golden path của CI chạy lại đầy đủ; web build xanh.

**Hai bug thật do xây consumer mà lộ ra** (4 suite xanh trước đó không thể thấy):
1. **`map2` không round-trip được layer `tiles` không có tileset** — đúng hình dạng
   mà một editor sinh ra trước khi có art. `to_text` ghi thiếu trường, `load` đọc token
   `row` kế tiếp làm tên tileset. Nay tileset rỗng viết là `-`, và có test dựng map
   trong bộ nhớ rồi round-trip (chiều mà một tool thật sự đi).
2. **`test_shell_golden` bấm `Tab` để chuyển section** — phím shell chưa bao giờ gán
   cho việc đó. Nó render section 0 **năm lần**, ghi ra năm file PPM giống hệt nhau,
   và vẫn xanh mọi assertion. Nay dùng đúng chord và **fingerprint vùng nội dung**.

**⚠️ Chưa xác minh:**
- **Vẫn chưa mở cửa sổ thật**; chuột là `InputState` tổng hợp, nên *cảm giác* kéo chưa
  được kiểm.
- **Chưa hấp thụ Map Lab** (`--maplab` vẫn ghi `fpsmap1`) và **chưa hấp thụ Sandbox**
  — nên vẫn chỉ có **một** workspace, và **cố ý chưa có interface `Workspace`**.
- Chưa render tileset (tile id vẽ bằng bảng 10 màu cố định); chưa sửa entity/trigger;
  zoom chưa neo theo con trỏ; `set_cursor` vẫn chưa có consumer.

### S5 — Farm vertical slice ✅ 2026-09-04 · chương 113

Merge `feat/s5-farm`. Game thứ hai, và là game **đầu tiên vào bằng manifest**:
`--project projects/farm.gameproject` → `entry farm` → `launch_entry` → scene. Không
thêm flag CLI nào.

- **`farm_core`** (không renderer): đồng hồ 06:00→02:00, năng lượng, cuốc/tưới/trồng/
  thu, tăng trưởng cây, lịch NPC (giải qua **entity của `map2`**, nên đổi chỗ cửa hàng
  là sửa map chứ không sửa file lịch), dialogue dạng data, save có version.
- **`save_core`** (`engine/document/save`) generic: version + chuỗi migration. Save của
  bản **mới hơn bị từ chối**; **thiếu bước** trong chuỗi cũng bị từ chối.
- **`FarmScene`** không SDL → chạy được trong test không cửa sổ.
- Data ở `assets/farm/*.def` — cân bằng số liệu **không cần build lại**.

**✅ Đã chạy:** `ctest` **69/69**; **ASan/UBSan sạch**; render game ra ngoài màn hình ở
640×360×ss2 (ngày / vừa trồng / đêm) rồi **soi ảnh**; `--project-inspect`,
`--project-package`, `--hub` chạy trên `farm.gameproject` **không sửa gì**; web build
xanh; `--bench-ui` Release ss=2 **1.4–1.5 ms** (không đổi).

**Hai bug thật do consumer lộ ra:**
1. **Tăng trưởng cây**: "chín" từng suy ra từ chỉ số stage đã làm tròn → cây có số
   stage không chia hết số ngày **thu hoạch được sớm một ngày**. Parsnip (4/5, chia hết)
   không thể lộ ra. Nay **chín** (theo lịch) và **hình dạng** là hai quyết định tách rời,
   và test có cây 5 ngày / 3 stage.
2. **`Camera2D::set_viewport` không clamp lại** → thế giới nhỏ hơn cửa sổ không được
   căn giữa ở frame đầu; và resize cửa sổ sẽ đẩy view ra ngoài thế giới. `set_bounds`
   cũng vậy. Đây là **consumer đầu tiên** của `Camera2D` kể từ chương 110.

**⚠️ Chưa xác minh:**
- **Chưa từng chơi trong cửa sổ thật** — nhịp bước, cảm giác camera khi đi, và "12 phút
  thật một ngày có đúng không" đều là câu hỏi **cảm giác**, test không trả lời được.
- **Chưa có art**: tile là màu phẳng, nhân vật là hình tròn. Tileset CC0 và **tên game
  thật** vẫn là quyết định đang chờ.
- Map do script sinh ra, chưa ai ngồi vẽ; **một map duy nhất**, chưa dùng trigger nên
  chưa có nhà trong / thị trấn.
- Chưa có shop / mùa / thời tiết / friendship / câu cá (v1 — S8); cloud save chưa nối.

---

## Quyết định kiến trúc đã chốt (đừng đảo lại mà không có lý do mới)

| # | Quyết định | Vì sao |
|---|---|---|
| D1 | Giữ nguyên tên token trong `theme.hpp`, chỉ **thêm** cái thiếu | Đổi tên chạm 9 scene mà không thêm giá trị |
| D2 | Golden test dùng **invariant**, không byte-diff | `test_ui_golden.cpp:9-12` — AA làm tròn khác nhau giữa compiler/arch, hash không portable |
| D3 | Không làm atlas 1024×1024 / shelf packer | CPU blit, không có texture GPU để pack |
| D4 | S2 hoãn **riêng overlay**, không biến mọi widget thành `DrawCmd` | Bản nhỏ nhất giải đúng vấn đề popup bị vẽ đè |
| D5 | Pixel editor dùng `.hrt`, không PNG | Repo không có encoder *lẫn* decoder PNG; `.hrt` đã có codec + test + consumer |
| D6 | Bộ widget S2 rút gọn; thêm khi một workspace thật cần | Xây 20 widget trước là đúng cái bẫy §10b của `docs/strategy/02` |
| D7 | Sửa bug ở **chỗ chung**, không vá call site | `→` hỏng ở 2 nơi; mọi chuỗi non-ASCII tương lai cũng sẽ hỏng |
| D8 | `ui::Input` mang **ý định**, không mang phím | Một nơi duy nhất biết Cmd vs Ctrl; và widget test được không cần SDL |
| D9 | `push_clip` **giao**, không thay | Con trong không thể vẽ ra ngoài cha — điều kiện để lồng nhau đúng |
| D10 | Dịch vụ platform được **tiêm vào scene**, không gọi thẳng | Scene gọi `platform::clipboard_*` là không link được nếu thiếu SDL → giết `test_shell_golden` |
| D11 | Hộp thoại huỷ diệt mở với **Cancel** đang focus | Enter đầu tiên sau khi dialog bật lên thường là phản xạ còn sót |
| D12 | Mọi thao tác ghi audit log phải **nhập lý do** | Biến log từ danh sách timestamp thành lời giải thích |
| D13 | Định dạng mới **từ chối** file version cao hơn | Đọc nửa vời = âm thầm mất trường rồi ghi mất mát xuống đĩa |
| D14 | Bảng dữ liệu suy ra được thì **sinh bằng code**, đừng chép tay | `autotile_count()==47` là kết quả nên sai quy tắc là test kêu ngay |
| D15 | Core mới phải có **consumer thật** ngay trong slice | `tilemap_core` không ai load = đúng bẫy "motion without connection" |
| D16 | Flag CLI cũ là **alias** lên registry, không phải đường thứ hai | Hai call site "đang khớp" là cách GUI và CLI trôi xa nhau |
| D17 | Mọi tham số lệnh mutating phải **không rỗng** | Dòng audit không lý do trông giống bằng chứng mà chẳng trả lời gì |
| D18 | `dirty` là **vị trí trong lịch sử**, không phải cờ | Cảnh báo về thay đổi đã undo hết = dạy người dùng bỏ qua cảnh báo |
| D19 | Recovery **đề nghị**, không tự áp dụng | Tự áp dụng = mất đúng bản người dùng cố ý lưu |
| D20 | Nét vẽ tích luỹ **ngoài** stack rồi đẩy vào một lần | `merge_key` giữ "revert đầu + apply cuối" — sai cho gesture tích luỹ |
| D21 | Command ghi **giá trị tuyệt đối**, không phải delta | apply thành idempotent, nên `push_apply` gọi được trên gesture đã xảy ra |
| D22 | Từ chối recovery phải **an toàn** (Cancel giữ nguyên autosave) | Hành động huỷ diệt không được nấp sau nút người ta bấm theo phản xạ |
| D23 | Chưa tạo interface `Workspace` khi mới có một implementation | Một cái khuôn với một người ở; đợi cái thứ hai thì khuôn sẽ đúng hơn |
| D24 | Scene đăng ký lệnh gắn với `this` **phải** gỡ trong destructor | Handler sống lâu hơn object = cú gọi vào bộ nhớ đã giải phóng |
| D25 | Palette **không** thu tham số | Ba giá trị cần validate thuộc về dialog của Hub, không phải một ô một dòng |
| D26 | Test round-trip phải seed **từ struct**, không chỉ từ file | Seed từ file chỉ kiểm parser; tool thì đi chiều ngược lại |
| D27 | Balance data (crop/item) là **file text**, không phải literal C++ | Cân bằng số liệu là công việc lặp lại; nó không nên cần build |
| D28 | Số sai trong file def là **lỗi**, không phải 0 âm thầm | `days=four` → cây không bao giờ lớn mà không có dòng nào giải thích |
| D29 | **Chín** và **hình dạng** của cây là hai quyết định tách rời | Suy cái này từ cái kia biến một lựa chọn làm tròn thành bug cân bằng |
| D30 | Save của bản **mới hơn** bị từ chối, và **thiếu bước** migration cũng vậy | Đọc rồi ghi lại = mất dữ liệu đội lốt tương thích |
| D31 | Runner giữ việc **tăng version**, migration chỉ mô tả đổi dữ liệu | Migration quên bump = vòng lặp vô hạn |
| D32 | `set_viewport`/`set_bounds` **clamp lại** | Bounds là quan hệ giữa thế giới và **view**; đổi view thì vị trí cũ có thể không còn hợp lệ |
| D33 | Fixture của test phải **phá vỡ sự trùng hợp tiện lợi** | 4 và 5-1 tình cờ bằng nhau, và chính sự trùng hợp đó đang làm việc thay cho code |
| D34 | Một `inspect()` duy nhất, trả **dữ liệu** chứ không phải dòng đã in | Bốn bản sao đã trôi xa nhau *trước khi* ai đó nhận ra: publish dừng ở lỗi đầu tiên, ba bản kia liệt kê hết |
| D35 | Asset **thiếu vẫn nằm đúng chỗ** trong danh sách, có badge | Browser âm thầm bỏ cái nó không tìm thấy = đúng cái browser không dùng được để tìm ra chỗ hỏng |
| D36 | Project **không shippable ⇒ không có package hash** | Release id suy ra được từ nội dung thiếu là release id publish được |
| D37 | `known_entries` phải được **tiêm vào**, không tự dựng trong scene | Studio nói "unknown entry farm" trong khi CLI nói OK — hai câu trả lời, một sự thật |
| D38 | Log đọc **mới nhất trước**, file vẫn append-only cũ-trước | Câu hỏi mà lịch sử release trả lời là "vừa xảy ra chuyện gì"; để nó ở dòng cuối = log không được đọc |
| D39 | Scene nhúng được cấp **framebuffer riêng**, không vẽ vào framebuffer cha dưới clip | Scene hỏi `g.width()/height()` rồi canh giữa theo đó — dùng clip thì **mọi toạ độ thành lời nói dối** |
| D40 | Accumulator fixed-step **tách ra dùng chung**, không chép | Bản sao thứ hai khớp mọi frame bình thường và lệch **đúng lúc máy khựng** |
| D41 | Input **chặn cửa**, không chuyển tiếp: chord và Escape không bao giờ tới game | Game ăn `Cmd+K` = palette không với tới; game nuốt Escape = nhốt bàn phím |
| D42 | "Không có input" phải nói **không có con trỏ** (-1), không phải `InputState{}` | Chuột mặc định ở (0,0) là **một vị trí có thật** trong không gian game |
| D43 | Bảng entry là **một**; `known_entries()` suy ra từ nó | "kept in sync with" là comment nhờ con người làm build step |
| D44 | `status()`/`hint()` thuộc về **workspace**, không phải shell | Shell ghép "tile 4, 9" nghĩa là shell biết tài liệu có tile — điều một scene làm cho bất khả thi |
| D45 | Recovery trong interface là **có mặc định**, không thuần ảo | Bắt workspace không autosave viết ba hàm rỗng = interface bắt đầu nói dối |
| D46 | Undo của scene bằng **snapshot toàn cảnh**, một bước cho cả gesture | `to_scene/from_scene` đã round-trip; không có nghịch đảo nào để viết sai |
| D47 | `commit()` **giữ lại selection** qua chính cú apply của nó | apply cài lại đúng thế giới đang có; xoá selection ở đó là artefact, không phải ý định |
| D48 | Hấp thụ editor cũ, **không viết cái thứ hai** | Hai implementation của một ý tưởng chỉ đồng ý vào ngày chúng được viết |
| D49 | Remote config + live event dùng **đúng định dạng defs của file** | Người vận hành gõ đúng dòng họ sẽ gõ trong file; không parser mới, không schema thứ hai |
| D50 | Override **gán theo trường**, không thay cả bản ghi (`merge_defs`) | `crop parsnip sell=70` từ dashboard sẽ reset days/stages/seed về mặc định struct |
| D51 | Một dòng override là **nguyên tử**; lỗi ⇒ không commit gì | Bản ghi sửa nửa vời = cân bằng không ai chọn và không ai thấy |
| D52 | Field lạ: **file bỏ qua**, dashboard **báo lỗi** | File cần tương thích tiến; ô dashboard vừa được người ta gõ 30 giây trước |
| D53 | Override **không được** làm game không chơi được (`days<1`, `stages<2`) | Remote config là cần gạt không cần build — cũng là input duy nhất hạ được game đang chạy |
| D54 | Quyết định sync là **hàm thuần**, chốt bằng test trước khi gửi byte nào | Thao tác đầu tiên có thể **huỷ việc bằng cách thành công** |
| D55 | So **nội dung** trước, so version sau | Hai save trùng byte *là* cùng một ván; đẩy lại chỉ vì máy khác lưu sau là rủi ro không đổi lấy gì |
| D56 | Cần **bookmark** (version+hash đã đồng ý), không chỉ hai hash | Hai hash khác nhau không nói được ai đã đổi |
| D57 | 404 là ô trống; **mọi lỗi khác thì không** | "Không biết" mà thành upload là mất dữ liệu |
| D58 | Bản mây **đọc không được thì để nguyên** | Build mới hơn có thể đọc tốt; ghi đè biến việc không tương thích tạm thời thành xoá vĩnh viễn |
| D59 | Hai bên cùng đổi ⇒ **hỏi**, và chip phải **nêu tên phím** | "Hai save khác nhau" không nói thì người chơi đứng nhìn nông trại không lưu được |
| D60 | Các lớp config **nối chuỗi**, không bắn song song | Hai request cùng bay = cái về sau thắng; fake transport trả lời **ngược thứ tự** để test được điều đó |
| D61 | Save trong lúc sync đầu **không upload** | Verdict sắp về đọc chính file đó; upload ở đây = gửi hai lần và quyết định từ ảnh chụp cũ hơn |
| D62 | Guest cần **`device_id`** để quay lại được | Không có nó, save đẩy lên vào một tài khoản không lần nào sau đọc được |
| D63 | Không seed dữ liệu mà **không ai đọc** (leaderboard farm) | Một dòng seed vô dụng là thứ người sau tưởng là tính năng |
| D64 | "Build xanh" **không phải** là "chạy được" | 17 chương báo "web build xanh"; nó chỉ link được, và trang chưa từng chạy |
| D65 | Không có renderer tăng tốc thì **fallback phần mềm**, không chết | Ta tự vẽ mọi pixel; renderer chỉ dán một quad |
| D66 | `resizable` được **dịch ở platform seam**, không phải ở game | Game xin "vừa màn hình"; nghĩa của nó khác nhau theo nền tảng |
| D67 | Bundle web **không được chứa** thư mục gitignore (state cục bộ) | `saves/device.id` bị phát đi = hai người lạ chung một tài khoản |
| D68 | Chỉ `saves/` là **bền** trên web (IDBFS), phần còn lại là nội dung | Nội dung đi kèm `demo.data`; thứ thuộc về người chơi thì không |
| D69 | Flush IDBFS **tuần tự + gộp**, không phải mỗi lần ghi | syncfs chồng nhau đan vào nhau → file về 0 byte |
| D70 | Chứng minh persistence phải **cắt mạng**, không chỉ tải lại | Cloud save âm thầm đóng thế cho filesystem |
| D71 | Con trỏ vào viewport ánh xạ qua **rect vừa vẽ**, không qua scale lưu riêng | Panel nhỏ hơn khung game thì blit là *fit*, không phải bội số nguyên |
| D72 | Chấp nhận con trỏ **trễ một frame**, không tính layout hai lần | Bản sao thứ hai của layout là bug chờ một lần resize |
| D73 | Press **bắt đầu bên trong** thì giữ con trỏ tới khi nhả | Không thì game không bao giờ nghe thấy nhả, và giữ nút mãi |
| D74 | "Bấm không ăn" phải **hỏi DOM và app**, đừng suy từ ảnh | Ảnh chụp canvas bị CSS scale — toạ độ trong ảnh **không phải** toạ độ game |
| D75 | Mutation **sống sót thì ghi lại**, không giấu | Một hàng "8/8 giết" sai còn tệ hơn "7/8, và đây là lý do" |
| D76 | **Một cửa cho mỗi loại**: game = manifest, Studio = `--shell`, còn lại = `--lab` | 12 flag là 12 bản sao của cùng một `platform::Config` |
| D77 | `entries()` và `labs()` là **hai bảng**, dù cùng struct | Một bảng chung khiến `entry fx` trong manifest thành hợp lệ |
| D78 | Flag lạ là **lỗi kèm danh sách**, không rơi xuống demo mặc định | Người hay gõ flag vừa xoá nhất chính là người dùng nó hôm qua |
| D79 | Chỉ xoá cái mà **cửa khác tới được cùng căn phòng** | Xoá `--maplab` là xoá khả năng sửa entity; xoá lab là bỏ consumer runtime của 7 core |
| D80 | Ledger có ngày tháng thì **thêm tên mới trong ngoặc**, không viết lại | Một phán quyết ghi ngày 2026-09-04 phải đọc như điều đúng vào ngày đó |
| D81 | Art vào bằng **một cửa**, và về **một định dạng** (`.hrt`) | Hai nguồn giữ hai định dạng = asset cache/closure/package hash phải biết cả hai, mãi mãi |
| D82 | PNG **chỉ decode**, và **chỉ offline** | Không gì ghi PNG; compressor là bài toán lớn hơn nhiều mà chưa ai cần |
| D83 | Định dạng lạ thì **từ chối kèm tên**, đừng đoán | "interlaced không hỗ trợ" hơn hẳn một ảnh sai một cách tinh vi |
| D84 | Test decompressor phải dùng **stream của compressor thật**, đủ cả ba loại khối | Decoder chỉ thấy một loại chạy được tới khi gặp file người khác |
| D85 | Map giữ **id ngữ nghĩa**; theme mới là chỗ nối tới art | Lưu chỉ số sheet = phải đánh số lại cả level mỗi lần đổi art |
| D86 | Id **không có dòng theme thì không có art**, rơi về màu phẳng | Cho phép pack phủ một phần map thay vì tất-cả-hoặc-không |
| D87 | **Guard thừa là chỗ mutation nấp** — một điều kiện, nhiều lý do | Hai guard che nhau: xoá cái nào test cũng xanh |
| D88 | Art nhập vào phải có dòng trong `ATTRIBUTION.md` **cùng lúc** | Pack rơi vào `assets/` không ai thấy = nghĩa vụ license không ai thấy |

---

## S6 — Studio Project workspace + audit log (chương 114) — XONG

Commit: `007d067` (inspect core) · `f8f5d2f` (Project section) · `d900ab2` (audit log).

**Cái gì đã chạy, không chỉ đã viết:**

- `engine::inspect()` thay **bốn** bản sao của "đọc manifest → validate → hash asset"
  (CLI launch, CLI inspect, publish, hub) + bản thứ năm trong Studio (`map_asset_of`).
  Bốn bản đó **đã trôi xa nhau**: publish dừng ở asset thiếu **đầu tiên**. Đã xác minh
  bằng CLI, không chỉ unit test — manifest có ba đường dẫn hỏng nay cho **cùng ba
  dòng** từ cả publish lẫn inspect.
- `project.inspect` vào registry; `--project-inspect` thành **alias**, output
  byte-identical với trước khi refactor.
- Studio có section **Project**: asset browser (type · path · content hash · size ·
  present/MISSING) + verdict + package hash + Copy/Re-inspect. Sáu section, `Cmd+1..6`.
- Hub (cả `--hub-ui` lẫn Studio) vẽ **audit log**, mới nhất trước, UTC, lý do là cột
  rộng nhất.

**Bug thật tìm được (đều do xây consumer, lần thứ năm và sáu):**

1. **Publish báo một lỗi mỗi lần chạy.** Bốn bản sao đã bất đồng từ lâu; không ai chọn
   điều đó, nó chỉ là chuyện xảy ra với bốn bản sao của một ý tưởng.
2. **Studio và CLI bất đồng về chính project farm.** Scene giữ `known_entries={"fps"}`
   còn `main.cpp` biết `{"fps","farm"}` → `--shell projects/farm.gameproject` báo
   *"unknown entry scene: farm"* trên project mà `--project-inspect` gọi là OK.

**Đã mutation-test:** dừng ở lỗi đầu tiên · bỏ asset thiếu khỏi danh sách · hash
project thiếu nội dung · đảo thứ tự problem · đảo chiều log · xoá hẳn khối log.

**Số liệu:** 70/70 test · ASan+UBSan sạch (70/70) · web build xanh · Release ss=2
**1.10 ms** median / 1.34 ms p95 (budget 8 ms) · golden path + second-game smoke chạy
lại xanh · không rò `.tmp`.

**Chưa xác minh (nói thẳng):** chưa ai **bấm** vào bất kỳ thứ gì — chỉ render ngoài
màn hình. Asset browser chưa từng chứa danh sách dài (5 asset, chưa cuộn thật). Không
có gì theo dõi filesystem: sửa asset ở tool khác thì panel cũ cho tới khi bấm `R`.
Log chưa có filter/paging. **Play viewport chưa làm** — nó cần `App` giữ được
sub-scene, mà `app.hpp` chỉ có một `unique_ptr<Scene>` set trong constructor, không có
setter. Đó là thay đổi kiến trúc, không phải một panel, và xứng đáng một slice riêng.

---

## S6b — Play viewport (chương 115) — XONG

Commit: `a0014f1` (FixedStep) · `571ab4e` (Play viewport + bảng entry).

**Cái gì đã chạy:**

- `--shell projects/farm.gameproject` → section **Play** → Farm chạy **trong Studio**
  ở đúng 640×360 gốc, letterbox scale nguyên, kèm **Pause** và **Step một frame**.
- `engine::FixedStep` tách khỏi `App::frame`; Play viewport dùng **cùng** clamp.
- `launch_entry` + `kKnownEntries` gộp thành **một bảng** `entries()`; Play factory là
  người đọc thứ ba, và nó đọc cùng bảng chứ không tạo bản sao thứ tư.

**Bug thật tìm được:** "không nhận input" từng là `InputState{}` mặc định, chuột ở
**(0,0)** — một vị trí có thật. Game không focus đang bị bảo con trỏ đậu ở góc trên
trái vĩnh viễn. Chỉ có test viết **từ góc nhìn của scene** mới thấy; nhìn màn hình
không bao giờ thấy.

**Đính chính chương 114:** chương đó viết Play viewport "cần `App` giữ sub-scene". Sai.
Studio tự giữ `unique_ptr<Scene>` được. Rào cản thật là `App::frame` bị hàn vào
`platform::framebuffer()`/`input()`, và thứ đáng dùng chung là **accumulator**. Đã ghi
đính chính vào chính chương 114.

**Đã mutation-test:** chuyển input khi không focus · cho chord lọt · Step thành Resume ·
`stop()` không reset đồng hồ · scale phân số · paused vẫn chạy · (FixedStep) clamp sai
chỗ, `>` thay `>=`, reset giữ phần dư, gán thay vì cộng accumulator.

**Số liệu:** 71/71 test · ASan+UBSan sạch (71/71) · web build xanh · Release ss=2
**1.56 ms** median (đây là **ss=1** — nhãn "ss=2" ở dòng này sai, xem phần sửa ở S8;
**chưa có game chạy** trong số đo này) · golden path +
second-game smoke xanh · không rò `.tmp`.

**Chưa xác minh:** vẫn **chưa ai bấm Play** — chỉ render ngoài màn hình. **Chuột chưa
tới game** (cố ý: con trỏ đúng một nửa còn tệ hơn không có). Chỉ `farm` đã chạy trong
viewport; `fps` có trong bảng nhưng chưa thử. Viewport **vẫn chạy khi sang section
khác** — cố ý, nhưng một scene đắt sẽ ăn frame time ở Map workspace mà không cảnh báo.

---

## S7 — Hai workspace, và cái interface được nó tạo hình (chương 116) — XONG

Commit: `7ff216c` (interface) · `11d7371` (Scene workspace + hấp thụ sandbox).

**Cái gì đã chạy:**

- `--shell` → section **Edit** có **tab**: `Map | Scene`. Tab bẩn hiện dấu `*` —
  status strip chỉ nói về workspace đang xem, tab là chỗ duy nhất cái kia báo được.
- `--sandbox` là `WorkspaceHost` mỏng bọc **chính đối tượng** mà tab Scene giữ.
  `sandbox_scene.{hpp,cpp}` **đã xoá** (315 dòng).
- Scene workspace có **undo** (snapshot toàn cảnh), **autosave + recovery**, và
  `scene.save/undo/redo/reload/play` trong palette — trước đây sandbox **không có gì**
  trong số đó.
- `creator.gameproject` khai báo `asset scene scenes/demo.scene`, nên workspace mở cái
  manifest nói, không phải đường dẫn cứng.

**Bug thật tìm được (do test, không phải do nhìn màn hình):**

1. **Mọi thao tác sửa đều xoá lựa chọn.** `push_apply` **apply**, apply cài lại thế
   giới, cài lại dựng entity mới → xoá selection. Kéo slider một cái là mất chọn đúng
   actor đang sửa. Nay `commit()` giữ selection qua chính cú apply của nó; undo/redo
   thật vẫn xoá — đúng, vì cái đó dựng một thế giới **khác**.
2. **Delete trễ một frame**: phím set cờ mà handler ở trên đã đi qua rồi.
3. **Pixel giữa một actor là cái gạch hướng**, vẽ bằng màu nền có chủ ý. Probe ở đó
   là đo cái gạch, không đo actor. Lần thứ ba trong ba chương một probe toạ độ đơn đo
   nhầm thứ — **đếm** mới là phát biểu đúng của gần như mọi khẳng định thị giác.

**Đã mutation-test 8 lần:** commit mỗi frame kéo · giữ index selection cũ qua restore ·
ghi cả edit rỗng · Stop tính là edit · commit xoá selection · recovery tự áp dụng ·
từ chối recovery mà xoá autosave · recovery không undo được.

**Số liệu:** 71/71 test · ASan+UBSan sạch (71/71) · web build xanh · Release ss=2
**1.13 ms** median (đây là **ss=1** — nhãn "ss=2" sai, xem phần sửa ở S8) · golden path xanh (release id đổi vì manifest thêm
asset — đúng như thiết kế).

**Chưa xác minh:** chưa ai **bấm**. Scene canvas **không có pan/zoom**, không
multi-select, không copy/paste, không grid/snap. **Spawner/OnOverlap** round-trip được
nhưng **không có inspector** — sửa interval của Emitter phải sửa tay file `.scene`.
Texture vẫn dò theo tên cố định. **`--maplab` vẫn còn và vẫn ghi `fpsmap1`** —
`WorkspaceHost` giờ là cơ chế để nghỉ hưu nó, nhưng đó là quyết định về bề mặt CLI.

---

## S8 — Farm nối vào backend: giá từ dashboard, save biết cãi (chương 117) — XONG

Commit: `aa174df` (hai quyết định thuần) · `f794567` (scene + SDK + BaaS).

**Cái gì đã chạy:**

- **Một định dạng, ba nguồn.** Remote config và live event gửi đúng text mà
  `assets/farm/crops.def` dùng: `crop parsnip sell=70`. Layer: file → remote config →
  live event, **nối chuỗi** chứ không bắn song song.
- **`apply_overrides` ≠ `merge_defs`.** Chỉ gán trường được nêu tên; một dòng lỗi
  không commit gì; dòng làm cây không trồng được bị từ chối.
- **Cloud save đối chiếu**: 404 là ô trống, 500 thì không; bản đọc không được thì để
  nguyên; hai bên cùng đổi thì **hỏi** (F6/F7).
- **Farm biết tiếp tục ván cũ** — trước đây chỉ có F9, phím không ai bấm.
- **HUD**: hotbar 4 ô, chip cloud, dải cảnh báo lỗi remote config (có nền riêng).
- **`test_farm_live`**: dựng Drogon thật, đổi giá qua `PUT /v1/admin/config/farm_defs`,
  và game đang chạy tính đúng giá mới.

**Bug thật do test end-to-end tìm ra:**

1. **Guest là tài khoản mới mỗi lần chạy** → save đẩy lên được nhưng không kéo về được.
   Upload chạy, download chạy, **tính năng không hoạt động**. Migration 7+8 thêm
   `device_id`. Colony dính lỗi này từ chương 57 mà không ai thấy vì colony không bao
   giờ đọc save về rồi so.
2. **Bấm F5 khi sync đầu còn đang bay** → gửi hai lần, và quyết định sync tính từ ảnh
   chụp mây cũ hơn cú upload. Bản gương của nó (Pull từ ảnh cũ đè lên save vừa lưu)
   mới là bản mất dữ liệu.

**Lỗi cùng họ, lần thứ tư:** `sell` được viết ở **cả** `crops.def` và `items.def`, và
`end_day` đọc bản của item — nên giá cây là con số **duy nhất một đợt cân bằng không
đổi được**. Hai file trùng số thì đồng ý vào ngày viết, rồi một bên lặng lẽ hết được đọc.

**Đã mutation-test 20 lần, giết hết.**

**Bài học về chất lượng test:** (1) ảnh chụp là ảnh của scene mình truyền vào — lambda
`render` đóng gói scene đầu tiên, nên mọi ảnh chụp phía dưới (của scene KHÁC) đều là
ảnh của scene đó, và ảnh "conflict" là một nông trại không có conflict. (2) **đếm, đừng
dò** — lần thứ tư liên tiếp.

**Số liệu:** 72/72 test · ASan+UBSan sạch (72/72) · web build xanh · golden path (cả
`creator` lẫn `farm`) xanh · không rò `.tmp`.

**Sửa lại một con số đã ghi sai:** mọi lần trước đều chép "Release ss=2 1.1–1.6 ms" —
đó là cột **ss=1**. Đo lại, và dựng luôn commit trước (`eaa54b4`) ở Release để đối
chứng: **ss=1 1.0–1.8 ms · ss=2 6–10 ms**, và ss=2 **vượt ngân sách 8 ms ở 2/3 lần
chạy**. Không phải slice này gây ra — con số đã bị dán nhãn sai từ lần đo đầu tiên.
Phát biểu đúng là: ở 1280×720 Studio thoải mái, **bật supersample thì nó chạm hoặc
vượt ngân sách**.

**Chưa xác minh:** `test_farm_live` **không chạy trong CI** (CI chỉ cài SDL2, mọi test
gác sau Drogon đều biến mất ở đó). `device_id` là theo **máy**, không theo người.
Remote config chỉ lấy một lần lúc khởi động. Analytics bắn-rồi-quên. Vẫn **chưa ai bấm**.

---

## S9 — Cửa sổ, mở ra thật (chương 118) — XONG

Commit: `c835b1e` (platform) · `f4711f4` (bundle + CI) · `e9a4cea` (persistence + trang).

**Điều quan trọng nhất:** suốt 17 chương, "web build xanh" **chỉ có nghĩa là link được**.
Lần đầu mở trang trong trình duyệt: **không chạy**.

**Năm lỗi thật, không cái nào compile bắt được:**

1. `SDL_CreateRenderer` thất bại khi không có WebGL → **chết hẳn**. Nay có fallback
   phần mềm (mọi pixel vốn đã do ta vẽ vào framebuffer CPU; renderer chỉ dán một quad).
2. `resizable` là khái niệm desktop → trên web SDL lấy cỡ CSS → Studio ra canvas **3×3**.
3. **Bundle publish kèm `saves/`, `releases/`, `channels/`** — ba thư mục đã gitignore
   vì chúng là state cục bộ. Nghĩa là `saves/device.id` được phát cho mọi trình duyệt:
   **hai người lạ dùng chung một tài khoản**. Kèm cả `releases/audit.log`.
4. **Web không có trí nhớ** (MEMFS chết theo tab). Save chỉ *trông như* còn vì bản mây
   gánh thay.
5. **`FS.syncfs` chồng nhau mất dữ liệu** → `slot1.sav` quay lại **0 byte**.

**Cách chứng minh:** chặn mọi `/v1/` rồi tải lại. Farm vẫn tiếp tục đúng ngày đã lưu,
chip ghi "offline" → thế giới đó **lấy từ đĩa**. Không có phép thử này thì cloud save
vẫn đang đóng thế cho filesystem mà không ai biết.

**Một giả thuyết sai, ghi lại vì nó đáng:** ban đầu tôi kết luận `std::random_device`
trên Emscripten là tất định (chuẩn C++ **cho phép** vậy) và đã viết bước trộn entropy.
Nguyên nhân thật nằm trên đĩa: `cat assets/saves/device.id` in ra đúng chuỗi đó. Bước
trộn giữ lại — 6 dòng, và giá trị này **không được phép lặp**.

**Đồ nghề:** không có tool duyệt web nào chạy được (extension chưa nối, devtools MCP
không attach được). Thứ chạy được lại nhỏ hơn cả hai: Chrome `--remote-debugging-port`
+ ~60 dòng Node dùng `WebSocket` **có sẵn** (Node 22) nói CDP thẳng. `Network.setCacheDisabled`
quan trọng hơn vẻ ngoài: hai vòng "sửa mà không thấy gì đổi" là do trình duyệt phục vụ
`demo.js` cũ trong cache.

**Đã chạy được trong trình duyệt:** cờ vua · farm (kèm BaaS thật) · Studio shell.

**Số liệu:** 72/72 test · web bundle 47 → 36 entry (0 file state cục bộ) · CI có job web
mới (link + grep manifest).

**Chưa xác minh:** **chỉ render phần mềm** (`--disable-gpu` suốt) — WebGL và bước
downsample tuyến tính của supersample chưa hề chạy. **Chưa click chuột** — chỉ gửi phím.
Một trình duyệt duy nhất (Chrome 152 headless, macOS). Job web trong CI **chưa từng
chạy**. Danh sách `--exclude-file` phải bảo trì tay. **Native ↔ web cloud save vẫn chưa
chứng minh được** — hai máy là hai guest theo thiết kế, muốn chung phải có tài khoản thật
và chưa có UI cho việc đó.

---

## S10 — Con trỏ, và thứ đầu tiên cần đến nó (chương 119) — XONG

**Lần đầu có người bấm.** Và điều đáng ghi nhất: chuột **đã hoạt động sẵn** — cái sai là
**toạ độ trong bài test của tôi**, đọc ra từ ảnh chụp trong khi canvas đang bị CSS thu
nhỏ 0.82×, nên điểm bấm rơi ra ngoài nav rail 8 px. "Bấm không ăn" trông y hệt một lỗi
input thật; cách phân biệt rẻ nhất là **hỏi DOM và hỏi app xem mỗi bên thấy gì**, đừng
suy luận từ bức ảnh.

**Đã bấm thật, trong trình duyệt:** Project (mở asset browser) · Play → Play (FPS
raycaster chạy trong Studio) · **vẽ một ô tile** (tab → `Map *`, status `tile 7, 7`,
Undo sáng, hint `undo: paint`) — cả editor trong một cú bấm.

**Cái được xây: chuột vào Play viewport.** Chương 115 cố ý không đưa con trỏ vào game vì
**không kiểm chứng được** phép biến đổi. Giờ kiểm được:

- ánh xạ qua **rect mà `draw()` vừa blit vào**, không qua hệ số scale lưu riêng;
- **trễ một frame** có chủ ý — cách khác là tính layout hai lần rồi giữ hai bản khớp;
- bấm ngoài tranh là của Studio, nhưng **press bắt đầu bên trong thì giữ con trỏ** tới
  khi nhả (không thì mọi cú kéo để lại một nút game tưởng vẫn đang giữ).

**Con trỏ có người tiêu thụ** (D15): farm rê lên ô **kề bên** thì quay mặt, bấm thì dùng
công cụ. Kiểm chứng cuối: bấm một ô **bên trong farm đang chạy trong Play viewport, trong
trình duyệt** → `tilled the soil`, đúng ô. Sáu phép biến đổi, một cú bấm.

**8 mutation, giết 7.** Cái sống sót **ghi lại chứ không giấu**: bỏ guard `mouse_x >= 0`
trong farm không làm test đỏ, vì cả nông trại vừa màn hình nên camera căn giữa, và
(-1,-1) rơi ra ngoài bốn ô kề. Guard vẫn giữ (đó là hợp đồng của platform, và đọc -1
thành vị trí chính là bug chương 115), nhưng assertion cạnh nó là **kiểm hợp đồng, không
phải bằng chứng**.

**Số liệu:** 72/72 test · web build xanh.

**Chưa xác minh:** không wheel, không chuột phải, không kéo-thả trong trình duyệt ·
**không touch gì cả** · chỉ render phần mềm, một trình duyệt · raycaster và colony vẫn
không đọc chuột nên phép biến đổi của viewport chỉ có **một** nhân chứng.

---

## S11 — Một cửa cho mỗi loại (chương 120) — XONG

**Ba quyết định của anh đã được chốt và ghi vào tài liệu** (xem mục dưới). Slice này là
quyết định số 3.

`main.cpp` có **31 flag**; 12 trong số đó là 12 bản sao của cùng tám dòng
`platform::Config`. Rút xuống **ba loại cửa**: game (`--project <manifest>`), Studio
(`--shell`), lab (`--lab [id]`).

- **Xoá hẳn `--hub-ui`** + `hub_scene.{hpp,cpp}` (2 file chết) — Hub section của Studio
  vẽ **cùng một panel** từ cùng một view model.
- **Gộp 12 flag** thành `--lab`: `scene map texture editor fx light audio anim 3d viz3d
  iso colony`. `--lab` không tham số thì **liệt kê chính bảng đó**.
- **Hai bảng, không phải một**: `entries()` (game — manifest khai báo được) và `labs()`.
  Tách ra để `entry fx` trong manifest **vẫn bất khả**.

**Lỗi thật sửa nhân tiện:** flag lạ trước đây **rơi xuống demo M0** — gõ sai hoặc gõ
flag vừa xoá thì mở nhầm cửa sổ và **không nói gì**. Nay là lỗi `rc=2` kèm danh sách
đầy đủ **và bảng "flag đã nghỉ hưu → đi đâu"**. `demo --help` in cùng nội dung.

**Không xoá cái gì còn là đường duy nhất:** `--lab map` vẫn là nơi **duy nhất** sửa được
entity/spawn; 7 lab kia là **consumer runtime duy nhất** của `particles_core`,
`light_core`, `audio_core`, `tween_core`, `render3d_core`, `viz3d_core`, `studio_core`.
Bề mặt giảm 13 flag, dự án **không mất gì**.

**Trang web thôi là bản sao thứ hai của danh sách lab**: `?mode=lab-<id>` đi thẳng qua
bảng, xác minh trong trình duyệt (`?mode=lab-fx` → particle playground, 481/4000 hạt).

**Tài liệu đã cập nhật kỹ:** `CLAUDE.md` (viết lại mục lệnh), `README.md` (viết lại khối
run; bảng tính năng lịch sử giữ nguyên nhưng tên flag trỏ đúng), `docs/PROJECT-BRIEF.md`
(§Block 2 viết lại; **các dòng ledger giữ nguyên phán quyết có ngày tháng**, chỉ thêm
tên mới trong ngoặc). **`docs/book/` không sửa** — một chương là điều đúng vào lúc nó
được viết; chương 120 là nơi ghi thay đổi.

**Số liệu:** 72/72 test · web build xanh · golden path xanh.

**Chưa xác minh:** không có test nào phủ bề mặt CLI (chỉ chạy tay + smoke script của CI
cho các verb spine). `--gui`/`--tui` vẫn ở top-level (chess có 2 tham số vị trí, và TUI
không phải cửa sổ). `iso`/`colony` là game về mọi mặt trừ giấy tờ — cho chúng manifest
sẽ xoá thêm 2 flag, nhưng `colony` **sinh sprite lúc chạy** nên resource closure sẽ từ
chối; đó là việc thật, không phải đổi tên.

---

## S12 — Định dạng ngoại lai đầu tiên (chương 121) — XONG

Đây là **quyết định số 2** của anh, làm xong: *"hỗ trợ cả 2, để đẹp thì CC0/CC-BY, sau
clone vẽ lại trong studio"*. Câu đó quyết định **kiến trúc**, không chỉ nguồn art: hai
nguồn phải về **cùng một định dạng**, nếu không mọi thứ downstream (asset cache,
resource closure, package hash) phải biết hai loại file mãi mãi.

```
PNG (pack bất kỳ)  ──asset.import (offline)──▶  .hrt  ──▶  engine
Texture Lab        ─────────────────────────▶  .hrt  ──▶  engine
```

- **Tự viết DEFLATE** (`inflate_core`, RFC 1951+1950) vì SDL2 là dependency runtime duy
  nhất. **Chỉ giải nén** — không gì ở đây ghi PNG.
- **`png_core`**: depth 8, colour type 0/2/3/6, tRNS, cả 5 filter, không interlace.
  Mọi thứ khác **từ chối kèm tên lý do**.
- **`--cmd asset.import`** là cánh cửa, và là **offline**: engine không decode PNG lúc
  chạy.
- **`assets/ATTRIBUTION.md`** + commit luôn file PNG gốc cạnh `.hrt`, để import **chạy
  lại kiểm được**, không phải tin suông.
- **Farm có art**: Kenney Tiny Town (CC0). Map giữ **id ngữ nghĩa**; `farm/theme.def`
  nối id ↔ chỉ số tile. **Id không có dòng thì không có art** → rơi về màu phẳng cũ.
  Tiny Town **không có tile nước** nên cái ao vẫn phẳng trong khi quanh nó đã có art —
  đó chính là "hỗ trợ cả hai" **theo từng ô**.
- Bốn call site `register_release_commands` gộp thành một `cmd::register_all`.

**Test:** stream do **zlib thật** tạo, ở nhiều level để **cả ba loại khối** xuất hiện +
4 kiểu hỏng phải bị từ chối. 3 fixture PNG **tự dựng** (biết pixel do xây dựng, cố ý
khó: mỗi hàng một filter, tRNS ngắn hơn palette, greyscale) + 2 file **người khác làm**.

**Hai mutation sống sót lúc đầu — và là lỗi thiết kế, không phải lỗi test:**
`draw_tile` có ba guard, hai trong số đó **che lẫn nhau** (xoá cái nào cũng xanh), cái
thứ ba lặp lại bất biến `parse_theme` đã giữ. Nay còn **một điều kiện, hai lý do**.
Guard thừa không miễn phí: đó là chỗ mutation nấp được.

**Chỉ số tile được ĐỌC từ sheet, không đoán** — lần đầu lệch một ô sang trái, render ra
trông y như đất, rất thuyết phục.

**Số liệu:** 74/74 test · web build xanh · golden path xanh (release id của farm đổi vì
manifest thêm 2 asset) · xác minh trong trình duyệt (bấm chuột cày đúng ô đã có art).

**Chưa xác minh:** file PNG gốc **vẫn nằm trong bundle web** (5 KB, cố ý) · **không có
autotile** nên cỏ giáp đất là cạnh cứng · một sheet cho một map · `map2` đã có trường
tileset riêng nhưng **chưa dùng** · **cái ao vẫn là hình chữ nhật xanh phẳng** — thứ
đáng vẽ đầu tiên trong Texture Lab · **chưa đo** chi phí frame.

---

## Ba quyết định — ĐÃ CHỐT (2026-09-04)

1. **Tên game: Farm / Creatures** — chốt. Không còn là tên tạm; đã nằm trong
   `assets/projects/farm.gameproject`, `entries()` và `launch_entry`.
2. **Art: hỗ trợ CẢ HAI.** Trước mắt dùng bộ pixel-art **license mở (CC0/CC-BY)** cho
   đẹp; sau đó **clone và vẽ lại trong Studio**. Nghĩa là engine phải đọc tileset từ
   **một đường duy nhất**, bất kể nguồn — file pack ngoài hay `.hrt` do Texture Lab
   xuất. Kèm theo là chỗ ghi **attribution/license** (CC-BY bắt buộc ghi công).
   *(Lưu ý vận hành: tôi **không tải** asset từ mạng về mà không hỏi trước — sẽ dựng cơ
   chế + một bộ placeholder tự sinh bằng code của mình, rồi anh thả pack thật vào.)*
3. **Xoá flag CLI cũ** — xong ở chương 120.

Quyết định 2 đã làm xong ở **chương 121**. Kenney Tiny Town (CC0) đã tải về, import và
commit; đường đọc art dùng chung cho cả pack ngoài lẫn `.hrt` vẽ trong Studio.

## S13 — vẽ trong Studio cái mà pack không có (chương 122) — XONG 2026-09-05

Commit: `9bed544` (cửa thứ hai vào `.hrt`) · `dcc4ce6` (farm mặc hai sheet) ·
chương 122 + docs. Merge `--no-ff` vào `main`.

**Đã CHẠY, không chỉ viết:**

- `--cmd asset.texture <src.recipe> <dst.hrt>` — bake recipe của Texture Lab thành
  `.hrt`, offline, headless. Đối xứng với `asset.import`.
- `theme.def` có **sheet đặt tên, nhiều sheet**. `tile ground 1 town 0` và
  `tile ground 3 water 0` — cùng layer, hai file, hai nguồn gốc.
- Cái ao **có art**: `assets/textures/farm_water.{recipe,hrt}`, 16×16, hai sắc xanh.
- `test_farm` **sinh lại tile từ recipe và so từng byte** — provenance kiểm được.
- 74/74 test · 11 mutation, **giết hết** (2 cái phải viết test mới giết được) ·
  web build xanh · golden path xanh · release id → `184db301032118f2`.

**Bài học ghi lại:** một guard **không bao giờ được chạm tới** thì mutation sống sót,
và câu trả lời đúng là **viết test**, không phải xoá guard. Guard "index vượt sheet"
chưa từng chạy vì không file nào trong repo có index sai — sửa bằng một scene dựng
trên **bản sao** cây asset (không phải cây thật: test sửa `theme.def` của chính dự án
là test có thể làm hỏng dự án).

**Chưa xác minh / trần:** không autotile · một tile lặp, không biến thể · nước không
động · Texture Lab **không vẽ được hình có hình dạng** → nửa "clone vẽ lại" mới đúng
cho texture · `.recipe` không nằm trong manifest · chưa đo frame cost.

## S14 — chỗ để vẽ (chương 123) — XONG 2026-09-05

Commit: `badd58e` (paint_core) · `bd783c2` (Pixel workspace) · `bcaabd0` (sửa bàn
phím web) · chương 123 + docs. Merge `--no-ff` vào `main`.

**Chương này bắt đầu bằng việc ĐỊNH LÀM AUTOTILE và phát hiện KHÔNG LÀM ĐƯỢC.**
Tiny Town có 9 mảnh đất-trên-cỏ, không phải 47; 9 mảnh không vẽ được dải rộng 1 ô;
đường đi của farm rộng đúng 1 ô. **Chặn ở ART, không ở code** — và Texture Lab không
sinh được hình có hình dạng. Phải có người cầm con trỏ, mà trong dự án chưa có chỗ.

**Đã CHẠY, không chỉ viết:**

- `paint_core` — sửa pixel dưới dạng `doc::Command`, cùng hình dạng với `map_edit`.
  Thêm `touch_line` (Bresenham) vì ở zoom 8 con trỏ đi vài pixel mỗi frame.
- `PixelWorkspace` — workspace **thứ ba**, `--lab pixel` và tab trong Studio.
  Pencil/Rect/Fill/Pick, undo chung stack, autosave + recovery trên file **nhị phân**,
  palette **lấy từ chính ảnh**.
- 76/76 test · 8 mutation, giết hết · golden path xanh · **đã chạy trong Chrome thật**.

**Bug lớn tìm được nhờ chạy trong trình duyệt:** **web build CHƯA BAO GIỜ có input
bàn phím**, từ chương 118 tới giờ. SDL2 nghe phím trên **canvas**, mà trang không bao
giờ focus nó → mọi keydown vào `body`. Không WASD trong farm, không F5/F9, không
phím tắt Studio. Ba chương "đã xác minh trong trình duyệt" đều **chỉ dùng chuột**.
Sửa: `tabindex="0"` + `focus()`. Đã xác minh lại: `R` đổi tool, `D` làm nhân vật đi.

**Bài học ghi lại:** cạnh phím là **poll-derived** — nhấn-thả trong cùng một frame
16ms là vô hình. Ba lần thử đầu thất bại **giống hệt** cái bug đang truy.

## S15 — chơi được bằng tay (chương 124) — XONG 2026-09-05

Commit: `ba03c64` (sửa renderer) · `cc48216` (điều khiển màn hình) · chương 124 + docs.
Merge `--no-ff` vào `main`.

**Đã CHẠY, không chỉ viết:**

- D-pad + 2 nút hành động cho farm. **Đọc CON TRỎ, không phải sự kiện chạm** — SDL tự
  tổng hợp chuột từ ngón tay, nên platform seam **không cần thêm gì**, và toàn bộ
  Studio (vốn chỉ đọc chuột) vẫn dùng được trên thiết bị cảm ứng.
- **MỘT `layout()`** cho cả renderer lẫn hit test.
- `consumed` chặn thế giới đọc con trỏ khi nó nằm trên nút.
- Vừa hay quyết bằng **TỈ LỆ**, không phải ngưỡng pixel.
- 76/76 · ASan sạch · 7 mutation giết hết · golden path xanh ·
  **đã chạy bằng CHẠM THẬT** trong viewport 390×844.

**Bug renderer lộ ra:** `draw_round_rect` vẽ cạnh thẳng **đục** và cung góc **có
alpha** — một lời gọi, hai hành vi, ẩn từ chương 69 vì mọi viền đều đục.

**Bài học ghi lại:** một mutation sống sót **hai lần** vì test *trông như* có phủ.
Lần một: viewport để bản đồ letterbox nên nút không bao giờ nằm cạnh người chơi.
Lần hai: bấm `W` để quay mặt **cũng đi một bước**, làm ô dưới nút không còn kề bên.
**Đặt người chơi bằng file save, và đặt SAU khi quay mặt.**

## S16 — những mảnh mà pack không có (chương 125) — XONG 2026-09-06

Commit: `be11013` (cửa thứ ba + 16 mảnh art) · `2849d3f` (luật LINE + theme + farm).
Merge `--no-ff` vào `main`.

**Đã CHẠY, không chỉ viết:**

- **`--cmd asset.pixels <src.pix> <dst.hrt>`** — cửa thứ ba và cuối cùng vào `.hrt`.
  Ba nguồn gốc, một format: **nhập về / sinh ra / vẽ tay**.
- **Nguồn là TEXT, không phải Pixel workspace** — và lý do rất cụ thể: một bộ autotile
  16 mảnh **không phải 16 bức vẽ**, nó là **một profile hành lang cắt 16 kiểu**, thứ
  phải đúng là **quan hệ giữa các mảnh**, mà quan hệ thì đọc/diff/review chứ không bấm
  qua 16 canvas.
- **`assets/textures/farm_path.{pix,hrt}`** — 64×64, đúng 16 mảnh Tiny Town không có
  (pack chỉ có **mảng 9 mảnh** để lấp *vùng*; đường rộng 1 ô là **đường thẳng**).
  Màu lấy từ `town.hrt` ô 40 để nằm **trong** bảng màu của pack — ghi vào ATTRIBUTION.
- **`autotile ground 2 path 0`** — một dòng theme. Bản đồ vẫn ghi id 2 khắp nơi.
- **`autotile_index` (47) vẫn chưa có người dùng, và giờ đã GIẢI THÍCH được**: luật 47
  dành cho *vùng*; đường 1 ô không đường chéo nào đủ hai cạnh → chỉ 16/47 với tới được,
  rải trong sheet có 31 ô không vẽ nổi. Nên có `autotile_line_index`, **chỉ số CHÍNH LÀ
  mặt nạ 4 bit** → vị trí trong lưới 4×4 là nghĩa của mảnh.
- 76/76 · ASan sạch · **12 mutation giết hết** · golden path xanh · web build xanh ·
  **đã render và NHÌN**: sheet, và bản đồ farm — con đường liền một dải, có đầu mút hai
  đầu và một khúc cua thật.

**Bài học ghi lại:** **hai mutation sống sót vì test so *cả khung hình*.** So khung
chứng minh "mảnh **thay đổi** theo ô" chứ **không** chứng minh "mảnh **đúng**" — cả hai
mutation vẫn cho ảnh biến thiên, chỉ là sai. Sửa **không** phải bằng thêm assert lên
ảnh, mà bằng cách nhận ra bộ chọn mảnh đang là **method private của Scene** — đúng cái
hình dạng không kiểm được nếu không có renderer. Đưa ra thành hàm thuần
`farm::line_piece` trong `farm_core` (đúng luật D của spine), rồi kiểm **cả 16** vùng
lân cận + 5 ô mốc trên bản đồ thật. **Guard thừa thứ tư** liên tiếp (121, 122, 123, 125).

## S17 — những động từ ngón tay không với tới (chương 126) — XONG 2026-09-06

**Đã CHẠY, không chỉ viết:**

- **Hotbar bốn ô GIỜ LÀ CONTROL**, không còn là bức tranh của một control chưa tồn tại.
  Nó vẽ đúng bốn tool từ chương 113 và không trả lời gì. Hình học của nó **chuyển vào**
  `controls.hpp` (chứ không **chép** ra) — vì một dải vừa trả lời tap vừa tự tính toạ độ
  là **hai layout cho một màn hình**, đúng con bug dòng đầu file đó nói.
- **Hai chiều cao, MỘT điều kiện**: `hud_height = pad_fits ? 44 : 24`. 44 là cái ngón tay
  cần; 24 là cái *nhãn* cần. Cả hai đều đúng, về hai thứ khác nhau — và nó thành *target*
  đúng lúc pad vừa, tức cùng một câu hỏi, hỏi một lần.
- **Nút `save` (F5)** — một hàng TRÊN hàng ngón cái, để với `use` không trượt vào nó.
- **Hai đáp án xung đột cloud (F6/F7) THAY THẾ `save`**, không đứng cạnh. `save_game()`
  gọi thẳng `push_save()`, nên bấm save lúc xung đột **âm thầm nghĩa "bản của tôi
  thắng"** — không được phép mời điều đó dưới dạng ô 44px cho người chưa từng thấy câu
  hỏi. `keep` lấy ghế ngoài (dễ với) vì nó **không đổi gì**; `take` phải với xa.
- **Nút ghi TÊN PHÍM nó thay** (`F5`/`F6`/`F7`, cạnh `Z`/`Q`) → chip góc phải
  "F6 keep yours / F7 take cloud" **thành legend của chúng, không sửa một chữ**.

**Tìm ra một KHOÁ CỨNG khi kiểm tra:** nhánh `talking_` trong `update()` **`return`
trước khi đọc con trỏ**. Mở hộp thoại với Anna = **đóng băng vĩnh viễn** (không trả lời,
không đi, không lưu, không thoát; lối ra duy nhất là tắt app), màn hình **trông hoàn toàn
bình thường**. Sống từ chương 124 — chương *tuyên bố* "chơi được bằng tay" — và không ai
thấy vì **`test_farm_scene` KHÔNG CÓ test hội thoại nào cả**. Sửa: `talk_layout` +
`read(Talk)` trong cùng file; **chạm một lựa chọn để chọn, chạm panel để sang dòng**,
không thêm nút nào. Panel cũng chuyển lên **trên** hotbar (trước đó `h - box_h - 12`,
đúng với dải 24 và che dải 44).

**Và một lỗi thứ tư chỉ ẢNH CHỤP mới thấy:** nút `v` của d-pad vẽ đè lên chip cảnh báo
config — đúng dòng mà comment của nó gọi là "dòng operator phải đọc được từ đầu phòng".
76 test xanh, 20 mutation chết hết, compiler im lặng. Giờ đã có test: tìm **bbox nền đục
của chip** rồi cấm mọi control cắt qua nó — nói bằng LUẬT, không bằng ngưỡng pixel.

- **13 hình chữ nhật đổi bản chất của test hình học.** 6 cái thì nhìn được; 13 thì không,
  và hai cái chồng nhau 4px là trò tung đồng xu người chơi luôn thua. Nên: **sweep tính
  chất** ~12.000 layout (480..1920 × 260..1200, bước lẻ, × 2 trạng thái conflict) — mọi
  box còn sống đều nằm trong màn hình, **không hai cái nào chồng nhau** — và sweep **tự
  kiểm chính nó** (`with_pad > 100 && without_pad > 100`).
- **`Box` rỗng LÀ toàn bộ guard.** `contains` đã false với mọi điểm trong box rộng 0, nên
  control chưa được layout **không thể** bị bấm, kể cả bởi code quên hỏi. Thay cho
  `bool has_save` — hai sự thật về một thứ thì sớm muộn cũng cãi nhau. Test then chốt:
  **cùng một pixel**, trên hai layout, phải cho hai câu trả lời khác nhau và không lẫn.
- **20 mutation: 18 chết ngay; 2 sống sót và cả hai CÙNG MỘT LỖI** — `if (b.empty())
  return;` trong renderer nút, và pad vẫn vẽ khi hộp thoại giữ input. Mọi test đều hỏi
  *"bấm được không"*; **không cái nào hỏi thứ ĐANG VẼ có bấm được không**. Hai claim hành
  vi đóng cả hai (giữ chuột trên `use` không đổi một pixel nào khi hộp thoại đang mở; một
  control vắng mặt không để lại chữ ma ở góc), rồi 20/20.
- 76/76 · golden path xanh · web build xanh · **đã render và
  NHÌN** ba khung: pad + F5, xung đột (F6/F7 dưới chip), và hộp thoại 3 lựa chọn.

**Bài học ghi lại:** *"chơi được bằng tay"* được tuyên bố ở ch.124 dựa trên **ba động từ
đi được**, trong khi game có bảy — và cái thứ bảy **treo máy**. Khoảng cách không nằm ở
chỗ test yếu, mà ở chỗ **không có test nào cho đường đó**. Trước khi tin một tuyên bố về
"dùng được", hãy **liệt kê động từ** và đối chiếu từng cái, đừng đối chiếu ấn tượng.

## S18 — màu không có trên sheet (chương 127) — XONG 2026-09-06

**Trần đã đóng:** Pixel workspace có hai cửa chọn màu và **cả hai đọc từ file** —
palette là màu ảnh *đang có* (`build_palette` đếm tần suất), eyedropper là *một pixel*.
Tập màu vẽ được **bằng đúng** tập màu đã có trong file: sắp xếp lại được một sheet,
nhưng không thêm nổi một điểm sáng đậm hơn một sắc. Đó là công cụ **chỉnh sửa**, không
phải công cụ **vẽ**.

**Đã CHẠY, không chỉ viết:**

- **`engine/paint/colour.{hpp,cpp}` — core thuần**, không renderer/UI/IO: `to_hsv`,
  `from_hsv`, `to_hex`, `parse_hex`. `to_hex` cũ nằm trong anonymous namespace của
  workspace; nay một bản, hai chỗ dùng.
- **Hai cửa, vì là hai câu hỏi.** Ba slider HSV = *"đậm hơn chút, vẫn màu đó"* (một sắc
  độ là **một trục** HSV, **ba trục tương quan** RGB). Ô hex = *"#8B5A2B, đúng màu pack
  dùng"* (slider kéo từng pixel, không ai **nói** cho nó một bộ ba chính xác được).
  Palette vẫn đứng đầu — nó vẫn là mặc định đúng; mixer là cho màu **chưa có** trên sheet.
- **Ba slider, không phải bốn.** Alpha đã có control riêng (ô 0 + chuột phải xoá) và ô
  hex nhận `#AARRGGBB`, nên cọ nửa trong suốt cách một dòng chữ — trong khi slider thứ tư
  tốn thêm 34px của một panel *đang thiếu chỗ*.
- **Trạng thái mixer KHÔNG phải là màu.** Nếu suy ba slider ra từ `colour_` mỗi frame,
  kéo Value xuống đáy **quên cả hue lẫn sat** (mọi màu v=0 đều là đen) và kéo lên lại ra
  **trắng** — cú kéo thành một chiều. Nên workspace giữ `paint::Hsv mix_` + `mix_a_`, và
  **mọi** cách chọn màu (ô palette, eyedropper, mã gõ vào, mở file) đi qua **một**
  `adopt()`.
- **Round-trip HSV chính xác TUYỆT ĐỐI**: quét đủ **16.777.216 màu → 0 sai lệch** (76 s
  bản debug, chạy một lần; CI chạy lát cắt 65.536 màu + các mốc sextant). Đây là claim
  **bằng nhau**, không phải sai số: lệch một đơn vị mỗi lần chạm thì màu trôi dần và
  **không gì trên màn hình nói ra** — chỉ file mới thấy, và lúc đó sheet đã lệch.
- **`+ 0.5f` trong `to_byte` là load-bearing**: `v*255` với kênh 200 ra `199.99998`, cắt
  cụt là mất round-trip ở gần như mọi màu.

**Một dòng ở `end()` có người dùng thứ hai.** Command palette nhận bàn phím lúc mở và
không bao giờ đòi lại; giờ click vào scrim sẽ **xoá focus** → palette mở mà điếc, không
phân biệt được với treo. Nên palette **lấy lại focus bất cứ khi nào không ai giữ** — đúng
nghĩa "overlay này sở hữu màn hình" mà `confirm()` vẫn làm. Thay đổi một dòng ở tầng
chung vẫn phải đi nhìn quanh.

**Guard đẻ ra lỗi nặng hơn lỗi nó sửa.** B/R/G/I vừa là tool vừa là chữ số hex, nên gõ
`#8B5A2B` đổi tool hai lần giữa chừng → workspace đứng phím lại khi ô hex giữ bàn phím.
Nhưng `ui::Context` **chỉ** chuyển focus khi **widget khác** nhận, nên click ra canvas —
nơi con trỏ của editor pixel sống cả ngày — **không** trả bàn phím: sau MỘT lần gõ vào ô
hex, **mọi phím tắt chữ chết đến hết phiên**, và không gì nói tại sao. Sửa **một dòng ở
`ui::Context::end()`** (`if (in_.pressed && hot_ == 0) focused_ = 0;`), không ở
workspace: click ra ngoài ô nhập nghĩa là gì thì ở đâu cũng vậy. Test chạy **cả hai
chiều** — guard không bao giờ nhả là *cùng con bug quay mặt lại*, và đó là nửa không ai
viết test.

**Panel hết chỗ thì control không tràn — nó BIẾN MẤT.** `ui::slot()` **cắt** kích thước
xuống phần còn lại, nên control cuối nhận rect **cao 0**: vẽ không ra gì, hover/bấm/focus
đều không trúng, panel phía trên **trông hoàn toàn bình thường**. Mixer làm inspector cao
thêm ~130px — đúng thay đổi để lộ chuyện đó. Guard là **hỏi chiều cao rồi so với cái nhận
về** (`save_row.h < 30`), và nói ra ở **status bar** — bên ngoài panel, vì panel quá ngắn
thì bên trong không còn chỗ để nói. Đây là bug chương 126 **nhìn từ mặt kia**: không phải
*vẽ ở chỗ không bấm được*, mà **không vẽ ra gì cả**.

**Và ảnh chụp lại tìm ra lỗi 76 test không thấy:** nhãn `COLOUR   from this image,  RMB
erases` **bị cắt cụt** thành `...RMB era` ở mép phải panel 280px. Không assertion nào
trong file đó nói được điều này; chỉ khung đã render mới nói.

- **17 mutation** trên `colour.cpp` / `pixel_workspace.cpp` / `ui.cpp`: **15 chết ngay**;
  **2 sống sót và cùng một loại** — đều là *câu trong header mà không assertion nào kiểm*.
  Bỏ wrap hue trong `to_hsv` vẫn round-trip hoàn hảo (vì `from_hsv` tự chuẩn hoá) nhưng
  slider hue (0..360) sẽ kẹt knob ở trái và in `hue: -30.10`; bỏ `fmod` trong `from_hsv`
  chỉ lộ ra khi hue < **-360°**, thứ không slider nào tạo ra được. Đóng cả hai bằng cách
  kiểm **hợp đồng**, không kiểm cách dùng hôm nay → 17/17.
- 76/76 · ASan+UBSan sạch · golden path xanh · web build xanh · **đã render và NHÌN**
  panel mixer (slider hiện đúng hue 210 / sat 0.50 / val 0.25 của `#FF203040`).

**Bài học ghi lại:** một guard mới **luôn** có chiều ngược lại, và chiều ngược lại thường
tệ hơn. "Ô nhập giữ phím tắt" đúng; "và không bao giờ trả lại" là bug nặng hơn cái ban
đầu. Viết test cho **cả hai chiều** của mọi guard, không chỉ chiều nó được sinh ra để đỡ.

## S19 — trang nó xuất bản trên đó (chương 128) — XONG 2026-09-06

**Vấn đề:** ch.126 làm mọi động từ của farm với tới được bằng ngón tay, ch.127 mở khoá vẽ
art — cả hai đổ về **một trang web** mà người khác vào được, và trang đó vẫn là **debug
shell**: font mono, panel log luôn hiện, canvas chặn ở `78vh`, **0 dòng** về chạm. Bản
web chưa từng có ngón tay nào đưa vào.

**Đã CHẠY, không chỉ viết:**

- **Chạm THẬT, chứng minh bằng giá trị.** `scripts/web_touch_check.mjs` drive Chrome qua
  CDP, bật touch emulation, bắn `Input.dispatchTouchEvent` (không phải click), rồi đọc
  `var px` ra khỏi **file save game tự ghi**: giữ nút `>` → `px` 4 → 8. Bằng chứng mạnh
  nhất đến từ việc **cố tình phá**: nhắm sang `<` → `px` 4 → **1**. Ngón tay không phải
  "không tới" — nó tới, và đi sang **tây**.
- **Nhắm bằng cách HỎI GAME.** Farm in một dòng stderr lúc render đầu:
  `farm: controls 640x360 right=116,206,44,44 …`. Harness đọc dòng đó rồi ánh xạ
  logical → CSS. Tự tính `44*390/640` là **bản sao thứ ba** của luật ch.126 và là bản
  đầu tiên lạc hậu.
- **SDL2 bản Emscripten DỰNG chuột từ ngón tay — không cần hint nào.** Trước nay chỉ là
  câu trong tài liệu.

**Lỗi thật lại nằm chỗ không ai ngờ: canvas BỊ KÉO GIÃN.** Emscripten tự đặt
`style.width/height` inline; sau đó `max-width`/`max-height` **cắt hai trục độc lập** —
quy tắc giữ tỉ lệ của phần tử thay thế hết hiệu lực. Trên máy 390px, game 1280×720 hiện
ra **390×720**: mọi assertion xanh, chạm vẫn đúng ô (SDL ánh xạ qua **cùng** cái hộp sai
hình đó), và hình cao gấp 2,5 lần. **Chỉ ảnh chụp nói ra** — chương thứ **ba liên tiếp**
như vậy (126: nút vẽ đè chip; 127: nhãn tràn panel). Sửa bằng `fit()` + `ResizeObserver`
+ `MutationObserver`, và ghi assertion `|shown - drawn| <= 0.02`.

**Một guard KHÔNG kiểm được, và nói thẳng là không kiểm được.** Lật `touch-action` sang
`auto` → **vẫn qua hết**. Probe riêng cho biết vì sao: CDP bỏ qua `touch-action` hoàn
toàn (kéo cuộn được **đúng 110px** ở cả hai giá trị). Nên nó là **tuyên bố ý định**, giữ
lại vì pinch-zoom/double-tap-zoom; cái *được* kiểm là **trang không cuộn**, tức không có
gì để cướp.

- **4 phép biến đổi, 4 chết**: `fit()` thành no-op → bắt được kéo giãn; log hiện mặc
  định; bar cao 300px; nhắm sai nút.
- CI: job `web-build` trước đây **chỉ build** — nay có bước **chạy trang**.
- Chạy ở **cả hai hướng màn hình** (390×844 và 844×390); đã render và **nhìn** cả hai.

**Chưa xác minh:** `touch-action: none` (harness không thể) · vẫn **một ngón** · **portrait
là con tem** (390×219 → nút d-pad 27px, dưới mọi khuyến nghị; trang chỉ khuyên xoay máy) ·
chỉ Chrome, chưa có Safari/iOS · `?shell=` trên điện thoại chưa kiểm.

## S20 — nửa bộ test chưa từng chạy (chương 129) — XONG 2026-09-06

**Con số chưa ai viết ra:** `ctest` ở máy này = **76**; CI = **48**. Chênh **28** là toàn
bộ nửa Drogon-gated — auth, JWT, RBAC, purchase, cloud save, inventory, secret rotation,
idempotency, và **ba bài end-to-end dựng server thật**. Tối ở CI **từ ngày được viết**.

**Và con số trong kế hoạch của tôi cũng sai.** Ghi "23 test" — đó là số **file**
`test_baas_*.cc`. Đo thay vì đếm: cấu hình lại với `-DCMAKE_DISABLE_FIND_PACKAGE_Drogon=ON`
(đúng cái CI thấy) → 48. **File không phải test.**

**Đã CHẠY, không chỉ viết:** job mới chính là build stage của `baas/ops/Dockerfile` (có từ
ch.107) dừng ở test. Hai mẩu build system để job không phải mang danh sách:
`BUILDSYSTEM_TARGETS` → target `baas_tests`; và `ctest --test-dir build/baas` chọn **đúng**
28 test của thư mục đó — **thư mục LÀ nhóm**, không regex (hai test tên `metrics` và
`rate_limiter`, không pattern `baas_*` nào bắt được), không danh sách tên để lạc hậu.

**Bắt CI chạy lộ ra bug thật trong phút đầu.** `test_sdk_realtime_live` **không biên dịch**
trên libcurl 7.81 (dùng `curl_ws_recv`, API có từ 7.86; Ubuntu 22.04 = base của image
Drogon). Điểm đáng nói không phải phiên bản mà là **sự bất đối xứng**: SDK **đã** xử lý —
thiếu header thì transport realtime thành **stub** và in ra một dòng; còn **test kiểm chính
cái stub đó lại không gate**, và một test không biên dịch được thì không làm hỏng một test,
nó làm **hỏng cả build** — kéo theo 27 cái kia. Không ai thấy vì **máy duy nhất từng dựng
nửa này là một cái Mac có curl 8.x của Homebrew**.

**Phát hiện bên dưới, về SẢN PHẨM chứ không phải CI:** transport ws:// native của SDK là
**stub trên mọi bản Ubuntu 22.04** — kể cả **image container của chính backend này**.
Người dùng Linux dựng SDK ở đó có REST và **không có realtime**, và thứ duy nhất nói ra là
một dòng `STATUS` lúc configure. Không có gì báo lúc chạy.

- **27/27 xanh trong container**, 51 s, đã chạy thật ở **đúng image và đúng kiến trúc
  (amd64)** của runner **trước khi** đẩy YAML — trong khi file CI vẫn còn dòng "written but
  not run in the authoring environment" (nay đã thu hẹp lại đúng phần nó còn mô tả).
- Job **assert sàn 27**: một job xanh mà chạy 0 test là đúng cái thất bại slice này tồn tại
  để chấm dứt, và không có dòng đó thì nó **không phân biệt được** với job chạy đủ.
- 76/76 ở máy · YAML đã parse kiểm.

**Chưa xác minh / trần:** `sdk_realtime_live` vẫn chỉ chạy ở **một máy** (cần libcurl ≥
7.86) · realtime native là stub trên 22.04, **im lặng lúc chạy** · chỉ SQLite (Postgres +
`FOR UPDATE` vẫn là **một** slice, tách ra là ship một race) · **không có job Docker**:
CI dựng và test backend, không dựng image, không gọi `/healthz` · bản kiểm ở máy chạy
amd64 **giả lập**; native amd64 là lần chạy đầu của CI.

## S21 — một cái link gửi được (chương 130) — XONG 2026-09-06

Chương 128 làm **một** game với tới được bằng ngón tay, ở một URL không ai đoán ra. Slice
này làm nốt nửa còn lại: `web/collection.html` liệt kê mọi `*.gameproject` thành card (bìa,
một câu, Play), và **Play đáp xuống trong game đang chạy** — có kiểm, không suy luận.

- **`project_core` thêm `cover` + `summary`**, tuỳ chọn, và `to_text` **chỉ ghi khi có** —
  đó CHÍNH LÀ migration, và nó là một test: `to_text(parse(old)) == old`. Một dòng
  `summary ` luôn ghi sẽ viết lại mọi manifest chưa từng nghe nói đến summary, ngay lần lưu
  đầu tiên.
- **`cover` vào closure** (nó ship thì nó được hash) — **trừ khi** manifest đã khai đúng
  path đó. Bìa của farm chính là tileset của nó; hash hai lần sẽ **đổi release id mà nội
  dung không đổi**. Bằng chứng: publish sau thay đổi báo `verified`, không phải id mới.
- **Chỉ mục được BAKE, không phải danh sách ai đó giữ.** `--cmd collection.index` +
  `assets::list_dir` (verb thứ tư của seam I/O, **sắp xếp** vì nó nuôi một file được
  commit). `assets/collection.json` đứng đúng quan hệ mà `.hrt` có với `.recipe`/`.pix`:
  test re-bake và so từng byte, và **đếm** entry bằng `list_dir` — thêm game mà quên
  index là một test đỏ, không phải một game biến mất khỏi trang.
- **Trang tự giải mã `.hrt`** (15 dòng `DataView`) → bìa là **đúng file engine đọc**, không
  mở cửa PNG nào vào pipeline. `.hrt` vẫn **ba** cửa; cửa thứ tư là quyết định của S26.
- **Ảnh chụp lại tìm ra lỗi — chương thứ TƯ liên tiếp.** Mọi assertion xanh trên trang có
  canvas 320×180 bị CSS thu xuống 258×145: scale **nguyên** tính rất kỹ rồi bị nhân 0,81
  ngay sau đó. Sửa bằng đúng cách chương 128: buffer lấy kích thước từ chính cái hộp nó
  hiện trong, + `ResizeObserver`. Bìa giờ là `64x64@9x` và `192x176@3x` — nguyên, **theo
  pixel thiết bị**, là chỗ duy nhất "nguyên" có nghĩa.
- **Bẫy `POST_BUILD` sập trong vòng một giờ** kể từ lúc tôi viết chú thích về nó: nó chỉ
  chạy khi `demo` link lại, nên sửa trang thì không copy gì và bài kiểm vẫn kiểm **trang
  cũ** — y hệt bẫy `--shell-file` ở ch.118. Đổi thành `add_custom_target` + `add_dependencies`.
  Thứ được copy là **allowlist** (`textures`, `projects`), không phải "cả cây trừ vài chỗ":
  `assets/` còn chứa `saves/`, và ch.128 là chuyện xảy ra khi một *denylist* quên một dòng.
- **Công cụ đo test lại là thứ nói dối.** Lần mutation đầu: 14/15, sạch đáng ngờ. Harness
  khôi phục **nội dung** nhưng đẩy **mtime lùi**, nên object của mutation trước sống sót:
  từ M4 trở đi **mỗi lần chạy có hai mutation**, cái thứ hai (bỏ một bounds guard) làm
  `test_collection` abort trước khi chạm assertion nào. Sửa `os.utime` + in
  `baseline after restore` → **15/15, baseline GREEN**. Bài học không phải về mtime: điểm
  mutation là một *tuyên bố về test do một chương trình sinh ra*, và chương trình đó **không
  có test nào**.
- Sống sót thật (đã đóng): không có gì kiểm rằng một **thư mục** tên `x.gameproject` không
  phải project — để nguyên thì nó thành một card hỏng ma, báo lỗi cho một manifest không ai viết.

**Số liệu:** 77/77 ctest · ASan+UBSan xanh trên 7 suite liên quan · **15/15 + 4/4 mutation**
· golden path xanh, 0 rò `.tmp`, package hash **không đổi** · web build xanh · bài kiểm
trình duyệt: 2 game liệt kê, 2 bìa giải mã (205c/32c), README render (6 heading, 1 bảng,
11 mục, 1 code block), **chạm Play → game chạy**.

**Chưa xác minh / trần:** bìa là **texture có sẵn**, không phải title card hay khung hình
chụp (cửa thứ tư — S26) · **không có `/play/<hash>`**: link trỏ vào manifest trong cây làm
việc, không phải release đã publish (cần server — S29) · markdown là **một tập con** (không
link, ảnh, list lồng, blockquote) · **Chrome only** · chỉ mục **bake bằng tay**, test chỉ
bắt được sau · `build-web/assets/` trùng lặp nửa MB với `demo.data`.

## S22 — luật mà máy giữ (chương 131) — XONG 2026-09-06

Merge `feat/s22-provenance`. Ba commit.

**Vấn đề thật, đo được trước khi sửa:** `CLAUDE.md` mang luật *"mỗi `.hrt` mới phải có
một dòng trong ATTRIBUTION.md, cùng thay đổi"* từ chương 122. Repo có **23 file `.hrt`**;
file đó gọi tên **ba**. Hai mươi cái còn lại nằm dưới một câu kết bao trùm — nhiều khả
năng đúng, **không kiểm được**, đúng hình dạng lời cam đoan mà ch.129 tìm thấy dưới 28
test chưa từng chạy.

| Commit | Việc |
|---|---|
| `669354c` | `assets::list_tree` (đệ quy — `list_dir` chỉ walk `textures/` thì bỏ sót `pieces/`, `sprites/` và gốc = 14/20 lỗ) + `provenance_core`: ba cửa để lại ba dấu, `Ledger::ok()` là boolean mà luật kia luôn ngụ ý. Hai file `.pack`; ledger sinh ra giữa hai marker trong ATTRIBUTION.md. |
| `7a5f4f3` | `asset.new` — `.pix` **trước**, bake, khai vào manifest, re-bake ledger, một hành động. `paint::blank_sheet`. |
| `5a9fdfc` | Studio: trường tên + nút Create trong Pixels; card asset có ORIGIN/FROM/LICENCE; `--lab` cũng đăng ký command table. |

**Ba thứ tìm ra khi làm, không phải khi lập kế hoạch:**

- **`asset.new` bản đầu kiểm manifest SAU khi ghi file.** `asset.new x 16 1 1
  nosuch.gameproject` báo lỗi và để lại hai file thật + ledger cũ → `ctest` đỏ vì một lý
  do không liên quan gì đến việc đang làm. Giờ mọi thứ từ chối được đều từ chối **trước**
  khi ghi — đúng bài stage-then-rename mà release store đã có.
- **Chưa test nào trong `test_pixel_workspace.cpp` từng NHẢ chuột.** `ui::interact` kích
  hoạt khi `released`; helper `mouse()` không bao giờ set field đó. Nên bộ test kéo được
  slider, gõ được vào field, và **chưa bao giờ bấm một cái nút nào**. Bốn nút tool, Undo,
  Redo, Save — vẽ trong mọi khung hình của mọi test, bấm trong không test nào. Đúng điểm
  mù ch.126, nằm dưới một suite trông rất kỹ.
- **Cửa import chưa từng được so byte.** `.recipe` và `.pix` đều được bake lại và so;
  bản import thì không — và nó là cửa **có giấy phép đứng sau**. Giờ mọi dòng `import`
  trong mọi `.pack` được chạy lại và so byte, đọc từ pack chứ không gọi tên Kenney.

**Mutation — 20 phép, 18 KILL, baseline sau khi restore GREEN.** Bốn cái sống sót ban
đầu đều là lỗ thật, đã đóng:

| Sống sót | Lỗ nó chỉ ra | Đã đóng bằng |
|---|---|---|
| `sibling()` dùng dấu chấm **đầu** thay vì cuối | mọi tên file trong repo có đúng một dấu chấm, nên `a.b.hrt` sẽ đọc là UNRECORDED trong khi source nằm ngay cạnh | test với `textures/a.b.hrt` |
| markdown in `0 unrecorded` bất kể thật | dòng người ta liếc qua; "0" in đè lên một lỗ còn tệ hơn không in gì | assert nguyên chuỗi `1 raster assets, 1 unrecorded.` |
| `asset.new` ghi đè `.hrt` có sẵn | guard `.pix` che mất — nhưng **20/23 file trong repo là `.hrt` KHÔNG có `.pix`**, gồm cả sheet CC0 đã import, nên đây là thứ duy nhất chặn `asset.new town ...` ghi đè Kenney | test tạo `.hrt` trần rồi assert từ chối + byte không đổi |
| nút Create luôn enabled | hàm vẫn từ chối nên **không có gì xảy ra** trong cả hai trường hợp; khác biệt quan sát được duy nhất là nút sai sẽ **nói** một lỗi không ai hỏi | assert `take_message()` rỗng — nút disabled thì **im lặng** |

Hai cái còn sống, **có chủ ý ghi lại**: `size < 1` → `size < 0`, và cap kiểm bằng phép
nhân thay vì phép chia. Cả hai cùng một guard: trên arm64, chia cho 0 không trap và phép
nhân ở các giá trị test được không tràn, nên **không phân biệt được bằng quan sát** — muốn
giết phải yêu cầu một sheet đủ lớn để treo cả suite. Guard viết bằng phép **chia** vì nó
không thể tràn; test không chứng minh được điều đó.

**Số liệu:** 78/78 ctest · **18/20 mutation** · golden path xanh, 0 rò
`.tmp`, package hash **không đổi** `cd1c2864f8315bff` · web build xanh · một khung hình đã
render và **đã nhìn** (inspector NEW SHEET; card ORIGIN/FROM/LICENCE).

**Chưa xác minh / trần:** sheet mới **16px, một ô** (CLI nhận mọi cỡ, nút thì không) ·
sheet mới mở ra với **palette rỗng** — phải qua mixer mới có màu · **`declared` là lời
hứa**, 20 file vẫn không bake lại được từ gì cả · ledger **bake bằng tay** (chỉ
`asset.new` tự re-bake; ba cửa kia không) · chỉ theo dõi `.hrt` — font/map/`.def`/scene
không có ledger · **không có trình nhập `.pack`**: không tải, không giải nén, không kiểm
checksum · **bốn nút tool vẫn chưa từng bị bấm** trong test (chưa publish rect).

## S23 — một định dạng map, một trình sửa (chương 132) — XONG 2026-09-06

Merge `feat/s23-one-map`. Ròng **−306 dòng**.

**Món nợ, và lý do nó không trả được sớm hơn** — chính `main.cpp` ghi:
*"Still writes fpsmap1, still the only place entities/spawns can be edited."*
Vẽ tile chuyển sang Map workspace từ ch.112. **Đặt spawn thì không.** Nên 285 dòng scene
+ một thư viện edit song song + một định dạng file song song sống thêm mười chương vì
**một động từ** chưa chuyển. Bài học rẻ nhất về migration: **5% cuối giữ 100% cái cũ sống.**

| Commit | Việc |
|---|---|
| `3672e78` | `place_entity` / `set_entity_prop` → `doc::Command`. Undo một lần TẠO thì **xoá** entity; kéo thì gộp, tạo thì không. |
| `3a87b8e` | Entity tool: kéo đặt, nút Facing, vẽ marker trên canvas. Canvas + inspector + palette gọi **một** hàm. |
| *(tiếp)* | `--cmd map.migrate`, `level_00.map` → `.map2`, xoá Map Lab + `fps::to_text/from_text`. |

**Lỗi tự mình vừa tạo, do bước 3 phát hiện ra ở bước 2:** nút Facing ghi `facing=E`
(chữ); raycaster đọc `dir` (radian). **Game sẽ bỏ qua mọi hướng editor đặt.** Mọi
assertion đều xanh vì tất cả dừng ở *"thuộc tính đã đổi"*. Đây là lần thứ **ba** lỗi nằm
đúng ở **bước nhảy cuối** — bàn phím ch.123 nối vào canvas không ai focus, nút Play
ch.130 có href đúng mà không mở gì. Test giờ đi hết: lưu file rồi đọc lại bằng
`fps::from_shared_text` và so `spawn_dir`.

**Bằng chứng sống lâu hơn file.** `test_fps` nhúng đúng những byte `level_00.map` mang
suốt 120 chương và khẳng định chúng migrate ra **đúng từng byte** file map2 đang commit —
mạnh hơn bài test cũ, vốn chỉ so hai trình đọc trên cùng một file còn sống.

**Xoá writer, không phải deprecate nó.** `tilemap::load` đã migrate fpsmap1 từ ch.110 —
đủ để **đọc** file cũ và không hề đủ để ngừng **tạo** file cũ. Giờ: `maplab/` xoá,
`fps::to_text/from_text` xoá, chỉ còn `tilemap::from_fpsmap1` là cửa một chiều. **Một
định dạng không ai ghi được thì không quay lại được.**

**Bốn test đỏ đúng như phải đỏ:** migrate map làm `collection.json` cũ (package hash đi
theo nội dung), làm hỏng hai đường dẫn hard-code, làm lệch số lệnh. Cả bốn là guard của
các chương trước bắn đúng lúc — bằng chứng duy nhất rằng chúng hoạt động.

**Ảnh chụp lại tìm ra lỗi — chương thứ NĂM liên tiếp**, và lần này là lỗi tôi vừa tạo 60
giây trước: mục ENTITY đẩy inspector quá đáy panel, nơi `slot()` clamp về rect cao 0 —
nút Save vẽ ở đâu không ai biết và bấm ở đâu cũng không được. `inspector_clipped()` giờ
có ở đây như Pixels đã có từ ch.127, và status nói ra.

**Mutation 16 phép, 16 KILL, baseline sau restore GREEN.** Lần đầu 14/16; **cả hai cái
sống sót là cùng một lỗ — `map.migrate` không có test nào.** Chĩa nó vào một file map2,
và bắt nó không ghi gì mà vẫn báo thành công: cả hai sống sót qua toàn bộ suite.

**Và harness ăn mất một commit.** Nó sửa file tại chỗ, và tôi chạy `git add -A` khi nó
đang chạy → mutation M13 (`if (false) place_selected(...)`) vào thẳng commit. Thứ bắt
được là **dòng cuối của chính harness** mà ch.130 thêm vào: `git diff --stat` sau restore
in ra một file lệch thay vì "sources restored clean". Đã `--amend` trước khi push. Luật
mới: **không đụng vào index khi harness đang chạy** — nó sở hữu working tree.

**Số liệu:** 77/77 ctest (78 trừ `test_maplab` đã nghỉ) · **16/16 mutation** · golden path xanh, 0 rò `.tmp`,
release id **đổi đúng như phải đổi** (`cd1c2864` → `eac0e534`: nội dung map đã đổi) ·
web build xanh · một khung hình đã render và **đã nhìn**, hai lần.

**Chưa xác minh / trần:** **trigger vẫn không sửa được** trong UI (map2 có từ ch.110) ·
chỉ sửa được thuộc tính `dir`, mọi prop khác round-trip nhưng không với tới · **không xoá
được entity** (chỉ undo) · danh sách entity = những cái đã có + `spawn_player`, không có
catalogue loại · `--lab map` hard-code một đường dẫn (lab không có manifest) · `fps::Map`
vẫn là lưới `uint8` riêng, id > 255 bị clamp.

## S24 — bốn lab, một actor (chương 133) — XONG 2026-09-06

Merge `feat/s24-effects-as-components`. Ròng **−492 dòng**. **13 lab → 9.**

**Vấn đề, nói thẳng:** `--fx --light --audio --anim` là bốn cửa sổ demo bốn core của
engine, mỗi cái một bộ slider. Cả bốn đều là **ngõ cụt**: hạt trong `--fx` không lưu
được, đèn trong `--light` là ba struct hard-code trong constructor, nốt trong `--audio`
là bốn số trong mảng. Chúng chứng minh engine *có* tính năng — và chính là lý do không
chỗ nào khác có.

Ràng buộc thứ tự (D79, ch.120): **hấp thụ trước, xoá sau.** Lab audio là caller **duy
nhất** của `audio::Mixer` trong toàn repo; xoá trước thì `audio_core` thành thư viện có
test và không có người dùng — code chết với suite xanh, loại đắt nhất.

| Commit | Việc |
|---|---|
| `ea5316a` | `Emitter` / `Light` / `Sound` + đồng hồ flipbook trên `Sprite`, trong `sandbox_core`; serialize `emitter=` `light=` `sound=` `noloop`. |
| `f9a989a` | Mục EFFECTS trong inspector Scene; thân panel **cuộn**; `SoundBank` + seam thiết bị âm thanh; `test_scene_workspace` (workspace duy nhất chưa từng có test). |
| `4bdc442` | Sáu mutation sống sót → năm test mới. |
| *(tiếp)* | Xoá 4 lab, `--help` thêm mục "retired (chapter 133)". |

**Core thuần không mở được loa.** `sandbox_core` biên dịch vào ba test không link SDL.
Nên model **không phát** mà **ghi lại**: `World::sounds` là những gì tick vừa yêu cầu
được nghe; `Workspace::take_sounds()` chuyển lên; `studioshell::SoundBank` biến thành
mẫu. Và thiết bị bên dưới nó là một **seam** đúng hình dạng `ui::Context::set_clipboard`
— `main.cpp` nối vào `platform::play_sound`, test headless nối vào không gì cả. Không có
seam đó, cho Studio một cái loa sẽ làm hỏng `test_shell_golden`.

**Ảnh chụp — chương thứ NĂM liên tiếp — ba lỗi:** đèn bán kính 160 px tràn ra toàn nền
editor (giờ clip theo **world**, không phải panel) · **mọi nhãn slider đè lên control
phía trên**, vì `ui::Context::slider` vẽ nhãn *bên ngoài* rect nó nhận — kể cả hai slider
có từ trước · panel trống lúc Play không nói vì sao.

**Và một lỗi ảnh chụp KHÔNG thể thấy.** `begin_scroll` clip **vẽ** chứ không clip
**hit-test**. Control cuộn lên trên viewport vẫn giữ rect sống ở đúng chỗ nó bị đẩy tới:
vô hình vì renderer cắt, bấm được vì hit test không. Cuộn xuống đáy inspector thì nút
Play trong header nằm dưới một bóng ma ăn mất cú click. Đây là **chiều ngược** của lỗi
ch.127 và ch.132 (*vẽ mà không bấm được*) và nó **tệ hơn**, vì trên màn hình không có gì
để nghi ngờ. Sửa 6 dòng trong `point_in` — nơi mọi caller đều được, kể cả danh sách asset
của mục Project vốn cũng cuộn và cũng thủng.

**Harness ăn mất một bản sửa.** Ch.132 ghi "harness sở hữu working tree khi chạy"; đây là
cạnh tiếp theo: tôi khôi phục file đã mutate bằng `git checkout`, và git khôi phục về
**HEAD** — xoá luôn bản sửa chưa commit trong đó. Mutation biến mất, code nó đang kiểm
cũng biến mất, suite xanh cả hai lý do. **Khôi phục bằng copy file.**

**Số liệu:** **78/78 ctest** (77 → 78: thêm `test_scene_workspace`; 50 khi không có Drogon, **đã đo**) · **20/21
mutation**, cái sống sót là *equivalent* (`anim::Flipbook` tự guard `fps > 0` ở cả
`update()` lẫn `frame()`) · golden path xanh, 0 rò `.tmp`, release id **không đổi đúng
như phải không đổi** (`eac0e534`: nội dung không đổi) · web build xanh + cả hai bài kiểm
trình duyệt PASS · hai khung hình đã render và **đã nhìn**.

**Chưa xác minh / trần:** `dir`/`spread` chỉnh bằng slider, gizmo chỉ để **đọc** (chưa
kéo được) · **effect không vào được Archetype**, nên `Spawner` không sinh ra vật phát hạt
(cố ý: proto lồng particle system) · **đường Texture → `frames>1` → FLIPBOOK không có
test** (asset root của test rỗng) · Sound chỉ nghe khi actor **bị huỷ** · hạt chỉ bay khi
Play · deposit đèn vẫn O(radius²)/đèn/khung như lab cũ.

## S25 — một con đường là một con đường, và bản đồ phải nói ra (chương 134) — XONG 2026-09-07

Merge `feat/s25-intgrid-rules`.

**Một dòng, sai chỗ.** `assets/farm/theme.def` ghi `autotile ground 2 path 0`: ô mang
id 2 là đường, đường là bộ 16 mảnh, bộ bắt đầu ở index 0 của sheet `path`. Dòng đó
đúng — nhưng nó nằm trong **file art của một game**. Nên mở đúng bản đồ đó trong Map
workspace (trình sửa map DUY NHẤT từ ch.132) thì editor vẽ ô vuông phẳng, vì nó chưa
bao giờ nghe nói tới `theme.def`. Bạn vẽ đường bằng cách tô ô vuông rồi **thoát editor,
chạy game** mới biết đường trông thế nào.

**Cắt đôi đúng chỗ.** *"Vật liệu này là đường, không phải vùng"* là sự thật về **thế
giới** → vào map (`rule 2 line`). *"Bộ bắt đầu ở index 0 sheet path"* là sự thật về
**art** → ở nguyên theme. Không trùng lặp; renderer cầm cả hai và cộng lại.

| Commit | Việc |
|---|---|
| `1935efb` | `RuleKind`/`Rule` trong `map2`, `rule_piece` + `neighbour_mask`, serialize, và **farm migrate**: xoá `farm::line_piece` và từ khoá `autotile` của theme. |
| `e33775b` | Nút `Rule none/line/blob` trong Map workspace + `mapedit::set_rule` undo được + canvas vẽ **connector**. |
| *(tiếp)* | Sáu mutation sống sót → năm test mới; chương 134 + docs. |

**Một implementation, cuối cùng.** `farm::line_piece` là **bản sao**: chỉ biết line (luật
47 mảnh nằm ở `tilemap/autotile.hpp`, nó không với tới), và nằm ở `games/farm` nơi không
editor nào gọi được. Giờ là `tilemap::rule_piece`, **trả 0 khi giá trị không có rule** —
nên renderer viết `base + rule_piece(...)` **không cần rẽ nhánh**. Nhánh không biến mất,
nó chuyển vào trong hàm, một lần, nơi hai luật vốn đã nằm cạnh nhau.

**Hai quyết định về format:** file được ghi ở **version THẤP NHẤT đủ diễn tả nó** — map
không có rule vẫn là `map2 1`, byte không đổi, release id không đổi vì một tính năng nó
không dùng · rule **trước lưới**, và bị từ chối nếu đứng sau: chúng nói lưới NGHĨA LÀ GÌ.

**Nhìn thấy được.** Map workspace không có tileset renderer (id vẽ thành màu phẳng — thành
thật về những gì editor biết). Nên ô có rule vẽ **kết nối**: một cuống về phía mỗi hàng
xóm nối tiếp, và với vùng thì thêm một chấm ở góc mà mảnh **thật sự** bẻ. Chấm lấy từ
`autotile_canonical` chứ không phải mask thô — một đường chéo mà hai cạnh kề không đỡ thì
không đổi được mảnh, vẽ nó là hứa một khác biệt renderer sẽ không tạo ra.

**Mutation, và cái guard được test một cách tình cờ.** 22 phép, năm sống sót lượt đầu:
· *"rule sau lưới được chấp nhận"* — test dùng map **cao một hàng**, nên khi dòng `rule`
lạc xuất hiện thì lưới đã xong và **parser vòng ngoài** từ chối nó vì lý do khác. Guard
thật sự cần map cao ≥2 hàng. Test mà chủ thể không bao giờ chạy là loại xanh đắt nhất.
· *"ngoài biên thì nối"* — `Map::at` trả 0 ngoài map và 0 không bao giờ mang rule, nên với
mọi ô `rule_piece` hỏi tới thì guard không đổi gì. **Nhưng không thừa**: `neighbour_mask`
là hàm public và hợp đồng của nó nói về ô RỖNG, nơi 0 == 0 sẽ báo khoảng không ngoài rìa
là "cùng vật liệu". Giữ guard, và viết cái test hợp đồng chưa từng có.
· *"brush 0 được mang rule"* — guard chỉ tồn tại **trong widget**. Nút bị disable nên bỏ
kiểm tra trong `cycle_rule` là vô hình — qua nút. Command palette và bàn phím **không đi
qua nút**. D-rule từ ch.111.
· **Hai cái sống sót thuần thị giác** — và câu trả lời không phải thêm một so sánh cả
khung (nó chỉ chứng minh ảnh *đổi*, không chứng minh *đúng*), mà là **ba pixel cụ thể**:
tâm ô không rule là màu của chính nó, tâm ô có rule là mực, góc ô có đường chéo không
được hai cạnh đỡ **không** phải mực.

**Số liệu:** 78/78 ctest · **22/22 mutation** sau khi vá năm cái, baseline sau restore
GREEN · golden path xanh, 0 rò `.tmp`, release id farm đổi đúng như phải đổi
(`d4ad8b0e72d4e530`: map đổi byte) · web build xanh + hai bài kiểm trình duyệt PASS ·
một khung đã render và **đã nhìn** (nó là lý do có ba assertion pixel ở trên).

**Một cổng CI bị flaky, và đã được vá đúng cách nó đáng được vá:** `web_touch_check.mjs`
hỏng 1/5 lần với người chơi không di chuyển, rồi PASS khi chạy lại với **đúng cùng một
giá trị** (`px 4 -> 8`) — là một cú khựng, không phải regression. Giờ nó giữ nút **tối đa
ba lần** và **báo cáo số lần**, nên khẳng định trở thành "giữ nút thì người chơi đi" chứ
không phải "nó đi trong đúng cửa sổ 700 ms này". Nguyên nhân khựng **chưa được chẩn đoán**.

**Chưa xác minh / trần:** **không có art 47 mảnh nào trong repo** — rule `blob` đặt được,
lưu được, xem trước được, `rule_piece` giải được, nhưng **chưa gì vẽ được một bộ vùng**;
vật liệu duy nhất có rule khi ship vẫn là đường (line) của farm · Map workspace vẫn không
có tileset renderer (vẽ kết nối, không vẽ art) · rule theo (layer, value), **các vật liệu
không biết nhau** (không có "cỏ gặp cát") · không override từng mảnh · `iso` và raycaster
không đọc rule · lớp `decor` của farm không có rule nào.

## S26 — một dàn nhân vật, không phải một bức vẽ (chương 135) — XONG 2026-09-07

Merge `feat/s26-mixer`. **Cửa THỨ TƯ vào `.hrt`**, và số cửa trong `CLAUDE.md` đổi **có
chủ ý** — đó là điều kế hoạch yêu cầu, không phải hệ quả.

**Vấn đề:** người chơi trong farm là một hình tròn màu. Anna cũng vậy. Từ ch.124 khi tile
có art thì người vẫn không, vì tile cắt được từ pack còn người thì không — Kenney Tiny
Town là nhà cửa và địa hình, **không có ai sống trong đó**.

Cách sửa hiển nhiên là vẽ hai nhân vật. Chương này làm điều khác, và **khác biệt đó là
toàn bộ luận điểm**.

**Ngưỡng một cửa thứ tư phải vượt qua.** Mỗi cửa là thêm một đường để ảnh vào repo, thêm
một dấu provenance phải nhận ra, thêm một origin trong ledger, thêm một thứ test phải
re-bake. Nên ngưỡng là: **nó trả lời câu hỏi ba cửa kia không trả lời được.**

Ba cửa đầu đều trả lời *bức ảnh này từ đâu ra* — và mỗi cái tạo ra **một** bức ảnh.
`asset.mix` trả lời: **cho tôi một trăm sprite trông như cùng một nơi**. Đó không phải
bản nhỏ hơn của việc vẽ, mà là **đánh đổi ngược lại**: cọ vẽ cho bạn tự do tuyệt đối và,
với một người, một dàn nhân vật không giống nhau — vì tính nhất quán qua sáu mươi sprite
là thứ phải **duy trì bằng tay** và không ai làm nổi. Part lấy đi tự do và trả lại sự
giống nhau miễn phí.

**Không ai vẽ Anna.** Cô là thân + đầu của người chơi, cộng một cái mũ, cộng hai lần đổi
màu. `textures/parts_farm.pix` là toàn bộ ngân sách art cho cả hai: một thân, một đầu,
một mũ — **không cái nào là một nhân vật**.

| Commit | Việc |
|---|---|
| `b5ec4f1` | `mix_core` + `--cmd asset.mix` + `Origin::Mixed` + parts + hai `.mix` + farm **tiêu thụ ngay** (người chơi và Anna hết là hình tròn). |
| `17c5ff4` | Mixer workspace (workspace **thứ tư**) + tab Studio + `--lab mixer` + `test_mix_workspace`. |
| *(tiếp)* | Mutation, chương 135, đổi số cửa trong `CLAUDE.md` có chủ ý. |

**Save và Bake là hai nút vì chúng là hai việc.** Save ghi `.mix` (nguồn, thứ nằm trong
version control); Bake ghi `.hrt` bằng **chính lệnh `asset.mix` CLI chạy**. Một nút sẽ
khiến mỗi phím gõ ghi đè một file nhị phân đã commit, và giấu đi việc file nào là artefact.
Test khẳng định **cả hai nửa**: Save không đụng `.hrt`, Bake khớp CLI **từng byte**.

**Ramp đổi màu là SÁU màu, cố định.** Trông như thiếu tính năng — nó chính là tính năng:
một colour picker với mười sáu triệu đáp án trả lại đúng thứ tự do công cụ này vừa đánh đổi.

**Hai thứ tìm thấy đúng thứ tự.** (1) **Dải part được VẼ và hoàn toàn chết** — rect, phép
hit test, `add_part`, lệnh undo đều có, và **không gì gọi chúng**, vì `update()` chỉ
hit-test phần preview. Ảnh chụp trông hoàn hảo. Đây là chương thứ **tư** liên tiếp cùng
hình dạng này (126, 127, 132), và thứ bắt được nó là một test **bấm vào chỗ bàn tay bấm**
thay vì gọi hàm phía sau nút. (2) **Nền carô là hai màu xám tối, nên nhân vật không có
chân** — quần tối trên nền tối là cùng một bức ảnh với không có gì.

**Và một comment tiên đoán chính thất bại của nó:** `test_shell_golden` bấm tab thứ hai
tại `(2i+1)/2n` với `n` viết cứng là `3`, kèm chú thích *"phân số 3/4 nghĩa là 'cái thứ
hai trong hai', và lặng lẽ thành 'cái thứ ba trong ba' ngày thêm một workspace"*. Workspace
thứ tư đến, và số `3` hỏng **đúng theo cách comment của nó mô tả**. Giờ cả hai chỗ hỏi
`sc.workspace_count()`.

**Chưa xác minh / trần:** **không có slot/anchor/rule** — mix là danh sách part phẳng, không
gì ngăn bạn thêm hai cái đầu · **không frame, không animation** · **không randomiser, không
seed** (bake tất định vì không có ngẫu nhiên, khác với "tái tạo được theo seed"; "Randomize"
là nút Kenney Mixer nổi tiếng nhất và nó **không có ở đây**) · swap khớp **RGB chính xác**,
nên một màu xuất hiện ở hai part sẽ đổi cả hai · **Mixer không tạo được `.mix` mới** (đúng
như Pixel workspace trước ch.131) · offset part chỉ sửa được bằng tay · **chỉ farm tiêu
thụ**, và chỉ hai nhân vật đứng yên.

**Số liệu:** 80/80 ctest (78 → 80: `test_mix` và `test_mix_workspace`; 52 khi không có
Drogon, đã đo) · **19/19 mutation** sau khi vá bốn cái, xoá một guard chết và loại một
mutant tương đương, baseline sau restore GREEN · golden path xanh, 0 rò `.tmp`, release
id farm đổi đúng như phải đổi (`db6959279af1cb80`: hai asset mới) · web build xanh + hai
bài kiểm trình duyệt PASS · ba khung đã render và **đã nhìn** (parts + hai sprite, farm
lúc chơi, và Mixer workspace — khung thứ ba tìm ra nền carô nuốt mất đôi chân).

### S27a — `creature_core`: một trận đánh REPLAY được ✅ 2026-09-07 · chương 136

Merge `feat/s27a-creature-core`. S27 là XL nên chia đôi đúng như `PLAN.md` đã xếp
(ch.119–120 trong bản gốc): **136 = thư viện + 18 loài**, **137 = game**.

**Vì sao Creatures đáng làm, và lý do KHÔNG phải "hai game hơn một game."** Đây là
thứ đầu tiên trong repo buộc phải cho **cùng một kết quả trên hai máy khác nhau**.
Farm không cần: save là ảnh chụp, day roll là cục bộ, hai người chơi có parsnip khác
nhau thì không ai biết. Một trận đánh thì khác ngay khi nó được **xem lại, lưu lại,
hay chia sẻ**: replay chỉ là *bằng chứng* nếu chạy lại ra đúng nó; một lượt PvP qua
mạng là **bốn byte**; và desync phải **bị phát hiện**, nếu không hai người lặng lẽ
đánh xong hai trận khác nhau và một người được báo là thua.

**Ba luật, mỗi luật là một điều thư viện TỪ CHỐI làm:**

1. **Không float trong đường resolve.** Hiệu quả hệ là *phần trăm*, STAB là
   `*150/100`, roll sát thương là `*85..100/100`. Một `float` sẽ đúng trên cả hai
   máy *gần như luôn luôn*, và "gần như" chính là toàn bộ vấn đề.
2. **RNG là STATE, không phải dịch vụ.** `Battle::rng` là một field, nó nằm trong
   hash, và chỉ `step` đẩy nó đi.
3. **Thứ tự lượt suy ra từ state**: priority → speed → một lần tung đồng xu lấy từ
   chính stream của trận. Không bao giờ "side 0 trước" — bản rẻ tiền đó chạy đúng
   cho tới khi side 0 và side 1 là hai cái máy.

`step` trả **event không có chuỗi** (`{kind, side, a, b}`): core thuần phải biên dịch
được vào test headless, và cùng một trận phải kể bằng một ngôn ngữ trên màn hình và
bằng một dòng log ở chỗ khác.

**`engine/rand.hpp`** — vừa là thêm vừa là xoá. Câu "std::mt19937 portable nhưng
distribution của nó thì KHÔNG" đã phải đúng ở nơi thứ ba, nên nó thôi làm một đoạn
comment chép vào từng game. `farm::Rng` giờ là alias; particles giữ xorshift32 riêng
**có chủ ý** (seed theo emitter, không phải đồng ý với ai).

**Bảng hệ chỉ ghi NGOẠI LỆ.** 6×6 = ba mươi sáu số viết ra là ba mươi sáu cơ hội đặt
`200` vào chỗ `50` — và tệ hơn, người đọc không phân biệt được một `100` cố ý với một
`100` bị quên. 18 loài là **sáu dòng tiến hoá ba giai đoạn**, mỗi hệ một dòng, nên
`evolve=` là thứ chịu lực chứ không phải trang trí.

**Không ai vẽ một con nào trong mười tám.** `parts_creature.pix` là **mười hai ô và
không ô nào là một sinh vật**: ba thân, ba mặt, ba mào, ba đuôi. **Tiến hoá = cùng
công thức cộng một part** — và câu đó được *kiểm*, không phải được *tuyên bố*: test
đọc ba file `.mix` của từng dòng và khẳng định mỗi giai đoạn là giai đoạn trước cộng
đúng một part, mọi part cũ còn nguyên, cặp swap không đổi.

| Commit | Việc |
|---|---|
| `e81b2a9` | `creature_core` + `engine/rand.hpp` (farm dùng chung, xoá bản sao) + ba file `.def` + `test_creature` |
| `0c2e089` | `parts_creature.pix` + 18 `.mix`/`.hrt` + **quét re-bake cả ba cửa** trong `test_commands` |
| `6456828` | Mười một lỗ test do mutation lộ ra |

**Sheet đầu tiên VÔ HÌNH.** Thân bắt đầu ở hàng 3, mào ở hàng 1–4 → thân (ghép
**sau cùng**) chôn mất mào: stage 2 và stage 3 của dòng nước ra **giống hệt từng pixel**
stage 1. Mọi file parse được, mọi mix compose được, mọi test xanh, và **claim duy nhất
tấm sheet tồn tại để nói** thì vô hình trong bản render. Sửa bằng một *hợp đồng bố
cục* viết thẳng vào file: mào hàng 0–4, thân hàng 5–15, đuôi cột 11–15.

**Một luật là "quét" cho một cửa và "ba cái tên" cho ba cửa kia.** `CLAUDE.md` nói
mọi nguồn `.hrt` đều được test bake lại và so byte. Đúng với `.pack` import (quét cả
cây từ ch.131). Với ba cửa còn lại thì chỉ đúng theo nghĩa **một `.recipe`, một
`.pix`, một `.mix` được gọi tên bằng tay** — trung thực khi repo có ba nguồn, không
còn trung thực khi một commit thêm mười chín. Giờ `test_commands` quét toàn bộ cây
(24 nguồn), bake lại từng cái, so với `.hrt` đã commit, và **từ chối pass trên danh
sách rỗng**. Đã kiểm hai chiều: lật một byte trong `creature_05.hrt` → đỏ và gọi tên
file. Đây là lần **thứ ba** dự án gặp đúng hình dạng này (ch.128 denylist preload,
ch.131 luật attribution): *luật đúng, kiểm tra phủ một thể hiện của nó, và lỗ hổng
vô hình cho tới khi số lượng tăng.*

**✅ Đã chạy:** `ctest` **81/81** (80 → 81: `creature`; **53** khi không có Drogon, đã
đo) · **32/32 mutation** sau khi vá mười một, baseline sau restore GREEN, nguồn sạch ·
ASan+UBSan xanh trên `test_creature`/`test_commands`/`test_farm` · golden path xanh,
`--project-verify` exit 0, **0 rò `.tmp`** · web build (Emscripten) xanh · **đã nhìn
hai khung render**: bảng 18 loài (bắt được lỗi mào bị chôn) và bản phóng to 12× của
dòng cỏ + dòng thường.

**⚠️ Chưa xác minh:** **chưa có game** — chưa overworld, chưa encounter, chưa manifest,
chưa `--project`; ch.137 là consumer, và điều này chỉ chấp nhận được *vì* nó là slice
ngay kế tiếp (đúng thoả thuận ch.111 đã ghi) · **tất định mới chứng minh trên một
máy** — 1000 trận replay khớp dưới compiler này, RNG có golden sequence, nhưng **chưa
có trận nào replay trên bản web**, mà đó lại chính là nền tảng luật "không float" tồn
tại vì nó · **mọi chiêu đều vật lý** (chưa có tách special/physical) · không stat
stage, không item, không thời tiết, không multi-hit · **ngủ mất luôn lượt tỉnh dậy**
(cố ý, hành vi Gen-1, nhưng chưa ai chơi thử) · AI là *một luật và một tiebreak* ·
bắt được thì kết thúc trận và không gì khác (chưa có box, chưa nhập party) · 18 con
**không animation, không sprite lưng** · `encounters.def`/trainer/gym **chưa tồn tại**.

### S27b — Creatures: game thứ hai, chơi được ✅ 2026-09-07 · chương 137

Merge `feat/s27b-creatures-game`. `--project projects/creatures.gameproject`: một
route có cabin, cỏ cao phục kích, màn battle điều khiển bằng ngón cái, party lên cấp
và tiến hoá, và một file save. Nhưng thứ đáng nói hơn là **game thứ hai đã làm gì với
code đã có**.

**Lời hứa đến hạn.** `CLAUDE.md` và file này đều ghi cùng một câu: *"điều khiển màn
hình mới chỉ có ở farm; nếu game thứ hai cần thì `farm/controls.hpp` sẽ phải tách —
nhưng chưa có người dùng thứ hai, nên chưa tách."* Chương này **là** người dùng thứ
hai, nên hai thứ tách ra:

- **`engine/ui/touch.hpp`** lấy phần là *sự thật về một BÀN TAY*: `kBtn` = 44 px (ngón
  tay ~9 mm; nhỏ hơn thì d-pad thành trò chơi ngắm), luật **tỉ lệ** (điều khiển chiếm
  tối đa nửa chiều rộng, d-pad tối đa hai phần năm chiều cao), phép tính 3×3 của pad,
  và `Box`/`Pointer`.
- **Cái KHÔNG tách là LAYOUT**, và đó mới là luận điểm. Pad của farm nằm trên một
  hotbar bốn ô; pad của game này nằm trên không có gì, còn một phần ba dưới màn hình
  thuộc về menu battle mà farm không có. Chia sẻ những hình chữ nhật đó nghĩa là **một
  màn hình được bố cục theo hàng xóm của màn hình kia**. Luật cả hai game tuân theo —
  *MỘT hàm layout, renderer VÀ hit test cùng đọc* — là một **kỷ luật, không phải một
  hình dạng**.
- **`farm::Theme` → `tilemap::Theme`.** "Ô id này mặc bức ảnh nào" chưa bao giờ là sự
  thật về nghề nông.

**Hai sự thật nữa thuộc về MAP** (luật ch.134, áp dụng thêm hai lần): ô nào **phục
kích** là `ground` id 3; **bảng nào được roll** là một **mask layer** `far` — không
phải `x > 20` viết trong `world.cpp`, cũng không phải một ground id thứ hai, nên hai
vạt cỏ **trông giống hệt nhau** và người chơi phát hiện bằng cách bước vào. Chỗ bắt
đầu là `entity home`, và test khẳng định ô dưới nó đi được — không thì blackout đặt
người chơi vào trong tường và không gì khác nhận ra.

**MỘT dòng ngẫu nhiên.** Roll phục kích của overworld và roll sát thương của trận đánh
ra từ cùng `World::rng`. Hai dòng nghĩa là một save khôi phục trận đánh nhưng không
khôi phục quãng đường dẫn tới nó.

**Lớn lên mà không được chữa lành.** Lên cấp **regrow** chỉ số và **giữ nguyên vết
thương**; tiến hoá cũng vậy. Một cấp mà hồi đầy máu sẽ kết thúc mọi trận đánh ngay khi
có ai đó lên cấp. `Growth` báo loài **đầu tiên và cuối cùng** của cả lần thưởng, vì
"blazehound thành pyrewolf" không phải câu để nói với người chưa từng có blazehound.
**Save không lưu chỉ số** — chúng là hàm thuần của loài và cấp, lưu lại sẽ khiến một
save mâu thuẫn với bảng nó được cân bằng theo.

| Commit | Việc |
|---|---|
| `08546a7` | `engine/ui/touch.hpp` + `tilemap::Theme` — lời hứa đến hạn |
| `a380f28` | `world.cpp` + route + theme + controls + scene + manifest + entry |
| `e4b16b0` | Mười hai lỗ test do mutation lộ ra, ba trong đó **pass vì lý do sai** |
| `a827b75` | Bài kiểm trình duyệt cho game thứ hai — và phát hiện bằng chứng của farm **không chuyển sang được** |

**Ba thứ chỉ ảnh render thấy.** (1) **Mọi nút battle là chữ gần đen trên nền gần đen** —
có mặt, đúng chỗ, không đọc được; `ink() > 0` bảo ổn. (2) **`Back` vẽ đè lên sinh vật
của người chơi** — hai hình chữ nhật quyết định ở hai nơi thì cuối cùng sẽ chồng nhau;
cả hai rect sinh vật giờ nằm trong `Layout`, và test kiểm **mọi CẶP** hình chữ nhật ở
mọi mode. (3) **Chính thước đo tương phản phải làm hai lần** — ngưỡng 1% báo 0 cho mọi
nhãn, vì một chữ khử răng cưa trải trên hàng chục màu gần nhau và không màu nào đạt 1%.

**Một khẳng định về cân bằng, có test:** starter cấp 5 phải **thắng phần lớn** những
gì vạt cỏ gần ném ra. Cả ba starter đạt 75–87%, có thua thật. Một route mà lần gặp đầu
tiên thường kết thúc ván chơi là route không ai đi qua, và không gì khác nói được điều đó.

**Bằng chứng của farm KHÔNG chuyển sang được, và tìm ra điều đó mới là mục đích.** Ở
farm: giữ hướng đông rồi bấm Save. Ở đây: giữ hướng đông là đi vào cỏ cao, có thứ nhảy
ra, và **nút Save biến mất** — đang đánh nhau thì màn hình là một cái menu. Bốn lần
giữ, `pos 4 6` mỗi lần; một probe cuối cùng in ra đúng chuyện đang xảy ra:
`phase=Battle`, `pos=8,6`. Nên game này được chứng minh bằng cách **hoàn thành cái nó
bắt đầu**: đi tới khi màn battle tự khai báo, **Run**, xác nhận, rồi mới save. Đó là
khẳng định *mạnh hơn* của farm.

**✅ Đã chạy:** `ctest` **83/83** (81 → 83: `creature_world`, `creatures_scene`) ·
**38/38 mutation** sau khi vá mười hai, baseline sau restore GREEN · ASan+UBSan sạch ·
golden path xanh trên `creatures.gameproject` (publish `e0d8f431296632cd`, verify exit
0, 0 rò `.tmp`) · web build xanh · **ba bài kiểm trình duyệt PASS**: farm, creatures
(mới), và collection page giờ liệt kê **3 game** · **bốn khung đã render và đã nhìn**
(route, battle, moves, ack) — ba lỗi ở trên đều từ đó.

**⚠️ Chưa xác minh:** **một route** (SPEC muốn 3 town + 2 route + 1 gym) — không
trainer, không gym, không PC box, không item, không shop · **không NPC, không thoại** ·
**battle không animation** — không tween thanh máu, không typewriter · một lượt là một
tap và một câu · **đổi party chưa test với băng ghế thật** (cảnh chưa bao giờ có hơn hai
con) · **tất định vẫn chỉ chứng minh trên một máy** — bản web giờ *chơi* một trận nhưng
**không so hash** · **không cloud save, không BaaS** (đó là S28) · camera không có
chuyển cảnh vào trận · `--bench-ui` vẫn chỉ đo Studio.


### S28a — một sự thật đưa được cho người khác ✅ 2026-09-07 · chương 138

`battle.hpp` mở đầu bằng một câu mà cả header dùng để bảo vệ: *cùng trạng thái đầu và
cùng danh sách hành động sinh ra cùng một trận đánh, trên mọi máy*. Chương 136 kiểm câu
đó một nghìn lần — và **cả nghìn lần đều chạy hai lượt bên trong MỘT tiến trình**. Nó
chứng minh `step` là hàm thuần. Nó không chứng minh gì về *mọi máy*: một hàm thuần vẫn
phải được **tính**, và hai trình biên dịch nhắm hai tập lệnh là hai phép tính khác nhau.

Chương này là chỗ lời khẳng định rời khỏi căn phòng nó được nói ra. Nó rời đi dưới dạng
một **file**.

**Cái động từ không ghi lại được.** Trước hết là một lỗi nằm giữa thanh thiên bạch nhật
từ ch.136. Một replay là trạng thái đầu cộng danh sách hành động — `Move`, `Switch`,
`Run`. Và **ném bóng**, động từ đặc trưng nhất của thể loại, không phải cái nào cả.
`throw_ball` giải quyết cú bắt **bên cạnh** `step`, rồi khi trượt thì gọi `step` với một
**cú đổi sang chính ô đang ra trận** — một hành động hợp lệ mà không làm gì, chọn vì
*tác dụng phụ* của nó. Nó chạy. Nó cũng là một **lời nói dối với bộ giải quyết**, và hệ
quả thì lặng lẽ và tuyệt đối: **không bản ghi nào của game này chứa được một quả bóng.**
Không phải "bóng replay sai" — hành động đó *không có cách biểu diễn*.

Nên bóng thành một hành động: `Action::Kind::Ball` mang phần trăm bonus, `Battle::caught`
nói trận kết thúc thế nào, priority 6 đặt nó trước mọi nước đi. Hai thứ rơi ra:
`settle` **phải kiểm `caught` TRƯỚC `winner`** (bóng dính đặt `winner = 0`, và nhánh dưới
đọc đó là một cú hạ gục — thưởng kinh nghiệm cho một con chưa từng ngất), và **dòng
ngẫu nhiên KHÔNG dịch**: cùng số, cùng thứ tự. Bằng chứng là mẫu cân bằng của
`test_creature_world` — 237, 261, 224 thắng trên 300 — ra **giống hệt đến từng chữ số**.

**Trong file có gì.** `crep1`, ba trường gánh sức nặng:

- **Trạng thái đầu, không phải seed.** Seed chỉ tái tạo được nếu đã biết đội hình — mà
  trong game này đội hình bước vào trận **đã bị thương**. Chỉ số vẫn **suy ra**, đúng luật
  của save: một file lưu `atk` sẽ có thể bất đồng với bảng cân bằng nó được cân.
- **Một hash sau MỖI lượt.** Hash cuối nói *hai bên bất đồng*. Hash từng lượt nói *ở
  đâu* — và một sai lệch bắt ở lượt 4, nơi hai trạng thái khác nhau đúng một chỗ, là lỗi
  đọc được. Cùng sai lệch đó bắt ở lượt 15 là hai trận đánh hoàn toàn khác nhau.
- **Vân tay của LUẬT.** Đây là trường không hiển nhiên. Một replay là sự thật **tương
  đối với bảng số nó được chơi dưới**. Chỉnh power một move và mọi replay đã lưu sẽ lệch
  — đúng, trung thực, và **không phân biệt được** với một cái máy tính sai. Không có
  `rules_hash`, verifier sẽ hô DESYNC ở mọi commit cân bằng, và một verifier kêu sói là
  một verifier người ta tắt đi. Vân tay phủ đúng thứ `step` đọc, **cố ý không** phủ bảng
  encounter hay đường sprite: chuyển một con sang vạt cỏ khác, hay hoạ sĩ vẽ lại nó,
  không được làm hỏng bản ghi một trận đánh với nó.

**`assets/creatures/reference.crep`** là một trận cố định, 26 dòng, **được commit**.
`test_creature` dựng lại từ bảng số và so **BYTE** — đúng chuẩn một `.recipe` phải chịu
từ ch.135 — còn CI chạy test đó trên Ubuntu/x86-64/gcc trong khi file trong repo do
macOS/arm64/clang viết. **Byte vượt qua một tập lệnh.** Và bản wasm chạy cùng lệnh đó
qua `?cmd=` trong job `web-build`, nên **cái đích project này thật sự ship** là thứ ba.

CI cũng chĩa verifier vào một file **phải fail**: một chữ số hex bị lật trong một hash
lượt. Một verifier trả OK cho mọi thứ cũng qua được dòng vui vẻ y như một cái đúng.

**Game tự ghi mà không cần được nhắc.** Một bản ghi mà người chơi phải nhớ tạo là bản
ghi không ai có lúc cần. Chỗ đặt lệnh ghi mới là phần đáng nói: một trận kết thúc theo
**bốn** cách, ở bốn nhánh khác nhau của `update`. Đặt lệnh ghi ở ba trong bốn là một bản
ghi thiếu đúng một kết cục — và cái thiếu là cái người viết code không nghĩ tới. Nên
`update` thành hai dòng bọc quanh thân cũ: **một chỗ hỏi câu hỏi đó**.

**Một danh sách, bốn người đọc.** Bốn file `.def` được viết ra ở `creatures_scene.cpp`,
`test_creature.cpp` và `test_creature_world.cpp` — verifier sẽ là cái thứ tư. (Bản của
`test_creature` **đã trôi**: nó nạp ba trên bốn.) `kDexFiles` + `load_dex` giờ ở
`defs.hpp`, và `load_dex` nhận một **reader** nên core vẫn thuần.

**Mutation: 21/32 lần đầu, 11 sống sót — và một trong số đó là code THỪA.** Guard
`k0 > 3` trong reader không đổi gì khi xoá, vì `legal_action` đã chặn mọi kind lạ; nó
đọc như thắt lưng cộng dây đeo mà chỉ là thắt lưng, nên nó bị xoá chứ không được test.
Mười cái còn lại là lỗ thật, và ba cái đáng kể:

- **"đọc creature nào cũng đầy máu"** sống sót vì **mọi** replay trong test đều bắt đầu
  bằng hai đội hình mới toanh, mà đội mới thì đầy máu. Đúng cái ca game thật luôn ở:
  đội hình bước vào cỏ mang theo thứ trận trước để lại.
- **"bóng không có priority"** sống sót vì mọi test đều ghép một người ném NHANH với một
  mục tiêu chậm — priority 0 sinh ra y hệt trận đánh. Chỉ một trận **đang thua** mới
  phân biệt được: starter cấp 3 trước một con cấp 40, nơi nếu bóng không đi trước thì
  `act` gặp người ném đã ngất và **cú ném không hề xảy ra**.
- **"side khai báo hai lần"** sống sót vì ca test nối thêm một `side 0 3 0` trống, và nó
  bị từ chối vì **thiếu ba con** — một ca kiểm nhầm guard, và đọc y hệt một ca kiểm đúng.

**Và một cái sống sót KHÔNG phải vì thiếu test.** `p.count >= want[side]` chặn con thứ
bảy được ghi vào một đội sáu. Xoá nó đi thì file **vẫn bị từ chối** — kiểm đếm ở cuối
bắt được — nhưng chỉ *sau khi* `member[6]` đã đè lên `count`, `active` và tràn sang phe
kia. Hành vi quan sát được y hệt; hỏng là **trong cùng một object**, nên không có
redzone và **ASan cũng không thấy** (đã kiểm: bản đột biến chạy sạch dưới ASan+UBSan).
Không assertion nào trong bộ này phân biệt được hai bản. Nó được giữ nguyên, kèm một
fixture ghi lại ý định và một ghi chú nói rõ vì sao fixture đó không thể fail. Đây là
**mutation đầu tiên của project là một lỗi thật mà không test nào bắt được** — và viết
một assertion pass vì lý do thứ ba để giả vờ đã bắt được thì tệ hơn là nói thẳng.

Chốt **42/43** cả hai vòng, baseline sau restore GREEN cả hai lần.

**⚠️ Chưa xác minh:** **ba toolchain vẫn không phải "mọi máy"** —
không Windows/MSVC, không 32-bit, không big-endian · **MỘT trận tham chiếu**, 15 lượt:
có Move/Switch/Ball, **không có `Run`**, không có ngủ, không có stall trăm lượt (Run và
một trận bắt đầu **đã bị thương** được phủ bằng test dựng tay, không phải bằng file
commit) · **chưa gửi bản ghi đi đâu cả** — BaaS có replay store từ ch.100 và game không
đụng vào (đó là S28b, và là lý do format có hash từng lượt) · **tape không nằm trong
save**: `to_text` không lưu được trận đánh, nên không có trạng thái nào mà nửa bản ghi
bị mắc kẹt — đó là một **giới hạn mặc áo đơn giản hoá**, và đáng nói rõ nó là cái nào.


### S28b — bốn byte một lượt ✅ 2026-09-07 · chương 139

Chương 136 dựng một trận đánh là số nguyên từ đầu tới cuối. Chương 138 biến bản ghi
của nó thành một file ba toolchain đồng ý. Cả hai đang trả lời một câu hỏi chưa ai
hỏi — và header đã nói thẳng ra: *"…thứ cho phép hai người chơi đánh nhau qua mạng
bằng cách trao đổi bốn byte một lượt thay vì một world state"*. Đây là chương có
người hỏi.

**Hai bên gửi cho nhau cái gì.** Không phải thanh máu. Không phải con số sát thương.
Mỗi bên gửi **hành động mình chọn** — một kind và một index — rồi cả hai tự tính cả
lượt. Sau đó cả hai gửi `hash(battle)` và so. Đo được: **34 byte một lượt**, trong đó
16 byte là cái hash — *kiểm tra* lượt đấu tốn gấp bốn lần *chơi* nó, và vẫn là không
đáng kể. Một giao thức trao đổi **kết quả** thì buộc phải tin kết quả đó. Cái này
không cần tin, vì nó biết khi nào hai bên khác nhau.

**Ba thứ người chơi KHÔNG được chọn.** *Mình là phe nào, và seed* — cả hai đến từ sự
kiện `matched` của server. Phương án "mỗi client góp một nửa seed rồi trộn" nghe công
bằng hơn nhưng tệ hơn: **ai gửi sau có thể mài nửa của mình** cho tới khi kết quả trộn
vừa ý; sửa cho đúng phải commit-reveal. Server không có lợi ích trong trận đấu, nên
server chọn. Seed đi dưới dạng **16 ký tự hex, không phải số JSON** — số JSON là double
trong mọi trình duyệt, mà bit thấp của một seed 64-bit chính là toàn bộ vấn đề. *Và
sinh vật của mình là gì*: dây gửi `species:level`, hai bên cùng gọi `make` — nên một
peer **nói dối được nó mang con nào, không nói dối được con đó là gì**. Đúng luật của
save và của replay, lần thứ ba.

**Bắt desync, từ CẢ HAI phía.** Bài test đáng nói không phải một nghìn trận khớp nhau,
mà **bốn mươi trận mà một peer cố tình chạy bản có một move mạnh hơn** — tức là một
client cũ, trường hợp bình thường của một game đã phát hành. **40/40 lệch, 40/40 được
CẢ HAI bên bắt, ở CÙNG một lượt**, và không bên nào tuyên bố người thắng. Cả hai cùng
bắt mới là điểm mấu chốt: một giao thức mà chỉ nạn nhân nhận ra là một giao thức mà
người kia tiếp tục chơi một ván đã kết thúc. Và một trận desync **không báo gì lên
ladder** — không bên nào biết chuyện gì xảy ra sau lượt hai bên thôi đồng ý.

**Bảng xếp hạng giữ điểm CAO NHẤT thì không chứa được rating.** Elo **đi xuống**. Một
bảng lặng lẽ từ chối hạ nó biến ladder thành sổ ghi ngày đẹp nhất của mọi người. Một
cột `mode` (`'best'` mặc định, nên mọi bảng đã ship không đổi) + migration 9.

**Và một ladder mà client chọn CON SỐ thì không phải ladder.** Project này có luật
chống giả mạo ở khắp nơi — *điểm thuộc về user của JWT, không bao giờ là một field
trong body* — và luật đó vô nghĩa trên ladder nếu **giá trị** vẫn là một field trong
body. Nên ladder có verb riêng: `POST /v1/leaderboards/{key}/match` nhận **kết quả**,
server tự tính. Nghĩa là server cần đúng cái Elo client dùng để dự đoán → `baas` giờ
có `src/` trên include path cho **một** header `constexpr` thuần. Đó là **ngoại lệ có
chủ ý** của "backend không link engine code", và là ngoại lệ hẹp nhất có thể: không
link gì cả, chia sẻ một header, và **lý do chính là lý do bảng số đó tồn tại** — một
rating tính từ hai bản sao của một đường cong chính là cái bug nó sinh ra để chặn.

Cả hai người chơi đều báo cáo, vì cả hai đều đã chơi. Đó không phải retry, đó là
trường hợp bình thường — nên `match` id làm nó idempotent, và **kho idempotency
chuyển sang `baas/common/`** theo đúng ghi chú nằm trong `inv_service.cc` từ ch.102:
*"khi có endpoint THỨ HAI cần idempotency thì hãy chuyển"*. Endpoint thứ hai đã đến.
Đây là lời hứa thứ ba project trả đúng điều kiện đã ghi (ch.137 trả hai).

Người báo cáo thứ hai nhận `delta: 0`, có chủ ý: delta đã lưu thuộc về người báo
trước, và đưa nó cho người thứ hai đã **nói với kẻ thua trận PvP thật đầu tiên rằng
họ được cộng 16 điểm**. Một con số đúng với người khác còn tệ hơn không có số.

**Cũng không phải ladder nếu client chọn ĐỐI THỦ.** Một client không giả được rating
vẫn báo cáo được bốn mươi trận thắng người đứng đầu. Chỉ server biết nó ghép ai với
ai, nên hub **nhớ 4096 cặp gần nhất**, và endpoint trả `403` nếu trận đó không phải
do nó ghép. Giá phải trả được ghi rõ: `/match` chỉ dùng được cho game dùng matchmaking
của hub, và chỉ tới lần restart kế tiếp — cả hai vốn đã đúng với chính cái hub.

## Cái bug hai TIẾN TRÌNH tìm ra mà một tiến trình không thể

Mọi thứ trên đều xanh. `test_netbattle` chơi một nghìn trận; `test_creature_pvp_live`
chơi một trận qua WebSocket thật với Drogon thật rồi kiểm ladder. Rồi hai tiến trình
`--pvp` được chĩa vào một backend đang chạy, và **trận đấu chạy năm trăm lượt và vẫn
đang chạy**.

Trace nói hết: `recv <party 1 1:20 5:18 9:22>` / `send <party 1 1:20 5:18 9:22>` — hai
đội **giống hệt nhau**, vì client headless có một đội cố định. **Mọi test trong repo
đều dựng đội hình từ loài bốc ngẫu nhiên, nên chưa test nào từng cho hai đội GIỐNG
HỆT nhau vào một trận.** Năm trăm lượt sau, mọi con của cả hai bên hết PP; không gì
gây được sát thương; và nước cuối cùng của `choose` là *"đổi sang con còn sống đầu
tiên"* — bất kể PP của nó. Nên hai bên xoay băng ghế vào nhau, mãi mãi, mỗi bên đều
đang đi nước duy nhất nó có.

Không nửa nào của bản vá là phần thú vị; **cặp** mới là. `choose` thôi đổi sang một
con cũng không hành động được, và — **riêng biệt** — một trận chạm 200 lượt là **HÒA**.
Cái thứ hai không phải van an toàn bắt bên ngoài: nó là **luật của trò chơi**, trong
`battle.hpp`, nên game hoang dã, một replay đã lưu và một trận xếp hạng cùng được bảo
vệ. 200 vượt xa mọi trận thật (dài nhất trong một nghìn trận là 28), nên không bản ghi
đã commit nào dịch, và `test_creature` bake lại `reference.crep` từng byte để chứng
minh. Với AI đã sửa, một trận gương giờ **phân thắng bại trong 28 lượt** chứ không
chỉ dừng ở cap — nên hai guard được test **tách nhau**.

**Ba chương liên tiếp một bằng chứng không chuyển được, và ba lần việc phát hiện ra
điều đó đáng giá hơn chính bằng chứng.** ch.137: bằng chứng chạm của farm không sống
sót khi gặp game thứ hai. ch.138: tất định trong một tiến trình không nói gì về hai
toolchain. Ở đây: một tiến trình chơi cả hai phe chưa bao giờ đưa cho chúng cùng một
đội.

**Mutation: 29/32 vòng đầu — và một cái sống sót CHỨNG MINH một comment là SAI.**
`elo_update` làm tròn ra xa số 0, và comment phía trên nói đó là thứ giữ cho trận đấu
zero-sum. Đổi thành phép chia nguyên thường **sống sót mọi test trong `test_elo`** — vì
phép cắt của C++ **đối xứng**: tử số của người thắng và người thua là số đối chính xác
dù làm tròn kiểu nào. Cái làm tròn thật sự mua được là **độ lớn**: cắt luôn làm tròn
phần được xuống và phần mất lên, nên một kết quả sít sao đáng 15 trong khi số học nói
16, ở khoảng một nửa số ván.

Đó là một hình dạng đáng đặt tên riêng: **không phải thiếu test, cũng không phải sai
code — mà là một dòng ĐÚNG được bảo vệ bằng một lập luận SAI.** Nó qua review y hệt một
lập luận đúng, và sẽ bị xoá ngay lần đầu có ai đó "đơn giản hoá".

Hai cái còn lại: **một guard chưa từng được kiểm ở nửa kia** (bỏ `alive()` trong vòng
quét băng ghế mới sống sót, vì trong ca no-PP mọi con đều còn sống — mà một con **ngất
vẫn giữ PP**, nên nó sẽ được chọn rồi `act` từ chối và phí lượt), và **một khả năng
không ai dùng** (`begin()` reset object, mà mọi test đều dựng `NetBattle` mới — trong
khi client nào chơi ván thứ hai đều dùng lại một cái).

Chốt **35/35** cả hai vòng, baseline sau restore GREEN cả hai lần.


**⚠️ Chưa xác minh:** **game KHÔNG có PvP** — `--pvp` là client headless chơi bằng
`choose`; màn battle không có lobby, không có nút "tìm trận" · **không có trọng tài**:
server gán phe/seed/cặp đấu và chỉ chấm trận nó ghép, nhưng **không replay lại trận**,
nên hai client bị sửa giống nhau vẫn đồng ý với nhau (tape đã lưu là thứ khiến điều đó
**kiểm được sau**, và chưa có gì tự động kiểm) · **không reconnect, không timeout,
không đầu hàng** · hub vẫn **single-node, in-memory** — phòng, hàng đợi, và giờ cả sổ
ghép cặp · **K cố định 32** cho tất cả, không sàn, không placement, không decay.


### S29a — cái pool CHÍNH LÀ cái khoá ✅ 2026-09-07 · chương 140

Có một comment trong `inv_service.cc`, viết cẩn thận, và **đúng**: *"phép kiểm tiền
SELECT-rồi-UPDATE chỉ nguyên tử vì pool của SQLite bằng 1, nên `newTransaction()` giữ
kết nối duy nhất… Trên bản Postgres với pool > 1, TOCTOU sẽ mở lại."* Slice này lẽ ra
để trả cái comment đó. Thực tế: comment **đúng về `purchase` và sai về chính file nó
được viết trong**, còn bản Postgres mà nó cảnh báo thì **không tồn tại**.

**Hai chỗ pool CHƯA BAO GIỜ là cái khoá.** `purchase` mở transaction nên trên SQLite
nó thật sự giữ kết nối duy nhất suốt cả đoạn đọc-rồi-ghi. `grant` và `consume` thì
không: **pool bằng 1 phát kết nối cho MỘT CÂU LỆNH, không phải cho một chuỗi**. Hai
luồng gọi `execSqlSync` hai lần mỗi bên xen kẽ nhau trên pool 1 y hệt như trên pool 10.

Và nó tệ hơn "mất một lần cập nhật". `test_baas_concurrency` chạy tám luồng cùng
`grant` 1 món mà người chơi **chưa có**, và bản code cũ làm thế này:

```
libc++abi: terminating due to uncaught exception of type drogon::orm::UniqueViolation
```

Cả hai luồng đọc "không có dòng nào", cả hai đi nhánh INSERT, cái thứ hai vi phạm
unique index. **Không ai bắt.** `inv::grant` gọi được từ `POST /v1/inventory/grant`,
nên **hai request đồng thời giết chết tiến trình server** — không phải số dư sai, mà
là crash. Nằm đó từ chương 102.

**Một dòng SQL.** `db::lock_clause()` trả `" FOR UPDATE"` trên Postgres và `""` trên
SQLite — khác biệt cú pháp *duy nhất* giữa hai backend trong toàn bộ codebase, và là
cái quyết định tiền có tiêu được hai lần hay không. `""` trên SQLite **không phải "cùng
thứ nhưng vô hại"**: SQLite không có `FOR UPDATE` và trả về lỗi cú pháp — nên một
mutation đảo dòng này bị giết bởi một bộ test chỉ chạy trên SQLite.

**Một bài test không có răng, và nói thẳng ra.** `test_baas_concurrency` **không thể
fail trên SQLite** vì lý do mà các khoá tồn tại. Header của nó ghi rõ điều đó, và ghi
luôn cách làm cho nó có răng. Cái nó *thật sự* chứng minh trên SQLite là các transaction
mới **không deadlock** — và nó xứng đáng nói câu đó, vì bản đầu tiên của chúng deadlock.

**Hai mươi lăm phút im lặng.** `lb::submit` tính rank *sau khi* ghi, mà transaction vẫn
còn trong scope. Trên SQLite transaction giữ kết nối duy nhất, nên `rank_for_value` chờ
một handle không thể được trả về cho tới khi hàm return. Bộ test **không fail. Nó
DỪNG.** Timeout mặc định của ctest là 1500 giây. Bản vá là một cặp ngoặc; bài học là
`TIMEOUT 120` cho mọi test trong thư mục baas.

## Đường Postgres chưa bao giờ tồn tại

`baas/ops/pg-test.sh` dựng nó bằng Docker: `postgres:16-alpine`, và build trong image
`drogonframework/drogon` (image này **có** libpq, bottle của Homebrew thì không). Nó
build được. Rồi câu lệnh **đầu tiên** chết:

```
ERROR:  syntax error at or near ","
LINE 1: INSERT INTO schema_migrations(version, name) VALUES(?,?)
```

**Drogon không dịch `?` thành `$1`.** Cả **107** truy vấn trong `baas/` viết bằng `?`.
Dưới đó còn hai tầng nữa: `id INTEGER PRIMARY KEY` là auto-increment ở SQLite và là một
cột số bình thường ở Postgres, và mười lời gọi `insertId()` cần `RETURNING`.

Nên dòng *"Postgres là một bản build lúc deploy, đã được ghi lại"* trong `db.h` là **một
CÂU VĂN, không phải một khả năng**. Không có gì trong lịch sử backend này từng thực thi
một câu lệnh với nó, và sẽ không có gì phát hiện ra cho tới lúc deploy.

Phản ứng trung thực **không phải** là lặng lẽ mở rộng slice. Là để script lại trong repo
như một **bản tái hiện**: một lệnh ai cũng chạy được và fail đúng lỗi đó, và một `❌`
trong sổ xác minh ở chỗ trước đây là `⚠️`. CI **chưa** chạy nó, có chủ ý — một job đỏ mà
không ai sửa được thì không dạy được gì. Làm nó xanh là một slice có tên riêng.

## Một bộ test chỉ pass được MỘT lần

`db_url(name)` bản đầu nối thêm `.db` vào một cái tên **đã có** `.db`. Mọi test ghi vào
`test_baas_auth.db.db` trong khi `cleanup_db` xoá `test_baas_auth.db`. Lần chạy đầu:
88/88 xanh, vì file mới tinh. Lần thứ hai: fail mười chỗ, trên những database chưa từng
bị dọn. **Một bộ test xanh không phải bằng chứng nếu nó chưa xanh hai lần** — và lần
chạy bắt được nó là lần thứ hai, chỉ vì mutation harness bắt đầu bằng một baseline.

## Mutation: 14/15

Nhiều cái chết dưới dạng **lỗi BUILD** hoặc **lỗi cú pháp từ chính SQLite**, không phải
assertion fail — thay `newTransaction()` bằng `client()` thì không compile, đảo
`lock_clause()` thì SQLite từ chối mọi câu đọc có khoá. Một seam mỏng như vậy sai là kêu
to, và đó là phần lớn lý do nó đáng có.

**Một cái sống sót, và ở đây KHÔNG THỂ giết được.** `apply_match` khoá hai dòng theo
thứ tự id nhỏ trước, để hai transaction lấy cùng một cặp không thể deadlock. Đổi thành
thứ tự đến thì **cả bộ test vẫn xanh**, vì SQLite không có row lock nên không có
deadlock nào để mà có. Phản ứng **không phải** viết một assertion pass vì lý do thứ ba:
quyết định được **đưa ra ngoài** thành `lb::lock_order(a,b)`, một hàm thuần của hai con
số mà test đọc được **GIÁ TRỊ** — `lock_order(a,b) == lock_order(b,a)` với mọi cặp, và
nó không phải hàm đồng nhất. Mutation trên *hàm* bị giết; mutation trên *chỗ dùng* vẫn
sống, và sẽ sống cho tới khi có Postgres thật.

Đây là lần thứ tư project làm đúng một nước đi này: **khi một tính chất chỉ quan sát
được ở nơi không với tới, hãy đem quyết định ra khỏi nơi đó và kiểm giá trị của nó ở
chỗ với tới được.**

**⚠️ Chưa xác minh:** **Postgres KHÔNG chạy** (không phải "chưa test") — nên các khoá
mới đúng về hình dạng và **chưa từng được thử ở tình huống cần chúng** · thứ tự khoá
trong `apply_match` là **lập luận, không phải bằng chứng** · các đường đọc-rồi-ghi khác
(cloud save, asset registry, hàng đợi test-run) **chưa rà** · **`/healthz` chưa có gì
chọc vào** và **chưa có OpenAPI** — nửa còn lại của slice OPS chưa động tới.


### S29b — bốn lần chạy một cái script ✅ 2026-09-07 · chương 141

Chương 140 kết thúc bằng một script fail có chủ ý — câu lệnh ĐẦU TIÊN mà repo này từng
gửi tới một PostgreSQL thật đã chết vì cú pháp của chính nó. Slice này là **bốn lần
chạy** cái script đó, mỗi lần tìm ra một thứ khác. Phần đáng chú ý không phải các bản
vá — hầu hết ba dòng. Mà là: **cả bốn lỗi đều vô hình trên SQLite, và cả bốn đều nằm
trong code CÓ test.** Bộ test xanh xuyên suốt.

**Lần 1 — `?` không phải placeholder.** Drogon không dịch nó. Cả 107 truy vấn dùng `?`.
Câu trả lời phải là **một** bản dịch, ở **một** chỗ, mà không gì đi vòng được:
`db::portable(sql, dialect)` — thuần và toàn phần, nên `test_baas_sql_seam` đọc **GIÁ
TRỊ** của nó mà không cần database nào ở gần. Bốn phép viết lại, mỗi cái là một bất
đồng không call site nào tự sửa được: `?`→`$n`; `AUTOID` (SQLite viết `INTEGER PRIMARY
KEY`, Postgres cần `GENERATED BY DEFAULT AS IDENTITY` nói thành lời); `CURRENT_TIMESTAMP`
(mọi cột thời gian ở đây là TEXT, và Postgres **không** đặt một `timestamptz` vào đó —
không ở DEFAULT, không trong UPDATE, không ở hai vế một phép so sánh); `BYTELEN(x)`.

`AUTOID` là **token** chứ không phải khớp mẫu `INTEGER PRIMARY KEY`, vì
`schema_migrations.version` là một `INTEGER PRIMARY KEY` **không** phải khoá thay thế —
số hiệu phiên bản do người gọi đưa — và một bộ viết lại đủ khôn để đoán sẽ sai đúng chỗ đó.

Rồi cái seam: `db::exec` là **một cánh cửa**, `db::insert_id` là **một chỗ** id mới sinh
ra. 104 call site chuyển sang, 8 `insertId()` chuyển sang. Và thứ biến nó từ quy ước
thành seam: nửa sau của `test_baas_sql_seam` **đọc cây mã nguồn** và fail nếu
`execSqlSync` xuất hiện ở đâu khác. Một **DÒNG** được miễn bằng `// raw-sql: <lý do>`,
một **FILE** thì không. Chín dòng trong cả cây, được in ra và **đếm**.

Cái scan trả công ngay lập tức: một nửa số test **INSERT chính cái project chúng sẽ nói
chuyện**, và một fixture viết bằng `?` fail trên Postgres to y hệt một service.

**Lần 2 — một tham số CÓ độ rộng.** Năm mươi tám lần
`ERROR: incorrect binary data format in bind parameter 1`. Drogon bind số nguyên ở dạng
**nhị phân**, `sizeof(T)` byte, không có type OID; Postgres suy ra kiểu từ cột rồi đòi
payload đúng độ rộng đó. Mọi cột id ở đây là `INTEGER` (4 byte), mọi id trong code là
`long` (8). Cách sửa hấp dẫn là **làm cho các độ rộng khớp nhau** — đó là một luật không
có test, giữ qua một trăm call site, fail lúc chạy trên đúng cái backend không ai dev
trên đó. **Một tham số dạng TEXT không có độ rộng.**

Cùng lần chạy đó: `length(CAST(data AS BLOB))` là tiếng SQLite cho "bao nhiêu byte"
(`length()` ở đó đếm KÝ TỰ), còn Postgres **không có** kiểu BLOB. Và sáu test mở đầu
bằng `const long pid = 1;   // no FK enforcement needed for this unit test` — câu đó
không phải một quyết định thiết kế, nó là **mô tả mặc định của SQLite**: `PRAGMA
foreign_keys` **tắt**. Postgres bắt buộc ràng buộc mà schema đã yêu cầu, và sáu test đó
**không chèn nổi một dòng nào**. Một năm xanh trên dữ liệu mà không deployment thật nào
có thể chứa.

**Lần 3 — một cái khoá không giữ được dòng KHÔNG TỒN TẠI.** Chương 140 đặt transaction
+ locking read trước mọi đường đọc-rồi-ghi và tưởng thế là xong. `FOR UPDATE` khoá một
**DÒNG**; lần ghi **đầu tiên** vào một khoá thì không có dòng nào: hai luồng cùng đọc
rỗng, cùng INSERT, một cái thua unique constraint. **Chương 140 đóng lần ghi thứ hai và
để ngỏ lần thứ nhất** — và cái test nó viết ra để chứng minh bản vá cũng không thấy,
vì pool-1 của SQLite tuần tự hoá cặp đó.

Idiom: **materialise, rồi lock** (`db::ensure_row`, `ON CONFLICT DO NOTHING` không nêu
target = "bất kỳ xung đột nào", chạy trên cả hai). Hai nhánh biến mất theo: `put_qty`
không còn cờ `exists`, `store_rating` không còn nhánh INSERT.

Rồi đi tìm cùng hình dạng đó ở chỗ khác, và thấy **bốn** cái nữa — `cfg::set`,
`store::upsert`, `save::put`, `asset::put` — đọc-rồi-ghi, **không cái nào có transaction
gì cả**. Ở đó kẻ thua cuộc không phải một request hỏng, mà là một exception không ai bắt
trên event-loop thread của Drogon, tức là **tiến trình server**, từ
`POST /v1/saves/{slot}`.

**Lần 4 — pool-1 CŨNG LÀ một memory barrier.** Không còn lỗi SQL nào. **Mọi con số đều
sai.** `grant` trả về 8 và cái `GET` ngay sau nó trả về 7; 48 lần grant 1 ra 47. Không
phải mất cập nhật — mà là **một lần ghi chưa hạ cánh**. Drogon commit trong destructor
và **XẾP HÀNG** cái COMMIT lên event loop của kết nối đó. Pool 1: câu lệnh kế tiếp xếp
sau nó, trên cùng một kết nối, nên luôn thấy. Pool thật: câu kế tiếp lấy **kết nối
khác** và đọc trạng thái trước commit. Đó là thứ **thứ ba** mà pool-1 âm thầm cung cấp —
sau tính nguyên tử (ch.140) và một dòng để cái khoá giữ (lần 3): **đọc-thấy-cái-mình-vừa-ghi**.
`db::Transaction` đợi commit báo về, **có chặn 10 giây** (chương 140 đã tiêu 25 phút
trong một cái đợi không bao giờ xong và không nói gì); `rollback()` **không** đợi.

**30/30 trên Postgres. 89/89 trên SQLite.**

## Mutation: 27, giết 27 — sau khi hai cái sống sót chỉ ra hai lỗ thật

Hai mươi mốt trên SQLite, sáu **trong container** trên Postgres. Hai cái sống sót ở lượt
đầu, và cả hai đều thật:

**Một phép kiểm "nguyên từ" viết SAI CHỮ HOA-THƯỜNG.** `portable` khớp `AUTOID` và
`CURRENT_TIMESTAMP` **phân biệt hoa thường**, còn cái test đáng lẽ chứng minh nó chỉ
viết lại nguyên từ thì viết `autoidx`, `my_autoid` — **chữ thường**. Bộ khớp chưa bao
giờ nhìn tới chúng lần thứ hai, nên xoá phép kiểm biên phải khỏi `word_at` không đổi gì
mà test thấy được. Assertion đó **đã pass suốt cho một tính chất nó không hề chạy qua**.

**Một lần ghi BỊ TỪ CHỐI vẫn giữ lại cái dòng nó vừa tạo.** `save::put` và `asset::put`
tạo dòng **trước** khi kiểm `If-Match`, nên một cái 409 quên rollback sẽ xuất bản một
save rỗng dưới một slot chưa từng tồn tại. Cả hai đều rollback; **không cái nào có test
để ý**. Giờ thì có: một `If-Match` vào một tên **không tồn tại** phải 409 **và** phải để
lại một 404.

**⚠️ Chưa xác minh:** CI chạy Postgres **chưa từng chạy trên runner của GitHub** —
YAML viết theo đúng khuôn `baas-test` đã xanh và service container đã kiểm bằng cùng
image ở local, nhưng nó chỉ đúng khi push · **`--seed`/`--static` và server thật chưa
boot lần nào trên Postgres**; chỉ có bộ test chạy · thứ tự khoá trong `apply_match` vẫn
là **lập luận** (một deadlock cần hai transaction lấy cùng cặp ngược chiều — chưa dựng
được) · `/healthz` vẫn chưa ai chọc, **vẫn chưa có OpenAPI** · pool để mặc định 4, chưa
đo gì · `idem::lookup`-rồi-`record_with` vẫn còn khe hở mỏng dưới hai lần dùng ĐẦU TIÊN
đồng thời của cùng một key (ghi trong `idempotency.h` từ ch.102, chưa đóng).


### S29c — cái image chưa bao giờ trả lời ✅ 2026-09-07 · chương 142

Slice này có hai phần: một mô tả OpenAPI cho `/v1` **không thể trôi** so với router, và
một job CI dựng image backend rồi chọc `/healthz`. Phần một đi đúng như thiết kế. Phần
hai phát hiện **cái image chưa từng phục vụ một request nào trong đời**.

**Một mô tả sinh ra từ chính cái bảng đang phục vụ.** Một spec gõ tay bên cạnh một
router gõ tay là hai cái bảng khớp nhau đúng ngày viết ra và không bao giờ khớp lại —
project này đã xem chuyện đó xảy ra hai lần (ledger attribution ở ch.131, đường Postgres
ở ch.141). Nên ràng buộc thiết kế là: **không được phép để document và server bất đồng.**

`baas/openapi/spec.cc` là **một bảng**, một dòng một operation. Trong dòng đó chỉ có thứ
router **không thể biết**: endpoint để làm gì, chìa nào mở, và các câu trả lời nghĩa là
gì. Mọi thứ khác đều **suy ra** — tham số từ `{…}` trong path, `operationId` từ method +
path, danh sách tag từ lần xuất hiện đầu. *Một thứ đã viết xuống một lần thì không được
có bản sao thứ hai để mà quên.*

Rồi hai test, đóng hai khe khác nhau: **so với ROUTER** (`getHandlersInfo()` là bảng
route server thật sự dựng; so hai chiều — thêm route không thêm dòng là ĐỎ, xoá route mà
để lại dòng cũng ĐỎ) và **so với FILE** (`baas/openapi.json` được commit, nướng lại phải
ra **đúng byte** — chuẩn mà `.recipe`, `collection.json`, `reference.crep` đã chịu).

Hai chuyện nhỏ rơi ra từ cách làm đó. `/dashboard` từng đăng ký trong `main.cc`, nghĩa là
`register_routes()` *gần như* là toàn bộ bảng route — và "gần như" là một ngoại lệ, mà
ngoại lệ là chỗ cái route không-tài-liệu tiếp theo sẽ chui vào. Nó chuyển chỗ, đường dẫn
file thành một field của `AppConfig`, và **không còn gì để loại trừ**. Còn WebSocket:
Drogon liệt `/v1/ws` **một lần cho mỗi HTTP method** — mười một dòng cho một route. Bản
test đầu tiên hard-code `GET /v1/ws` làm ngoại lệ; đó là một cái TÊN nằm trong test, và
tên trong test thì mục. Chính Drogon ghi mô tả `WebsocketController: …`, nên cái LUẬT
chuyển vào `live_routes()`. Test **không còn ngoại lệ nào**. 51 route đăng ký, 51 tài liệu.

**`--seed` trả về 0.** `baas/ops/Dockerfile` kết thúc bằng `CMD [..., "--seed"]` từ chương
107, và `main.cc` thì `if (do_seed) { seed(db); printf(...); return 0; }`. **Container
seed xong rồi thoát. Mọi lần.** `docker-compose.yml` đặt `restart: unless-stopped` trước
mặt nó, nên thứ đã ship là một vòng lặp: seed → thoát 0 → khởi động lại → seed → thoát 0
— và **chưa bao giờ lắng nghe**. Cái healthcheck ngay bên cạnh, `curl … /healthz`, không
đời nào pass được. Chạy thật trên image trước khi sửa:

```
running: false  exit: 0
curl: (7) Failed to connect to 127.0.0.1 port 18101
```

Cái cờ mang **hai ý định dưới một cái tên**. Người ngồi trước terminal gõ `--seed` để in
key rồi lấy lại dấu nhắc; một container truyền `--seed` với nghĩa "bảo đảm project demo
tồn tại **trước khi phục vụ**". Giờ là hai cờ: `--seed` seed rồi chạy tiếp, `--seed-only`
seed rồi dừng. Và một cái nhỏ hơn, phát hiện khi đọc `docker logs` mà thấy **trống trơn**:
`stdout` bị đệm theo khối khi nó là pipe, nên dòng duy nhất operator cần — key của project
demo — nằm trong buffer suốt thời gian server chạy, tức là mãi mãi. Một `fflush`.

**Vì sao không test nào bắt được.** Mọi test trong bộ này link `baas_core` rồi **tự gọi**
`register_routes()`. Cách đó phủ toàn bộ server **trừ đúng cái file quyết định server LÀM
GÌ lúc khởi động** — và lỗi nằm trong `main()`. Chín mươi test, và **chưa cái nào từng
chạy `main()`**. Nên `test_baas_boot` chạy: `posix_spawn` chính cái binary, cổng trống,
stdout ra file, rồi hỏi nó từ **bên ngoài** — `--seed` phải **VẪN CÒN SỐNG** sau khi
`/healthz` trả lời, phải đã log key *trong lúc đang chạy*, một guest phải đăng nhập được,
và `/openapi.json` nó phục vụ phải trùng byte với file đã commit; `--seed-only` phải thoát
0; `--openapi FILE` phải ghi ra đúng byte đó.

Đây là **lần thứ tư** cùng một bài học được ghi lại: ch.137 (kiểm chứng trình duyệt không
sống sót qua game thứ hai), ch.138 (một ngàn replay trong một process không nói gì về hai
toolchain), ch.139 (một process chơi cả hai bên không bao giờ cho chúng cùng một đội),
ch.141 (backend một kết nối che ba bảo đảm). **Bằng chứng không đi qua được cái biên mà
bạn đã không bước qua.**

## Mutation: 17

Hai cái quan trọng nhất là M1 và M2: đặt lại `return 0` vào đúng chỗ cũ, và xoá hẳn cái
early-return để `--seed-only` không bao giờ dừng. Cả hai chết trong `test_baas_boot` —
đúng lý do nó tồn tại: **trước slice này cả hai đều sống sót qua toàn bộ bộ test.**

**⚠️ Chưa xác minh:** job `baas-docker` **chưa chạy trên runner GitHub** (image đã dựng và
chọc thật ở local, dưới emulation amd64; YAML thì chưa) · `docker compose` chưa chạy lần
nào — healthcheck của nó *bây giờ mới có thể* pass (đã kiểm `curl` CÓ trong runtime image)
nhưng chưa ai chạy `up` · document OpenAPI **không mô tả schema thân request/response**,
chỉ mô tả "một object" + prose; đó là quyết định (schema gõ tay là bảng thứ hai để trôi),
không phải sót · `/v1/ws` không so được với bảng route theo phương pháp này, nó dựa vào
mô tả Drogon tự ghi · chưa có UI đọc spec (Swagger/Redoc cần CDN, mà trang này không có).


### S30c — hai game băm ra cùng một số ✅ 2026-09-07 · chương 145

Một slice có hai việc nhỏ: cho `--bench-ui` đo được **game**, và cấp manifest cho `iso`
với `colony`. Cả hai đều là việc sổ sách. Cả hai đều biến thành thứ khác sau khoảng bốn
phút.

**Cái benchmark không ai hỏi được.** `--bench-ui` sống trọn vẹn trong `src/main.cpp` từ
chương 108: warm-up, số học phân vị, phán quyết 8 ms và in ấn — sáu mươi dòng trong một
khối sau một cờ. Đó đúng là hình dạng repo này **cấm ở mọi chỗ khác**, và cái giá của
ngoại lệ đúng như luật dự đoán:

```
$ ./build/demo --bench-ui 0
[1]    12873 segmentation fault
```

`ms[ms.size() / 2]` trên vector rỗng. Và `atoi` biến mọi lỗi gõ thành `0`, nên
`--bench-ui twenty` cũng thế. **Chưa ai từng tìm ra**, vì muốn hỏi đoạn code phân vị một
câu thì phải cấp phát 14 MB framebuffer và render cả Studio.

Còn sai lần thứ hai mà không tiếng nổ nào báo: `ms[size * 95 / 100]` là số học nguyên
**trên chỉ số**. Với 120 mẫu ra 114, gần đúng. Với **mười** mẫu ra chỉ số 9 — **khung
tệ nhất của cả lượt chạy**, được in ra như phân vị 95 của nó.

**Bốn cái sống sót là bốn dòng thừa.** `percentile` ban đầu có cả early-out *lẫn* clamp,
và bốn mutation sống sót: bỏ bất kỳ dòng nào trong bốn dòng ấy cũng không đổi câu trả lời
ở đâu cả. Lý do lộ ra khi nói thành lời — **early-out và clamp phủ nhau khít**. Cám dỗ là
viết test ghim cả bốn; như thế là ghim một sự trùng hợp. **Bốn cái sống sót không phải bốn
test còn thiếu, chúng là bốn dòng thừa.** Xoá early-out, giữ clamp (đó là cái mà `p` ngoài
khoảng cần), và tính rank bằng **số có dấu** để một rank âm không cuộn thành chỉ số khổng
lồ. Hai assertion `p = -1` và `p = 2` là thứ làm clamp có gánh nặng thật.

**Và những con số chưa ai có.** `PROJECT-BRIEF` mang cảnh báo *"`--bench-ui` đo Studio khi
KHÔNG có game nào chạy"* từ chương 117; cả hai game vẽ nguyên một bàn điều khiển trên màn
hình mỗi khung ở `ss=2` đều ra đời **sau** dòng đó. Release, 200 khung:

```
  studio  1280x720 ss=2   median  5.63 ms
  fps      640x400 ss=1   median  0.94 ms
  farm     640x360 ss=2   median  2.20 ms
  creatures 640x360 ss=2  median  3.27 ms
```

Cả hai game đều thoải mái. Đó là một câu trả lời tốt, và là **lần đầu tiên nó là một câu
trả lời** thay vì một giả định.

**Hai lab vốn là game.** `iso` và `colony` nằm trong bảng LAB từ chương 120, nghĩa là
chưa cái nào từng đi qua inspect, package, publish hay hub, và chưa cái nào xuất hiện
trên trang bạn gửi cho người khác. Hai manifest, hai dòng chuyển từ `labs()` sang
`entries()`. Rồi:

```
published Iso Farm → development cbf29ce484222325
cbf29ce484222325 already stored with different bytes — refusing
```

**Release id không đặt tên cho release.** Hai game khác nhau, cùng một id — và
`cbf29ce484222325` không phải trùng hợp: đó là **FNV-1a 64 offset basis**, tức băm của
*không có gì*. `package_hash` phủ các resource; nó **không** phủ `project`, `schema`,
`entry` — ba dòng mà `build_package` ghi vào file, ngay phía trên chỗ băm.

Trường hợp closure rỗng là bản ồn ào. Bản tổng quát tệ hơn và im lặng hơn: **hai project
dùng chung art và chỉ khác `entry` là MỘT release.** Luật của store — cùng byte thì
verified no-op, khác byte dưới id đã có thì từ chối — **không giữ được** trừ khi id phủ
mọi thứ `package.txt` nói. Giờ nó phủ, và bằng *cấu tạo* chứ không bằng cẩn thận: một
`canonical_body` viết một lần, dùng cho cả băm lẫn file. Bóc dòng cuối của một
`package.txt`, băm phần còn lại, id quay về — và đó là một test.

**Và test đã mã hoá cái bug thành giá trị mong đợi của nó:**

```cpp
// A shippable project with no assets (package hash = the empty/FNV-basis hash).
CHECK(dev.has_value() && *dev == "cbf29ce484222325");
```

Không phải test yếu — là test **sai thứ**, viết bởi người đã suy luận ra và suy luận ra
một bất biến sai.

**Một thẻ trống đọc thành hỏng.** Trang collection từ chối hiển thị thẻ không có ảnh, và
comment của chính nó nói vì sao: *"một panel trống lặng lẽ không phân biệt được với một
game không có art"*. Nên hai game cần bìa, và bìa là `.hrt`, tức phải đi qua **một trong
bốn cửa**. Đi cửa `asset.pixels`, và bìa của iso được bố trí bằng **phép chiếu của chính
game** — ô `(col,row)` nằm ở `x = 32 + (col-row)*9, y = 20 + (col+row)*5`, đúng hình thoi
`iso_render` đi qua. Cửa thứ **năm** (nướng một khung của game đang chạy — mà `bench_core`
vừa viết xong làm việc đó chỉ tốn sáu dòng) đã được cân nhắc và **từ chối**: cửa thứ năm
là một quyết định có chương riêng, không phải hệ quả phụ của việc cần hai cái thumbnail.

**Cổng:** 94/94 ctest **hai lần** · **23 mutation, 22 giết, 1 tương đương** (ghi rõ), cộng
bốn cái sống sót được giải quyết bằng **xoá code thừa** chứ không phải thêm test · trang
web chạy thật qua Chrome: 5 game, **mọi bìa giải mã được**, 5 thẻ chơi được, bấm Play vào
thẳng colony đang chạy · golden path xanh, 0 rò `.tmp` · web build xanh · **đã nhìn** hai
bìa và thẻ Colony đã render.

**Chưa xác minh:** `--bench-ui` đo **RENDER thôi** — không `update()`, nên vòng ngày của
farm và hàng đợi job của colony không nằm trong các số này · cột p95 không đáng tin trên
máy này · **chưa game mới nào được chơi từ manifest trong cửa sổ native** · iso lưu vào
`farm_save.txt` ở gốc asset, không dưới `saves/`, không gắn tên project · `colony` vẫn
sinh `colony_agent.hrt` lúc chạy — một `.hrt` gitignore, do C++ tạo chứ không qua cửa nào
trong bốn cửa, và do đó vô hình với sổ provenance.


### S30b — cái comment tự đặt tên cho slice của chính nó ✅ 2026-09-07 · chương 144

Chương 112 viết Edit section rồi để lại một ghi chú trên đúng hai dòng tính bề rộng
inspector:

> *"The split is fixed — a draggable one needs a cursor shape, a hit zone and a
> persisted position, and no second author has asked for it yet."*

Đó là một comment **tốt**: không viết TODO, nó nói tính năng đó **giá bao nhiêu** và vì
sao chưa đáng trả. Ba mươi hai chương sau đã có tác giả thứ hai, và hoá ra ghi chú đó
**tự đặt tên cho slice của chính nó** — con trỏ, vùng bắt, vị trí được lưu. Cả ba, và
cái thứ ba là cái duy nhất **có một quyết định bên trong**.

**Vị trí được lưu là một quyết định, không phải một con số.** Bản dễ chỉ một dòng: nhớ
bề rộng, mở lại thì khôi phục. Rồi ai đó mở Studio trên màn hình laptop, 900px không vừa,
và Studio phải làm *một cái gì đó*. Luật chốt lại:

> **Số được LƯU không bao giờ bị kẹp. Số được VẼ thì luôn luôn.**

`fit_inspector` là hàm của **cửa sổ**, nên không có gì của một khung hẹp sống sót qua nó:
thu nhỏ thì panel hẹp lại, phóng to lại thì bề rộng bạn đã kéo **quay về nguyên vẹn**.
Kẹp ở chiều **vào** — ghi con số đã hẹp xuống đĩa — sẽ trông y hệt suốt thời gian cửa sổ
còn nhỏ, và **âm thầm phá huỷ** layout ngay lần resize đầu tiên. Đúng cái memory gọi là
*guard có chiều ngược*: bug không bao giờ là va chạm mà guard ngăn, nó là **guard không
bao giờ nhả**.

**`platform::set_cursor` chưa từng có một call site nào.** Có trong seam từ ngày seam
được viết, bảy hình con trỏ, và một trăm bốn mươi chương con trỏ trong project này luôn
là mũi tên. Mà cũng **không gọi được** từ chỗ cần: hầu hết Scene compile vào test
headless không link backend nào, nên lời gọi trực tiếp là undefined symbol ở một nửa số
target. Nên Scene **báo cáo** `ui::CursorHint` và `App::frame` — chỗ duy nhất đã có cửa
sổ — **áp dụng**. Cùng một trao đổi mà `ui::Input` đã làm với bàn phím (intent, không
phải key), chỉ theo chiều ngược lại.

**Status bar: một DANH SÁCH, không phải một câu.** Bốn workspace tự nối chuỗi, bốn kiểu
chấm câu khác nhau. Nhưng lý do đổi không phải gọn gàng, mà là dòng này:

```cpp
g.draw_text(area.x, sy, left.c_str(), ws.dirty() ? th::warn : th::text_muted);
```

Một chuỗi, một màu. Nên trong Pixels editor với file chưa lưu, **mã hex dưới con trỏ
được vẽ bằng màu của một cảnh báo**. Strip có thể nói *tài liệu chưa lưu*, hoặc nói *pixel
này là #3aa0ff*, và nói gì thì cũng nói bằng cùng một giọng. Giờ mỗi ô mang tone riêng và
dấu phân cách được **VẼ**, không phải gõ.

**Đếm màu, lần thứ ba.** Claim ở đây là về **màu**, và màu vô hình với mọi thước đo repo
này đã có: cùng số glyph, cùng vị trí, cùng checksum, cùng frame diff. Nên
`test_ui_golden` render strip vào buffer riêng và đếm một màu cụ thể, **cả hai chiều** —
strip bẩn có pixel `warn`, strip sạch **không có pixel nào**. Dòng thứ hai mới là dòng
quan trọng: thiếu nó, test pass trên một strip tô `warn` toàn bộ, tức đúng cái bug đang
sửa.

**Grid + snap.** Số học ở `sandbox_core` (`snap_to`, làm tròn ra xa 0 ở **cả hai** phía
gốc — luật halfway của `std::round` lệch một bên, và một scene bố trí quanh 0 sẽ snap nửa
trái khác nửa phải), trigger ở workspace. Đặt và kéo là **hai call site của cùng một câu
hỏi**, và cái kéo là cái suýt sai: snap **sau** khi trừ grab offset, để **actor** rơi vào
lưới chứ không phải cái pixel bạn tình cờ bấm trúng.

Và một sửa trong test, không phải trang trí: nút Play trước đây được với tới bằng
`vp.y - 30` — số học trên rect của người khác. Control **đầu tiên** từng được thêm vào
giữa header và body (chính là nút Grid, chương này) làm toạ độ đó trỏ sang thứ khác, và
test **vẫn pass**. Nó đang test nhầm control.

**Xanh một lần không phải là xanh.** `ctest` xanh; chạy lại ngay lập tức, `shell_golden`
FAIL. Block mới kéo divider rồi khẳng định file layout ghi lại — tức là nó **ghi**
`saves/studio.layout`. Lần chạy thứ hai Studio mở ra ở trạng thái đã kéo, và "kéo thêm
90px" đâm thẳng vào clamp, không nhúc nhích. Test đúng về sản phẩm và sai về chính nó.

**Cổng:** 93/93 ctest **ba lần liên tiếp** · **25/25 mutation** (hai lượt: 21/25 + 2
SKIPPED vì chuỗi không duy nhất, 2 SURVIVED thật — "ô rỗng vẫn được dấu phân cách" và
"nút Grid vẫn sống khi scene đang chạy" — cả hai đã đóng rồi giết lại) · golden path
xanh, 0 rò `.tmp` · web build xanh · **đã nhìn hai khung hình thật**.

**Chưa xác minh:** chưa có bàn tay nào kéo divider này trong cửa sổ thật — đổi con trỏ
được khẳng định như một giá trị `CursorHint` trong test headless, còn `set_cursor` của
SDL chạy lần đầu tiên trong đời và **chưa ai nhìn** · lab full-screen dùng cùng splitter
và cùng file nhưng chỉ tab của Studio có test · chưa thử `studio.layout` trên web/IDBFS ·
grid **cố ý không** được lưu, và đó là một phỏng đoán chưa ai dùng đủ một giờ để kiểm.


### S30a — một field không ai đọc ✅ 2026-09-07 · chương 143

`assets/farm/crops.def` viết `season=spring` từ chương 113. `defs.cpp` **parse** nó vào
`CropDef::season`. `world.cpp` **chưa bao giờ nhìn tới** — không lúc trồng, không ở ranh
giới ngày, không trong file save, không trên màn hình. Mười tám chương. Trồng bí ngô vào
mùa xuân được, trồng củ cải vào mùa đông được, và tất cả những gì cái từ đó làm là **nằm
trong một struct**.

**Một field được GHI mà không ai ĐỌC thì không phải dữ liệu, nó là một lời tuyên bố.**
Cái này tuyên bố rằng game có mùa.

**Giờ nó có nghĩa gì.** Một năm bốn mùa, mỗi mùa `kDaysPerSeason` ngày. **Bảy**, không
phải hai tám: một ngày ở đây là mười hai phút thật, nên một mùa dài kiểu Stardew là năm
tiếng rưỡi và người chơi **không bao giờ thấy nổi một lần chuyển mùa**. Bảy làm một mùa
khoảng chín mươi phút — đủ dài để lên kế hoạch bên trong, đủ ngắn để gặp được.

Hai thời điểm, **một luật**: hạt bị từ chối ngoài mùa (và câu từ chối **nói tên mùa hiện
tại**), còn thứ gì còn trong đất khi mùa xoay thì **chết** — chín hay không cũng vậy, đó
chính là áp lực. Cả hai đi qua `grows_in`, cố ý: chúng là **cùng một câu hỏi hỏi ở hai
lúc**, và hai bản sao của nó chính là cách một cây trở nên trồng-được rồi chết ngay.
Đất **sống sót** — héo không phải là bỏ cày.

**Cái trùng lặp đã cho phép nó mục.** Lần đầu đặt luật kiểm tra vào `assign_crop`, và
test "file có `season=sprnig` phải bị từ chối" **FAIL**. Vì `assign_crop` là đường
*override*; đường *FILE* — `parse_defs` — mang **bản sao riêng** của cùng cái dispatch
đó. Hai hàm cùng quyết định một cây có những key nào và giá trị nào hợp lệ. Chúng đồng ý
suốt ba mươi chương và bất đồng **đúng lúc** một trong các field mọc ra một luật.

Giờ là **một hàm**, với đúng một cờ là khác biệt THẬT giữa hai người gọi: một **FILE**
`.def` bỏ qua key nó chưa từng nghe (nên file mới vẫn load được trên build cũ), còn một
**OVERRIDE** từ remote config phải từ chối — không thì operator gõ `sel=40` và tin rằng
mình đã đổi giá. Nửa "tương thích tiến" của lời hứa đó **chưa từng có test** cho tới
chương này.

**Nhìn thấy được, không chỉ được thi hành.** Một luật có răng mà màn hình không nhắc là
một luật người chơi trải nghiệm như một cái bug. Ba chỗ nói ra: HUD đọc
`Day 8  summer 1/7`, báo cáo buổi sáng nói `3 withered`, và **chip hạt được vẽ MỜ** khi
cây đang chọn không thể xuống đất hôm nay.

## Mutation: 18 — và cái sống sót là một QUYẾT ĐỊNH VIẾT BẰNG MÀU

`S18 the seed chip never says out of season` **sống sót**: mọi test khác trong
`test_farm_scene` vẫn xanh với cái chip sáng vĩnh viễn. Không gì trong file đó nhìn thấy
một lựa chọn màu — `ink()` đếm pixel **không phải** nền, mà đó là câu hỏi sai cho "cái
NÀY có được vẽ ở đây không". Nên có `count_colour`, và test chọn slot Seed trước (để
nhãn của chính nó là `text`, không bao giờ `text_muted`, nên mọi pixel mờ trong ô đó
**phải** là dòng phụ), rồi bấm Q cho tới khi thấy chip **cả mờ lẫn sáng**. Hai chiều —
vì một cái chip lúc nào cũng mờ sẽ pass một test chỉ đi tìm cái mờ. Sau khi vá: **18/18**.

## Và một chỗ để ghi lại các quyết định

Nửa còn lại của slice: `docs/adr/` — một **chỉ mục**, không phải kho lưu trữ. Bốn mươi
chín dòng, mỗi dòng gọi tên một quyết định bằng một câu và trỏ vào chương nơi lập luận
**đã** nằm sẵn. **Không có prose mới**, cố ý — một bản sao thứ hai của một lập luận là
một thứ thứ hai để quên, đúng bài học của hai cái parser ở trên.

Thứ nó thêm vào mà cuốn sách không có là **các lần ĐẢO NGƯỢC**: tám dòng ghi
`Superseded by N` — ba cửa `.hrt` → bốn; attribution phải nhớ → provenance suy ra;
`fpsmap1` → `map2`; mười hai cờ lab → một cửa mỗi loại; "pool 1 nên nguyên tử" →
transaction + locking read; "Postgres là bản build lúc deploy" → một dialect trong nguồn
hai trên dây; `--seed` thoát → `--seed` phục vụ; và **`season` là nhãn → `season` là
luật**, tức chính chương này tự ghi lần đảo ngược của mình vào sổ.

Một chỉ mục 49 con trỏ mục theo **ba cách máy kiểm được** — chương bị đổi tên, id dùng
hai lần, `Superseded by` trỏ vào dòng không tồn tại — nên `test_adr_index` kiểm cả ba.
Nó bắt được một cái ngay: dấu `|` escape trong `` `HRT1\|w\|h\|RGBA8` `` khiến dòng 20
đọc thành chín cột vô nghĩa.

**⚠️ Chưa xác minh:** chưa chơi tay qua **một lần chuyển mùa thật** trong cửa sổ (test
mô phỏng bảy lần ngủ; ảnh chụp chỉ là ngày 1) · cân bằng kinh tế của tám cây **chưa đo**
— số tiền là ước lượng, không phải kết quả chơi thử · save cũ (trước chương này) mở lại
sẽ **héo sạch** ở lần chuyển mùa đầu nếu cây không đúng mùa; đúng luật, nhưng chưa ai
thử migrate · `docs/adr/` kiểm được **liên kết**, không kiểm được **nội dung** — một
dòng mô tả sai quyết định vẫn pass.


## Việc kế tiếp

**Lộ trình đã chốt 2026-09-06** — xem `PLAN-v2-CORRECTIONS.md` để biết vì sao thứ tự này
thay cho T1–T10 của `PLAN-v2.md` (tóm tắt: T1 và T2 đã xong ~85%, và bốn chương 124–127 đã
làm chín T6).

| # | Slice | Size |
|---|---|---|
| ~~S19~~ | ~~Trang web thật + chứng minh chạm~~ — **XONG**, chương 128 | M |
| ~~S20~~ | ~~CI chạy 28 test BaaS~~ — **XONG**, chương 129 (27/28; cái thứ 28 là một trần) | S |
| ~~S21~~ | ~~Collection page + `cover`/`summary` + README template~~ — **XONG**, chương 130 | S/M |
| ~~S22~~ | ~~Tạo asset mới + `.pack` + ATTRIBUTION tự sinh + asset card~~ — **XONG**, chương 131 | M |
| ~~S23~~ | ~~Kết thúc migration map: hấp thụ Map Lab, giết `fpsmap1`~~ — **XONG**, chương 132 | L |
| ~~S24~~ | ~~Hấp thụ 4 lab hiệu ứng (`fx light audio anim`) thành component của Scene~~ — **XONG**, chương 133 (13 lab → 9) | M |
| ~~S25~~ | ~~IntGrid + rule autotile trong Map workspace~~ — **XONG**, chương 134 (rule vào map, `farm::line_piece` bị xoá) | L |
| ~~S26~~ | ~~Mixer workspace (cửa **thứ tư** vào `.hrt`)~~ — **XONG**, chương 135; số cửa 3 → 4 đã đổi có chủ ý | L |
| ~~S27a~~ | ~~`creature_core` + 18 loài dựng từ Mixer~~ — **XONG**, chương 136 | L |
| ~~S27b~~ | ~~Creatures — game (overworld, battle, manifest, controls)~~ — **XONG**, chương 137 | L |
| ~~S28a~~ | ~~Replay như một FILE + verifier + chứng minh xuyên toolchain~~ — **XONG**, chương 138 | M |
| ~~S28b~~ | ~~PvP realtime + ELO — consumer thật đầu tiên của realtime/matchmaking~~ — **XONG**, chương 139 | L |
| ~~S29a~~ | ~~Khoá mọi đường đọc-rồi-ghi + seam dialect + bản tái hiện Postgres~~ — **XONG**, chương 140 | M |
| ~~S29b~~ | ~~**Làm Postgres CHẠY**~~ — **XONG**, chương 141: 30/30 trên Postgres thật, CI chạy cả bộ test **hai lần**, một lần mỗi backend | L |
| ~~S29c~~ | ~~OpenAPI `/v1/*` + job Docker chọc `/healthz`~~ — **XONG**, chương 142: 51 route, 51 tài liệu, và cái image **chưa bao giờ phục vụ** cho tới hôm nay | M |
| ~~S30a~~ | ~~farm `season` (field chết) + `docs/adr/` chỉ mục~~ — **XONG**, chương 143 | M |
| ~~S30b~~ | ~~Nợ Studio còn lại: `splitter()` + lưu `studio.layout`, status bar dạng segment, Scene grid/snap~~ — **XONG**, chương 144 | M |
| ~~S30c~~ | ~~`--bench-ui` chạy được cả farm/creatures; manifest cho `iso` và `colony`~~ — **XONG**, chương 145: và hai game không có asset nào **băm ra cùng một release id** | S |

Điểm dừng show được **đã đạt** sau S21: mở một link trên điện thoại, thấy danh sách game,
chọn một cái, chơi. Điểm tiếp theo là **sau S28** (hai game + PvP).

Sau đó (chưa xếp thứ tự):

- **Manifest cho `iso` và `colony`** → chuyển từ `labs()` sang `entries()`. `colony` cũng
  là client BaaS, nên nó là bài kiểm tra thứ hai cho đường manifest → scene.
- **Điều khiển màn hình mới chỉ có ở farm.** Nếu game thứ hai cần, `farm/controls.hpp` sẽ
  phải tách ra — nhưng **chưa có người dùng thứ hai**, nên chưa tách (đúng luật §10b).
- **Nước động**: `studio::make_sheet` làm được miễn phí; farm chưa biết gì về frame.
- **Vật liệu autotile thứ hai** — hiện chỉ con đường; chưa có gì dùng chung giữa hai bộ.
- **Đo chi phí frame của farm** — `--bench-ui` vẫn chỉ chạy Studio.

### Đã hoãn có chủ ý (đừng coi là quên)

- **Pan/zoom, multi-select, copy/paste, grid/snap** trong Scene canvas.
- **Inspector cho Spawner/OnOverlap** — round-trip được, không sửa được trong UI.
- Filter/paging cho audit log · cache hash theo mtime/size trong `inspect()`.
- **`.recipe` không nằm trong manifest** — nó là *source*, giống PNG import.
- **Chưa đo chi phí frame** của farm; `--bench-ui` vẫn không chạy farm.
- **Pixel workspace**: một layer, không selection/move/copy, không đổi kích thước
  canvas, **không tạo file mới**, guide cố định 16px. *(Chọn màu ngoài ảnh: đã mở ở
  ch.127.)* Mixer: **không có ô vuông S/V 2D** (ba slider, vì `ui::hit` báo click chứ
  không báo drag); **màu đã pha không có nhà** — không nối vào palette, muốn lấy lại thì
  eyedropper sau khi đã tô; **inspector không cuộn**, chỉ báo khi bị cắt.
- **`.pix` và `.hrt` có thể lệch nhau** — giống `.recipe`: test bắt được, không chặn được.
- **`autotile_index` (47-blob) vẫn không có art** — Tiny Town chỉ có mảng 9 mảnh.
- **Điều khiển màn hình**: chỉ farm có; luôn hiện, không tự ẩn trên desktop; **một ngón**
  (SDL dựng chuột từ chạm, nên không giữ hướng + bấm hành động cùng lúc).
- **Không có nút `F9`** (load) — load vứt bỏ ngày đang chơi và farm không có modal để hỏi
  lại; động từ phá huỷ ở lại sau một phím phải cố ý bấm.
- **Nhãn hạt trong ô hotbar có thể tràn** ô 62px với tên cây dài.

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

## Việc kế tiếp

**S16 — vẽ những mảnh mà pack không có, rồi autotile.** Vẫn là việc kế tiếp đúng
thứ tự đã tìm ra ở chương 123: **art trước, luật sau**.

1. Vẽ trong Pixel workspace các mảnh đường đi **rộng 1 ô** (dọc, ngang, 4 góc, 4 đầu
   mút) vào một sheet **của mình**, không phải sheet Kenney.
2. Rồi mới nối `autotile_index` vào farm. Lần đầu nó có người tiêu thụ kể từ ch.110.

Sau đó (chưa xếp thứ tự):

- **Colour picker** trong Pixel workspace — hiện chỉ tô được màu ảnh đã có.
- **Nút cho tool (1–4) và save** trên màn hình — người chơi điện thoại hiện chỉ đi
  được, dùng được, đổi hạt được, hết.
- **Đa chạm thật** ở platform seam — điều kiện để vừa giữ hướng vừa bấm hành động.
- **Hấp thụ Map Lab** (`--lab map` → workspace, bỏ `fpsmap1`).
- **Manifest cho `iso` và `colony`** → chuyển từ `labs()` sang `entries()`.
- **Nước động**: `studio::make_sheet` làm được miễn phí; farm chưa biết gì về frame.

### Đã hoãn có chủ ý (đừng coi là quên)

- **Pan/zoom, multi-select, copy/paste, grid/snap** trong Scene canvas.
- **Inspector cho Spawner/OnOverlap** — round-trip được, không sửa được trong UI.
- Filter/paging cho audit log · cache hash theo mtime/size trong `inspect()`.
- **`.recipe` không nằm trong manifest** — nó là *source*, giống PNG import.
- **Chưa đo chi phí frame** của farm; `--bench-ui` vẫn không chạy farm.
- **Pixel workspace**: một layer, không selection/move/copy, không đổi kích thước
  canvas, không tạo file mới, guide cố định 16px.
- **Điều khiển màn hình**: chỉ farm có; luôn hiện, không tự ẩn trên desktop.

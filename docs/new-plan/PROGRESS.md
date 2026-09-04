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
| S6 | Asset browser · Validation · Release workspace · Play viewport | ⬜ chưa | | 114 |
| S7 | Studio asset tooling | ⬜ chưa | | 115–116 |
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

---

## Việc kế tiếp

**S6 — Studio: Asset browser, Validation panel, Release workspace, Play viewport**,
chương 114.

Farm giờ là *lý do* để Studio trưởng thành: có một game thứ hai để duyệt asset, để
validate, và để **Play viewport** chạy thử ngay trong Studio thay vì phải mở cửa sổ
riêng. S6 cũng là chỗ hấp thụ Sandbox → workspace thứ hai, và **khi đó** interface
`Workspace` mới đáng tạo (D23).

**Ba quyết định vẫn cần anh chốt:**
1. **Tên hai game** — *Farm* / *Creatures* đang là tên tạm và đã đi vào
   `assets/projects/farm.gameproject` + `launch_entry`. Đổi tên sau là một lần sửa
   manifest + entry id, không đắt, nhưng càng để lâu càng nhiều chỗ nhắc tới.
2. **Bộ tileset pixel-art license mở** (CC0/CC-BY) để Farm có art — hay chờ S7 tự vẽ
   trong Studio? Hôm nay game vẽ bằng màu phẳng và hình tròn, và nó **trông đúng như
   thế**.
3. **Xoá 10 flag CLI cũ** ở S6 (`--hub-ui --shell --editor --sandbox --maplab` …) —
   cần anh đồng ý (`PLAN.md §8` mục 3).

### Đã hoãn có chủ ý (đừng coi là quên)

- Hấp thụ **Sandbox** và **Map Lab** vào Studio → khi có workspace thứ hai thì
  interface `Workspace` mới đáng tạo (D23).
- Xoá 10 flag CLI cũ → cần anh đồng ý (`PLAN.md §8` mục 3).

Đây là *consumer* của S4a, và cho tới khi nó tồn tại thì registry + undo + autosave
vẫn đang là "motion without connection". Việc:

1. `Workspace` interface: `name/update/render_canvas/render_inspector/commands`.
2. Studio: top bar · nav rail · canvas · inspector · panel dưới · status bar;
   splitter kéo được, layout lưu `assets/studio.layout`.
3. Hấp thụ **Sandbox** → workspace *Scene*, **Map Lab** → workspace *Map*; mỗi cái
   có inspector, **undo qua `CommandStack`**, zoom/pan, grid + snap, selection outline.
4. Autosave có timer thật + prompt recovery khi mở.
5. Command palette (`Ctrl+K`) liệt kê `cmd::all()`.
6. Chỉ xoá flag cũ **sau khi** hỏi anh (PLAN §8 mục 3).

**Lưu ý thứ tự**: theo blend (`PLAN.md §4`, Rule 5 của brief), có thể chèn **S5 Farm**
trước S4b nếu cần đổi gió — S5 chỉ phụ thuộc S3, vốn đã xong. Bốn slice vừa rồi đều
là plumbing, nên theo đúng Rule 5 thì **S5 mới là lựa chọn đúng tiếp theo**.

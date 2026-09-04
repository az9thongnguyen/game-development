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
| S4 | Studio shell + `commands_core` + undo | ⬜ chưa | | 111–112 |
| S5 | Farm — vertical slice | ⬜ chưa | | 113 |
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

---

## Việc kế tiếp

**S4b — Studio workspace model + hấp thụ Sandbox và Map Lab**, chương 112.

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

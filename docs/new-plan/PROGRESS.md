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
| S2 | `ui` v2 (seam → clip → overlay → id → focus → layout → widget) | ⬜ chưa | `feat/s2-ui-v2` | 109 |
| S3 | `tilemap_core` v2 + `camera2d` | ⬜ chưa | | 110 |
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

---

## Việc kế tiếp

**S2 — `ui` v2**, branch `feat/s2-ui-v2`, chương 109. Thứ tự bắt buộc (mỗi bước một
commit + test), vì bước sau không làm được nếu thiếu bước trước:

1. Platform seam: `Key` enum thêm Ctrl/Shift/Alt/Home/End/F1–F12; `InputState` thêm
   `wheel`, `text` (UTF-8 nhận trong frame), `mods`, key repeat.
2. `set_cursor` + `clipboard_get/set` (web: no-op fallback).
3. `Renderer2D::push_clip/pop_clip`.
4. Hoãn **riêng overlay** (D4) — không làm draw list đầy đủ.
5. Id stack (đóng ceiling `ui.hpp:50-53`).
6. Focus + bàn phím (Tab/Shift-Tab/Enter/Space/Esc).
7. Layout engine.
8. Widget rút gọn (D6).
9. Cửa sổ resizable.
10. Chương 109.

Chạy `--bench-ui` lại ở cuối S2 và so với mốc 4.63 ms.

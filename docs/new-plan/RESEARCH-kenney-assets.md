# RESEARCH 2 — Kenney.nl, hệ sinh thái asset và công cụ sinh asset

**Ngày:** 2026-09-05 · **Tiếp nối:** `RESEARCH-competitors.md` · **Ảnh hưởng:** `PLAN.md` S5–S9, `SPEC.md` §3.4, §3.9, §7.2
**Phương pháp:** đọc trực tiếp kenney.nl (trang chủ, Assets, Tools, Starter Kits, trang pack *Tiny Farm*, Kenney Shape, Creature Mixer trên itch, repo Starter-Kit-City-Builder) + đối chiếu các nguồn asset/tool cùng loại tháng 6–9/2026.

---

## 0. Kết luận nhanh

1. **Kenney không phải đối thủ, là *mảnh ghép* bạn đang thiếu.** Kenney = nội dung CC0 đồng nhất + vài công cụ nhỏ có ràng buộc + starter kit mã nguồn mở + cộng đồng. Bạn = engine + studio + backend. Hai bên gặp nhau ở đúng chỗ bạn yếu nhất: **asset đẹp, nhất quán, license sạch**.
2. Điều bạn thấy "giống ý mình" là đúng ở **3 điểm**: (a) *công cụ sinh asset có ràng buộc* (Creature/Avatar/Ship Mixer, Kenney Shape 64×64/16 màu) — đây chính là hướng Texture Lab/Studio nên đi; (b) *thư viện asset có metadata* (category · series · tag · tile size · license · version) — đây là Asset browser + Collection page; (c) *Starter Kit* — đây là `--project-new <template>` của bạn nhưng có README chuẩn và game hoàn chỉnh.
3. **Món quà trực tiếp:** pack **Tiny Farm** (2026, 130 file, 16×16, CC0) cùng series *Tiny Town / Tiny Dungeon / Tiny Battle* — đủ để làm Farm (S5) và phần lớn Creature RPG (S10) mà không vẽ gì, cùng một style. Đưa vào `ASSETS-LICENSE.md` ngay.
4. **Ý tưởng đáng clone nhất từ toàn bộ nghiên cứu này: "Mixer" như một workspace của Studio** — sinh sprite từ *parts + palette swap + frame animation* theo data. Nó giải quyết mục tiêu "Studio tạo được asset đẹp như bên ngoài" bằng cách **không yêu cầu vẽ**, và khớp hoàn hảo với Creature RPG (sinh 18–150 loài từ vài chục bộ phận).
5. Xu hướng 2026 của mảng tool asset là **AI generator** (Sprite Fusion đang train model pixel-art riêng; SpriteCook; Sprite-AI). Bạn không nên đua AI; định vị Texture Lab/Mixer là **deterministic, inspectable, offline, seed-reproducible** — đúng tinh thần content-addressed của release pipeline.

---

## 1. Kenney.nl — mổ xẻ

### 1.1 Cấu trúc sản phẩm

| Mảng | Nội dung | Mô hình tiền |
|---|---|---|
| **Assets** | ~14 trang pack; "60,000+ assets"; 2D/3D/UI/Audio/Pixel/Textures; series (Tiny, Mini, City, Scribble, Modular, Board Games, Input Prompts, VFX, Patterns…); filter category/tag/series/tile size; sort release date/latest update/quantity/name; trang **Upcoming** và **Feed** | Free CC0; màn "donate trước khi tải" (bỏ qua được); **All-in-1 bundle $19.95** trên itch kèm update trọn đời |
| **Tools** | Asset Forge ($19.95 / Deluxe $39.95): kitbash 3D bằng block. **Kenney Shape** ($3.99): vẽ pixel 2D → đặt depth từng pixel → xuất 3D (OBJ/FBX/glTF/VOX/STL) hoặc PNG/SVG; *giới hạn cố ý 64×64, 16 màu*; depth tự sinh theo luminosity/color index/noise. **Mixers** (free, chạy trong browser, làm bằng Unity): Creature / Avatar / Ship Mixer — chọn bộ phận theo tab, đổi màu palette, xem frame animation; bán gói sprite tách rời $10; license riêng (mọi mục đích, **trừ NFT/blockchain**) | Tool trả tiền một lần, mixer free + upsell |
| **Starter Kits** | Repo GitHub MIT cho Godot 4.6: Match-3, Racing, 3D Platformer, FPS, City Builder (1.4k★, 185 fork), Basic Scene. README chuẩn: Features · Screenshot · Controls table · Instructions "How to…" · License tách **code MIT / asset CC0** | Free, đăng cả trên Godot Asset Store |
| **Games / Club / KB** | Game riêng; Patreon "Kenney Club" (early access, goodies); Knowledge Base; newsletter; Mastodon/Bluesky | Patreon |

### 1.2 Vì sao Kenney thắng (và bạn học được gì)

| Nguyên tắc Kenney | Bằng chứng | Áp vào dự án bạn |
|---|---|---|
| **Một style, hàng chục pack** — mọi pack trong một series khớp nhau về palette, outline, tile size | Series *Tiny* (Farm/Town/Dungeon/Battle/Ski) đều 16×16 cùng palette; "You might also like" chỉ gợi cùng series | **Style Guide per project** (palette + tile size + outline rule) trong Studio; validator cảnh báo khi import asset lệch style (S6) |
| **License không cần nghĩ** — CC0 toàn bộ, ghi rõ trên mỗi trang | Bảng metadata mỗi pack có dòng License | Cột `license` bắt buộc trong `.pack` manifest; badge trên Asset browser; sinh `ASSETS-LICENSE.md` tự động từ manifest |
| **Metadata đủ để lọc** | category · series · tags · tile size · file count · version history | Format `.pack` (§3.1) + filter trong Asset browser + Collection |
| **Ràng buộc là tính năng** | Kenney Shape 64×64/16 màu; Mixer chỉ cho chọn part | Mixer workspace có ràng buộc tương tự → kết quả *luôn* đẹp và nhất quán |
| **Starter kit là game thật, không phải demo** | City Builder có build/remove, camera, MeshLibrary động, save/load | Mỗi template `--project-new` phải là game chơi được + README theo mẫu Kenney (§3.3) |
| **Cầu nối cộng đồng rẻ** | Feed/Upcoming, newsletter, Patreon, donate gate mềm | Trang Collection có "Upcoming" (slice kế tiếp từ PLAN) + RSS từ chapter mới |
| **Web-first tool** | Mixer chạy trong browser, không cài | Studio WASM (khả thi vì CPU-only) — ít nhất Mixer + Pixel editor chạy web (S9 mở rộng) |

### 1.3 Điểm yếu của Kenney (chỗ bạn bổ sung được)
- Tool rời rạc, không có pipeline vào game: xuất PNG rồi tự lo. Bạn có manifest + closure + release → **asset vào game một cú kéo thả, hash theo dõi được**.
- Mixer đóng (Unity build), không mở rộng bộ phận. Bạn làm **Mixer data-driven** (`.mixer` def) → cộng đồng thêm part được.
- Không có tilemap/level tool, không có autotile. Bạn có Map workspace.
- Style "clean/flat" của Kenney bị chê "không đặc trưng" — Texture Lab v2 (dither, outline, palette) cho phép *tái tô* pack Kenney theo style riêng → asset gốc CC0 nhưng nhìn khác.

---

## 2. Hệ sinh thái asset & tool xung quanh (để chọn đúng nguồn và không đua sai chỗ)

### 2.1 Nguồn asset (đối chiếu 6–9/2026)

| Nguồn | Điểm mạnh | License | Dùng cho bạn |
|---|---|---|---|
| **Kenney** | ~40–60k asset, style đồng nhất, tiles/UI/audio/fonts | CC0 | Nguồn chính cho Farm + Creature + HUD |
| **itch.io creators** (Pixel Frog, Ansimuz, 0x72, Cainos…) | pack platformer/dungeon/top-down hoàn chỉnh có animation | Pixel Frog/Ansimuz/0x72 CC0; Cainos license riêng | 0x72 *DungeonTileset II* (CC0) cho quái/mine của Farm |
| **OpenGameArt** | đa dạng nhất, filter theo license | trộn CC0/CC-BY/CC-BY-SA/GPL; **LPC là CC-BY-SA** (lây license!) | Chỉ lấy CC0/CC-BY; tránh LPC nếu không muốn SA |
| **CraftPix** | "Game Kits" theo genre, có animation cắt sẵn | license riêng (không resell file) | Tham khảo cách đóng gói "kit theo genre" |
| **Unity Asset Store free / Fab** | nhiều nhưng engine-specific | tuỳ publisher | Bỏ |
| **Poly Haven / Google Fonts / Freesound** | textures/fonts/audio | CC0 / OFL / tuỳ | Fonts OFL (Inter, m5x7…), SFX CC0 |

Quy tắc đề xuất cho `ASSETS-LICENSE.md`: **CC0 mặc định; CC-BY chấp nhận có ghi credit; cấm CC-BY-SA và license "no-resell" tuỳ biến** để repo mở của bạn không kéo theo nghĩa vụ lạ.

### 2.2 Công cụ tạo/xử lý asset (bản đồ để định vị Studio)

| Tool | Loại | Giá | Học gì |
|---|---|---|---|
| **Sprite Fusion** | tilemap editor **trong browser**, không cần login; export Unity/Godot/Defold/TMX/JSON/PNG; autotile, collision; helper cho GB Studio; đang xây **AI pixel-art generator** riêng (16/32/64 px, idle/run/attack) (tin 8/2026) | free + tool trả tiền | Chứng minh "editor web không login" có sức hút lớn; export đa engine là điểm bán. Bạn: Studio WASM + export `map2`→TMX/JSON để người ngoài dùng Map workspace của bạn *dù không dùng engine của bạn* |
| **Tilesetter** | sinh **47-blob tileset từ vài tile mẫu** + map editor | $12.99 | Đúng "quick setup" autotile trong SPEC §3.9 |
| **Pixel Composer** | node-based VFX compositor cho pixel art | $10 | Mô hình node cho Texture Lab v2 (bạn dùng stack tuyến tính là đủ, node là stretch) |
| **Laigter** | normal map cho sprite 2D | free | Nếu `light_core` muốn normal-mapped 2D light (stretch) |
| **SpookyGhost** | procedural sprite animation (open source) | free | Ý tưởng cho `.anim`: animation bằng tham số (bob, squash) thay vì frame |
| **Kenney Shape** | pixel → 3D extrude, 64×64/16 màu | $3.99 | Ràng buộc + "auto depth theo luminosity/index/noise" → có thể clone rẻ để Texture Lab xuất **voxel/3D mini** cho `render3d_core` (stretch vui) |
| **Creature/Avatar/Ship Mixer** | parts + palette swap + frames, chạy web | free | **Clone thành Mixer workspace (§3.2)** |
| **Pixelorama / Aseprite / LibreSprite** | pixel editor | MIT / $19.99 / GPL | Đã phân tích ở RESEARCH 1 |
| **SpriteCook, Sprite-AI, Sprite Fusion AI** | AI generator | freemium | Không đua; cho phép import PNG từ chúng |

---

## 3. Đề xuất cụ thể vào PLAN/SPEC

### 3.1 Format `.pack` (Asset pack manifest) — thêm vào SPEC §3.4, làm ở S6

```
pack 1
name Tiny Farm
author Kenney
source https://kenney.nl/assets/tiny-farm
license CC0-1.0
version 1.0
category 2D
series Tiny
tags farm rpg roguelike map pixel
tile 16
palette palettes/tiny.hex          # tuỳ chọn; dùng cho Style Guide check
preview preview.png
file tilemap.png tileset
file characters.png sheet fw=16 fh=16
file ui.png ui
```

- Asset browser hiển thị **card giống trang Kenney**: preview, tên, badge category/series/license, tile size, số file. Filter: category · series · tag · tile · license.
- `ASSETS-LICENSE.md` **sinh tự động** từ mọi `.pack` trong asset root (`--cmd asset.license-report`) → không bao giờ quên credit.
- `resource_core` closure tính hash cả `.pack` → thay pack = release id đổi, đúng triết lý.

### 3.2 Mixer workspace — thêm vào S7 (ưu tiên cao hơn Pixel editor đầy đủ)

**Ý tưởng.** Data-driven parts-based sprite generator, chạy trên `ui` v2 + `image`:

```
mixer 1
name creature
size 32 32
frames 4 fps 6
palette base #3a2f2a #6b4f3a #c58f5a #e9d7b8      # 4 ramp slot, người dùng swap
slot body   layer 0 required
slot head   layer 1 required
slot eyes   layer 2 required
slot wings  layer 3 optional
slot tail   layer 3 optional
part body slime parts/body_slime.png   anchor 16 24
part head round parts/head_round.png   anchor 16 12  attach body:head
...
rule wings requires body:not slime
```

- **UI**: tab theo slot (như Mixer của Kenney), grid part có preview, palette ramp bên phải (click đổi màu → **palette swap theo index**, không tô lại), nút Random / Randomize-with-seed, kéo offset part, xem frame.
- **Output**: PNG sheet + `.anim` + dòng `asset` vào manifest; **seed + recipe lưu trong `.mix` file** → tái tạo được, diff được, hash được.
- **Ứng dụng ngay**: Creature RPG cần 18 loài MVP → thiết kế ~10 part/slot, sinh hàng nghìn tổ hợp, chọn 18; evolution = cùng seed + thêm part. Farm: NPC avatar mixer (đầu/tóc/áo) cho 2–6 NPC; item icon mixer (khung + biểu tượng + màu).
- **Test**: cùng seed + cùng recipe → cùng hash PNG (deterministic), 100 seed không crash, part anchor nằm trong canvas.
- **Chapter**: "Constrained generators: why parts + palettes beat brushes for solo devs".

Đây là *phần cụ thể hoá* của lời hứa "mini studio tạo asset đẹp như bên ngoài": bạn không cần vẽ giỏi, cần một bộ part tốt (có thể lấy từ Kenney CC0 cắt ra) + generator có ràng buộc.

### 3.3 Starter Kit README template — áp cho mỗi `.gameproject` (S5, S9)

Theo đúng cấu trúc Kenney (đã được 1.4k★ chứng minh dễ đọc):

```
# <Game name>          (icon.png bên cạnh)
Một câu mô tả + "built on hand-engine, runs native + browser"
## Features   (5–8 bullet, chỉ tính năng chơi được)
## Screenshot / GIF
## Controls   (bảng Key | Command; thêm cột Touch)
## Instructions
   1. How to add a crop / creature?   → sửa .def nào, mở workspace nào
   2. How to add a map?               → Map workspace, trigger
   3. How to save/load & cloud save?  → save_core, dashboard
   4. How to publish?                 → Release workspace / --cmd project.publish
## License    (code MIT · assets: xem ASSETS-LICENSE.md)
## Chapter    (link chapter tương ứng)
```

Trang Collection (S9) render README này + nút Play.

### 3.4 Style Guide + validator (S6)
- `style.def` trong project: `tile 16`, `palette palettes/tiny.hex`, `outline 1 #1a1a2e`, `max_colors 32`.
- Validation panel cảnh báo: asset có màu ngoài palette (> tolerance), tile size lệch, sprite không chia hết cho tile. Không chặn, chỉ cảnh báo — như Kenney "You might also like" giữ người dùng trong một series.

### 3.5 Bộ asset CC0 đề xuất để bắt đầu ngay (ghi vào `ASSETS-LICENSE.md` khi dùng)

| Nhu cầu | Pack | Ghi chú |
|---|---|---|
| Farm tiles/crops/tools/NPC | **Kenney Tiny Farm** (2026, 130 file, 16×16) | Nền cho S5 |
| Town/house/shop | **Kenney Tiny Town** | Cùng series |
| Mine/dungeon, quái | **Kenney Tiny Dungeon**, **0x72 DungeonTileset II** | Cả hai CC0 |
| Battle screen Creature RPG | **Kenney Tiny Battle** + Mixer sinh creature | Kenney Creature Mixer *không phải CC0* (license riêng, cấm NFT) — dùng làm cảm hứng, không copy part |
| HUD 9-slice, nút, thanh | **Kenney UI Pack / Pixel UI** *(verify tên chính xác trên site)* | Cho `theme_hud` |
| Gợi ý phím/nút gamepad/touch | **Kenney Input Prompts** | Cho tutorial + web touch overlay |
| Light sprite | **Kenney Light Masks** | Cho `light_core` |
| Texture Lab tham chiếu | **Kenney Pattern Pack / Pattern Pack Extra** | So chất lượng output procedural |
| Font | Kenney Fonts *(verify)*, m5x7/monogram (free), Inter (OFL) | |
| SFX/Music | Kenney audio packs *(verify tên)*, Freesound CC0 | |

### 3.6 Điều **không** làm theo Kenney
- Không bán asset/tool; không donate gate. Mục tiêu bạn là learning + open source.
- Không mở rộng sang 3D kitbash (Asset Forge) — ngoài Rule 3.
- Không làm marketplace/upload của người khác (moderation, license risk). Chỉ **import** từ nguồn ngoài qua `.pack`.

---

## 4. Cập nhật bảng "clone gì → slice nào" (bổ sung cho RESEARCH 1 §8)

| # | Tính năng | Nguồn | Slice | Ưu tiên |
|---|---|---|---|---|
| 26 | `.pack` manifest + Asset browser card kiểu Kenney + filter | Kenney Assets | S6 | Cao |
| 27 | **Mixer workspace** (parts + palette swap + seed) | Kenney Mixers | S7 (làm *trước* Pixel editor đầy đủ) | **Rất cao** |
| 28 | `ASSETS-LICENSE.md` sinh tự động | Kenney license hygiene | S6 | Cao |
| 29 | README template Starter Kit cho mỗi game | Kenney Starter Kits | S5, S9 | Cao |
| 30 | Style Guide `style.def` + validator | Kenney series | S6 | Trung |
| 31 | Dùng series *Tiny* làm asset nền Farm/Creature | Kenney | S5, S10 | Ngay |
| 32 | Export `map2` → TMX/JSON để dùng ngoài engine | Sprite Fusion | S9 | Thấp |
| 33 | Quick-autotile: sinh 47 từ 5–9 tile mẫu | Tilesetter | S7 | Trung |
| 34 | Mixer + Pixel editor chạy WASM không login | Sprite Fusion, Kenney Mixer | S9 mở rộng | Trung |
| 35 | "Upcoming" + RSS trên Collection | Kenney Feed/Upcoming | S9 | Thấp |

**Sửa SPEC:** §3.9 thêm mục *Mixer* trước *Pixel editor*; §3.4 thêm `.pack`; §7.2 Collection thêm README render + Upcoming; §5.5 và §6 (asset) trỏ tới bộ pack ở §3.5 file này.

---

## 5. Định vị sau hai vòng nghiên cứu

> **Kenney cho bạn asset. Godot cho bạn ngữ pháp UI. LDtk cho bạn mô hình map. Nakama/Talo cho bạn hình dạng backend. PICO-8 cho bạn tinh thần. Không ai trong số họ cho bạn *tất cả trong một binary, đọc được từng dòng, chạy trong browser, release theo hash*.**

Câu README đề xuất, cập nhật: *"A hand-written game console + studio + self-hosted backend, fully inspectable. Make assets with constrained generators, build maps, publish by content hash, run natively or in the browser. Every subsystem has a chapter."*

---

## 6. Nguồn
- kenney.nl (Home, /assets, /tools, /starter-kits, /assets/tiny-farm, /tools/kenney-shape) — đọc 5/9/2026.
- kenney.itch.io/creature-mixer (license, cơ chế, 4.9★/253); github.com/KenneyNL/Starter-Kit-City-Builder (README, MIT, 1.4k★).
- Cinevva "Best Free 2D Sprites… (2026)"; spritesheetgenerator.online (6/2026); AssetHoard (1/2026); soonlab (7/2026); gamineai (3/2026); itch.io blog.
- phaser.io news "Sprite Fusion… pixel art generator" (8/2026); spritefusion.com; spritecook.ai compare (6/2026); itch.io collections (Pixel Composer, Tilesetter, Laigter, SpookyGhost).

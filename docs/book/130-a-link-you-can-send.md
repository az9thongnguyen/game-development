# Chapter 130 — A link you can send

> Code: `web/collection.html` · `src/engine/project/collection.{hpp,cpp}` ·
> `src/engine/project/project.cpp` · `src/engine/assets.cpp` ·
> `scripts/web_collection_check.mjs` · `CMakeLists.txt`

## Tóm tắt (VI)

Chương 128 làm cho **một** game với tới được bằng ngón tay, ở một URL không ai đoán ra:
`demo.html?project=projects/farm.gameproject`. Chương này làm nốt nửa còn lại — một trang
**liệt kê** những gì có, và một nút Play **đáp xuống** trong game đang chạy.

Bốn điều đáng kể hơn tính năng:

1. **Trang tự giải mã `.hrt`.** `.hrt` là `"HRT1" | BE w | BE h | RGBA8` — mười lăm dòng
   `DataView`. Nên ảnh bìa là **đúng cái file engine đọc**, không phải mở thêm cửa PNG vào
   pipeline asset chỉ để phục vụ một trang web. `.hrt` vẫn đúng **ba** cửa; cửa thứ tư là
   quyết định của S26, không phải tác dụng phụ của slice này.

2. **Ảnh chụp lại tìm ra lỗi — chương thứ TƯ liên tiếp.** Mọi assertion xanh trên một trang
   mà canvas 320×180 bị CSS thu xuống 258×145: tôi tính scale **nguyên** rất cẩn thận rồi
   để CSS nhân nó với 0,81 ngay sau đó. Đúng lỗi của chương 128, ở trang bên cạnh.

3. **Cái bẫy `POST_BUILD` sập trong vòng một giờ kể từ lúc tôi viết chú thích về nó.**
   `POST_BUILD` chỉ chạy khi `demo` link lại; sửa mỗi trang thì không copy gì, và bài kiểm
   trình duyệt tiếp tục kiểm **trang cũ** — y hệt cái bẫy `--shell-file` đã đặt ở chương 118.

4. **Công cụ đo test lại chính là thứ nói dối.** Lần chạy mutation đầu cho 14/15 — sạch một
   cách đáng ngờ. Harness khôi phục **nội dung** file nhưng đẩy **mtime lùi lại**, nên
   object của phép biến đổi trước sống sót sang lần build sau: từ M4 trở đi **mỗi lần chạy
   có hai mutation**, và cái thứ hai làm crash mọi thứ. Sửa xong: **15/15**, cộng một dòng
   `baseline after restore: GREEN` để câu hỏi "cây code có sạch không" có câu trả lời.

---

## The index cannot be a list somebody keeps

A browser cannot read a directory. Something has to write down what is in `projects/`,
and the tempting thing is a list in the page.

Chapter 129 is what happens to those. Twenty-eight tests went dark because the thing that
named them stopped naming all of them, and nobody noticed for two years of chapters. So
the index is **baked**:

```sh
./build/demo --cmd collection.index projects collection.json
```

`assets/collection.json` is committed, and it stands in exactly the relationship a `.hrt`
has to its `.recipe` or its `.pix`: *the committed artefact must be what the sources bake
to, and a test says so rather than the commit message.*

```cpp
// tests/test_collection.cpp
const auto want = to_json(build_collection("projects", {"fps", "farm"}));
const auto got  = assets::load_file("collection.json");
CHECK(std::string(got->begin(), got->end()) == want);
```

Add a game and forget to re-index, and that is a red test, not a game missing from the
page. It reads the real directory to do it, which is the point.

The listing itself needed a new primitive, and it is the fourth verb on the I/O seam:

```cpp
// engine/assets.hpp
std::vector<std::string> list_dir(const std::string& dir, const std::string& suffix = "");
```

Sorted — not for tidiness. It feeds a **committed file**, and two machines whose `readdir`
disagrees must not produce two different artefacts. A missing directory is an empty list,
because "no projects here" is an answer and not a malfunction. `std::filesystem` works
over Emscripten's MEMFS, so one implementation serves native and web, which is the whole
argument for the seam.

## Two fields, and the one that ships

```
gameproject1
name Farm
schema 1
entry farm
summary Plant, water, sell, sleep — a day at a time, and it remembers.
cover textures/town.hrt
asset map maps/farm_home.map2
...
```

Both optional, and **emitted only when set**. That is the migration, and it is a test:

```cpp
const std::string old_text = "gameproject1\nname X\nschema 1\nentry fps\n";
CHECK(to_text(*parse_project(old_text)) == old_text);
```

An always-emitted `summary ` line would parse back to the same `Project` and rewrite every
manifest that has never heard of summaries, the first time anything saved one. A diff
nobody asked for is how a round-trip test stops being evidence.

`cover` is content — it ships — so it joins the resource closure and gets hashed like
everything else. With one exception, which is the interesting half:

```cpp
std::vector<AssetRef> declared = in.project.assets;
if (!in.project.cover.empty()) {
    bool already = false;
    for (const auto& a : declared)
        if (a.path == in.project.cover) { already = true; break; }
    if (!already) declared.push_back({"cover", in.project.cover});
}
```

The farm's cover is its own tileset, which is already `asset texture textures/town.hrt`.
Hashing it twice would put one file in the package under two names and **move the release
id with no change in content** — the one thing a content-addressed id must never do.
Proof that it does not: publishing after this change reported `verified`, not a new id.

And `validate()` refuses a cover that is not `.hrt`, because the page decodes `.hrt` by
hand and anything else is a blank card — a failure that looks exactly like a slow network.
Both directions are tested, which is chapter 127's lesson: the guard that never lifts is
the bug the guard was supposed to prevent.

## The page decodes the engine's own format

```js
var dv = new DataView(buf);
if (String.fromCharCode(dv.getUint8(0), ..., dv.getUint8(3)) !== 'HRT1') return null;
var w = dv.getUint32(4), h = dv.getUint32(8);      // big-endian: DataView's default
return { w: w, h: h, px: new Uint8ClampedArray(buf, 12, w * h * 4) };
```

Fifteen lines, and it means a cover is the same file the engine reads. The alternative was
a PNG *encoder* — this project has a hand-written decoder and no encoder — or a fourth
offline door into `.hrt` for "a frame we captured". Both are real work with real arguments
for them, and neither belongs in a slice about a list of games. `CLAUDE.md` still says
three doors.

## The bug only a screenshot could show, again

Every assertion in the new browser check passed. Then the screenshot:

> A plain `<canvas>` scales proportionally under `max-width: 100%`. A canvas with a
> **fixed drawing buffer** inside a smaller box does not scale — it is *resampled*, at
> whatever fraction the box happens to be.

The card is 258 CSS px wide; the buffer was 320×180; the browser scaled it by 0.81 on the
way to the screen. Which is to say: `paint()` computed a careful **integer** scale so that
one art pixel is a whole number of screen pixels, and then CSS multiplied the result by
0.81 and threw the arithmetic away.

The fix is chapter 128's, one page over — size the drawing buffer from the box it is shown
in, and keep doing it:

```js
var box = canvas.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
var cw = Math.round(box.width * dpr), ch = Math.round(box.height * dpr);
if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
...
new ResizeObserver(function () { sizeAndPaint(cv, img); }).observe(cv);
```

The covers now report `64x64@9x` and `192x176@3x` — integer scales in **device** pixels,
which is the only place an integer scale means anything. And the check grew the assertion
that would have caught it, as a number:

```js
if (Math.abs(c.bufW - c.wantW) > 1 || Math.abs(c.bufH - c.wantH) > 1)
    fail(`card ${i}'s cover buffer is ${c.bufW}x${c.bufH} inside a ${c.wantW}x${c.wantH} box`);
```

Fourth chapter running (126: a button drawn through a warning chip; 127: a label off a
panel; 128: a stretched canvas). The pattern is stable enough to be a rule: **assertions
check what a thing is, and a frame checks what it looks like next to everything else.**

## The trap sprung inside the hour

The copy of the page into `build-web/` started as `add_custom_command(TARGET demo POST_BUILD)`,
with this comment attached:

> *"POST_BUILD, so it runs when demo relinks. Editing only a cover and re-running the build
> copies nothing."*

Written as a footnote, and true. Then the very next edit was to `collection.html`, `demo`
did not relink, nothing was copied, and the browser check went on passing against the
**previous** page — which is precisely why `web/shell.html` is a `LINK_DEPENDS` (chapter
118, found the same way, by testing a change that was never in the bundle).

A custom target is always out of date, so it always runs:

```cmake
add_custom_target(web_pages COMMAND ... copy_if_different ... COMMAND ... copy_directory ...)
add_dependencies(demo web_pages)
```

What it copies is an **allowlist** — `assets/textures` and `assets/projects` — not the tree
minus a few. `assets/` also holds `saves/`, `releases/` and `channels/`, and chapter 128 is
what happens when a *denylist* forgets one: `saves/device.id` shipped, and every browser
signed in as the same guest. A denylist is one forgotten line from doing it again; a list
of what may be served cannot leak what nobody added to it.

## The instrument was the thing that lied

Fifteen single-token mutations, first run: **14 killed, 1 alive**. Suspiciously tidy.

The survivor was real and worth fixing — nothing tested that a *directory* named
`x.gameproject` is not a project, and leaving it in produces a card for a manifest nobody
wrote, reporting a problem that does not exist. That test now exists.

But the other fourteen were not evidence. The harness did this:

```python
shutil.copy(f, f + '.bak')      # save
open(f, 'w').write(mutated)     # mutate  -> mtime NOW
...
shutil.move(f + '.bak', f)      # restore -> mtime is the COPY's, i.e. EARLIER
```

`make` compares mtimes. After a restore the source looked *older* than the object built
from the mutated text, so it was never recompiled. From M4 onward every run carried **two**
mutations: the one under test, and M3 — which had removed a bounds guard and made
`test_collection` abort with `std::out_of_range` before reaching any assertion. Everything
after it "died" of the wrong cause.

It surfaced as a crash in a test that had passed ten minutes earlier, on a source file
`git diff` said was correct. Two lines fix it, and the second one is the more important:

```python
os.utime(f, None)               # mtime forward, or the next build keeps this object
...
base = ctest(...)               # and SAY whether the tree came back clean
print("baseline after restore:", "GREEN" if base.returncode == 0 else "*** RED ***")
```

Re-run: **15 of 15 killed, baseline GREEN**. Plus four on the page itself, driven through
the browser — a canvas buffer that is not resized (the screenshot bug, restored: killed),
`.hrt` read little-endian (killed), markdown with no table support (killed), and a Play
button that drops the project it was pointing at (killed).

The lesson is not about `mtime`. It is that a mutation score is a claim about the tests,
produced by a program, and that program had no test at all. A tool that measures quality
is not exempt from being measured.

## What the check actually proves

A correct `href` is not a working link. Chapter 123's keyboard was wired to a canvas nobody
had focused, and every check that stopped one step short of the end passed. So this one
does the last step:

```
ok    the page listed 2 games
ok    every cover decoded (64x64@9x/205c, 192x176@3x/32c)
ok    2 cards fit the viewport, 2 of them playable
ok    the README rendered (6 headings, 1 table, 11 list items, 1 code block)
ok    tapping Play landed in a running game (/demo.html?project=projects%2Fcreator.gameproject)
```

`205c` and `32c` are distinct colours counted out of the canvas. A decoder that quietly
produces nothing leaves a black rectangle, which on a dark card looks like art. And the
tap is a real `Input.dispatchTouchEvent` under touch emulation, followed until the
destination page says `running` — the whole slice, end to end, in one line.

A broken project is not hidden from any of this. `inspect()` already refuses to drop a
missing asset from its list, for the reason written there — *a browser that silently drops
what it cannot find is the one you cannot use to find out what is wrong* — and the card
applies the same rule one layer out: no Play button, and every problem listed where it
would have been.

## What was checked

| Claim | How |
|---|---|
| the index is what the manifests bake to | `test_collection` re-bakes from the real `projects/` and compares the committed bytes |
| adding a game cannot be forgotten | the same test asserts the entry count equals `list_dir`'s |
| old manifests are untouched by the new fields | `to_text(parse(old)) == old` |
| a cover ships, and ships once | `test_inspect`: a new cover joins the closure; a declared one is not hashed twice; publishing reported `verified`, not a new id |
| a broken project is listed, not hidden | a deliberately broken manifest comes back unplayable, with its problems and no package hash |
| the covers are decoded, not merely fetched | the page's canvas is read back and its distinct colours counted |
| the pixel art is not resampled | drawing buffer vs displayed box × dpr, within 1 px |
| Play reaches a running game | a real touch on the button, then wait for `status == running` |
| the README renders as a document | 6 headings, 1 table, 11 list items, 1 code block, and no `##` left in the text |
| the checks can fail | 15 C++ mutations, 15 killed, baseline green · 4 page mutations, 4 killed |

## Ceilings

- **A cover is an existing texture, not a title card.** The farm's is its tileset; the
  raycaster's is a brick. A *captured frame* would be the honest cover and is a fourth
  origin into `.hrt` — deliberately not opened here (S26 owns that decision).
- **No `/play/<hash>`.** The link points at a manifest in the working tree, not at a
  published release. Serving a release by id needs the server, and that is S29.
- **No search, no tags, no Upcoming.** Two games do not need a filter.
- **The markdown subset is a subset.** Headings, fenced code, pipe tables, lists,
  `code`, **bold**. No links, no images, no nested lists, no blockquotes. It renders the
  README template and stops.
- **Chrome only**, as in chapter 128. Safari on iOS is the browser most likely to differ.
- **The index is baked by hand.** Nothing re-bakes it on a manifest edit — the test only
  catches it afterwards. A Studio button (`cmd::run("collection.index", …)` is already
  registered) would close that, and no workspace calls it yet.
- **`build-web/assets/` duplicates what is already inside `demo.data`.** Half a megabyte,
  because the page and the wasm module read through different doors.

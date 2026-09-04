# Chapter 108 — A byte is not a character

> Code: `src/engine/text/utf8.hpp` · `src/engine/text/font.{hpp,cpp}` ·
> `src/engine/renderer2d.cpp` (both text paths) · `src/engine/hub/hub.{hpp,cpp}`
> (`next_action`) · `src/games/hub/hub_panel.{hpp,cpp}` ·
> `src/games/studio_shell/studio_shell_scene.cpp` · `src/main.cpp` (`--bench-ui`) ·
> Tests: `tests/test_font.cpp`, `tests/test_hub.cpp`, `tests/test_shell_golden.cpp`

## Tóm tắt (VI)

Chương này bắt đầu từ một ảnh chụp màn hình: dòng gợi ý trong Studio hiện
`publish???dev` thay vì `publish→dev`, và chữ trông thô hơn hẳn các màn khác.

Ba chẩn đoán ban đầu đều sai, và điều đó mới là bài học:

1. `???` **không phải** thiếu glyph mũi tên. Hàm `index_of(char)` ép **mọi byte**
   ngoài ASCII in được thành `'?'`. Mũi tên `→` là **ba** byte UTF-8, nên ra đúng
   **ba** dấu hỏi. Nguyên nhân là engine chưa bao giờ giải mã UTF-8 — nó đi qua
   chuỗi từng *byte* một và gọi mỗi byte là một *ký tự*. Cùng lý do đó khiến
   engine không thể vẽ nổi tiếng Việt — thứ ngôn ngữ mà chính tài liệu gốc của
   dự án đang dùng.
2. Chữ thô **không phải** lỗi baseline. Đo bảng `hhea`/`OS2` của Inter ở cỡ 20px
   cho thấy đỉnh chữ hoa nằm **dưới** mép trên line box 4px — không có gì bị cắt.
   Thủ phạm: `--shell` và `--hub-ui` là **hai scene windowed duy nhất** quên đặt
   `cfg.supersample`, nên khung hình được rasterize ở cỡ logic rồi để SDL kéo giãn
   nội suy lên màn HiDPI. Chữ bị *phóng to*, không phải bị *vẽ hỏng*.
3. Studio trông như "in text ra màn hình" **không phải** vì thư viện thiếu widget.
   Hai màn đó đơn giản là **chưa bao giờ gọi `ui::Context`** và chưa bao giờ
   include `theme.hpp` — 33 literal màu viết tay giữa chúng, và toàn bộ layout Hub
   bị chép làm hai bản (kể cả chuỗi mũi tên hỏng, cũng hai lần).

Bài học chung: **sửa ở chỗ dùng chung, không vá chỗ nhìn thấy.** Vá hai dòng chứa
mũi tên sẽ khiến mọi chuỗi non-ASCII tương lai hỏng y hệt.

---

## 1. The screenshot lied, three times

Debugging from a picture is debugging from a symptom. Every one of the three
things the picture seemed to say turned out to be wrong, and each was wrong in a
different, instructive way.

**The arrow.** The obvious reading is "the font has no U+2192." It does; Inter
covers arrows. The real code was this:

```cpp
inline int index_of(char c) {
    unsigned uc = static_cast<unsigned char>(c);
    if (uc < kFirst || uc > kLast) uc = static_cast<unsigned char>('?');
    return static_cast<int>(uc) - kFirst;
}
```

`kFirst`/`kLast` are 32 and 126. `→` encodes as the three bytes `E2 86 92`; each
one is above 126; each becomes `'?'`. Three question marks, exactly as seen. The
glyph table was never consulted, because the loop never asked for a *character* —
it asked three times for a *byte*.

**The thick text.** The plausible reading is a baseline or clipping error. Reading
Inter's `hhea` and `OS/2` tables and reproducing what `stbtt_ScaleForPixelHeight`
computes at 20px gives `ascent ≈ 16px` and `capHeight ≈ 12px`, so the top of a
capital sits at `y + 4` — four pixels *inside* the line box. Nothing clips. What
actually happened was upstream of the renderer entirely: fifteen windowed scenes
set `cfg.supersample = kAA`, and the two Studio scenes did not. Their framebuffer
was rasterized at logical size and stretched, with linear filtering, onto a HiDPI
backbuffer. The text was enlarged, not mis-drawn.

**The "printed text" look.** The tempting reading is that the widget library is
too thin. It is thin, but that was not the cause: `studio_shell` and `hub_scene`
never called it. Nine other scenes do, which is why they look like software and
these two looked like a log file.

The general lesson is uncomfortable and worth stating plainly: a screenshot tells
you what is wrong, never why. Each of these took a different instrument to settle
— a hex dump of the string, the font's own metric tables, and a grep for who
includes what.

## 2. Decoding UTF-8, and why the error path is the interesting part

The happy path of a UTF-8 decoder is a table lookup and a shift. The design lives
entirely in what it does with bad input.

```cpp
if      ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1Fu; lowest = 0x80; }
else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0Fu; lowest = 0x800; }
else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07u; lowest = 0x10000; }
else                          { return bad(); }
```

Three rules, each of which exists because of a specific failure:

**Malformed input consumes exactly one byte.** `bad()` advances the pointer by one
and returns U+FFFD. It would be tidier to skip the whole malformed run, and it
would be wrong twice: a decoder that consumes zero bytes on error loops forever,
and one that consumes the run lets a single corrupt byte swallow the valid text
behind it. One byte guarantees progress and localizes the damage.

**Overlong encodings are rejected, not normalized.** `/` can be spelled `2F`, or —
illegally — as `C0 AF`. Both would decode to U+002F if you let them. That is the
classic path-traversal bypass: the filter checks one spelling, the filesystem
accepts the other. `lowest` is the smallest value legal at each length, and
anything below it is refused.

**Surrogate halves are rejected.** U+D800–DFFF are not scalar values; they exist
only inside UTF-16. Accepting them here means emitting something no valid UTF-8
consumer downstream would accept.

Every one of these has a test, and each test names the failure it prevents rather
than the bytes it checks.

## 3. Caching by code point instead of indexing by byte

The old cache was an array of 95 entries indexed by `char - 32`. That shape *is*
the bug: it cannot represent a code point outside ASCII, so the lookup function
had to lie to fit. The new cache is a map keyed by `char32_t`.

Two details carried real risk.

**Printable ASCII is still rasterized eagerly.** It is nearly every string, so
doing it in one pass on first use of a size keeps the cost exactly where it has
always been. Everything else is rasterized on first sight.

**Coverage pointers must survive a rehash.** Each cached glyph's `Glyph::cov`
points into a `std::vector<uint8_t>` stored inside the map. When the map grows it
*moves* those entries — and moving a `std::vector` transfers its heap buffer
rather than reallocating, so the pointer stays valid. That is the whole argument,
and it is exactly the kind of argument that is right until someone swaps the
container for one with small-buffer optimization. So it is a test, not a comment:
take `'A'`'s coverage pointer, pull in five hundred fresh code points to force
several rehashes, and assert the pointer and its bytes are unchanged. A stale
`cov` would produce garbage rarely, and only on large glyph sets — the worst kind
of bug to find later.

**Missing glyphs draw a hollow box.** The old code substituted `'?'`, which is
indistinguishable from a question mark the author typed. A box is not. Writing the
test for it caught something too: U+E000 was a bad choice of "absent" code point,
because fonts often *do* map the private-use area. U+4E2D (中), which a Latin face
genuinely lacks, is the honest test.

## 4. One decision, two presentations

`recommend()` returned an English sentence: `"promote: development -> preview"`.
That is fine for a terminal and useless to a window, which needs to know *which*
of four buttons is the primary one. The window could parse the sentence, or
re-derive the pipeline state itself — and then quietly disagree with the CLI the
next time either changed.

So the decision became a value:

```cpp
enum class Next { Fix, Publish, PromotePreview, PromoteProduction, InSync };
Next next_action(const HubView& v);
```

and `recommend()` became a formatter over it. This is the same principle the
project already applies to `hub_lines` — one brain, many renderings — pushed one
step further: not just one *text* shared by the CLI and the window, but one
*decision* shared by the sentence and the highlighted button. `test_hub` walks all
five states and asserts both agree, which is what keeps them from drifting the
next time one is edited.

The panel itself follows: `hubui::draw_hub_panel` is the one Hub rendering, called
by `--hub-ui` and by the Studio's Hub tab, so the layout that used to exist in two
copies now exists in one.

## 5. Colour is not a status

Each channel card shows a coloured rail *and* a word: `in sync`, `behind`,
`MISSING`, `unset`. Neither alone is enough. Colour alone excludes anyone who
cannot separate those hues, and it also forces a legend. A word alone makes the
eye read every row to find the one that matters. Together, the shape of the screen
is scannable and the meaning is unambiguous.

The same reasoning puts exactly one accent-filled button on the screen: whichever
step `next_action` names. If everything can be the primary action, nothing is.

## 6. Measuring before growing

Every pixel here is written by our own code, and supersampling multiplies that
work by four. `--bench-ui` renders the Studio headless and reports median and p95
frame time, because S2 is about to add layout, clipping, overlays and a dozen
widgets to this exact path.

Getting a number worth quoting took three corrections, and the third is the one
that matters most.

**The first measurement was a lucky sample.** It reported 4.63 ms at ss=2; repeated
runs gave 11–18 ms. The timed loop included the first frames, which pay for
first-touch page faults across a freshly allocated 14 MB framebuffer and for
rasterizing each type-scale size once. Real costs — but *start-up*, not steady
state. Twenty warm-up frames before timing removed that.

**Debug is not the shipping cost.** The two builds are four to five times apart, so
`--bench-ui` now prints which one it is. Quoting the wrong one is how a budget
quietly stops meaning anything.

**This laptop is not a benchmark rig.** Even warmed up, medians move by roughly 2×
between runs on an idle machine — thermal state and which core the scheduler picks
dominate. So the honest output is a range, and the honest use of it is *relative*:

| build | ss | physical px | median across 5 runs |
|---|---|---|---|
| Release | 1 | 1280×720 | 0.6 – 1.2 ms |
| **Release** | **2** | **2560×1440** | **2.4 – 4.4 ms** |
| Debug | 1 | 1280×720 | 1.2 – 5.7 ms |
| Debug | 2 | 2560×1440 | 11 – 18 ms |

Two ratios survive the noise, and they are what the number is actually for:

- **ss=2 costs about 4× ss=1** — exactly the pixel count, which says this workload
  is fill-bound, not logic-bound. Optimizing the widget code would buy nothing;
  drawing fewer pixels would.
- **Debug costs about 4–5× Release.** A developer on an unoptimized build sees the
  Studio over budget and will wrongly suspect the UI code.

The shipped configuration uses roughly a third of an 8 ms budget. The right way to
use this in S2 is not to compare against "2.4 ms" — it is to re-run `--bench-ui`
on the same machine in the same sitting, before and after, and watch the ratio.

## 7. Testing a window without opening one

`test_shell_golden` drives the entire Studio shell with no window at all. A
`Scene` needs only an `engine::Context`, and a `Renderer2D` writes into whatever
`platform::Framebuffer` the caller owns — so the "graphical" layer is as testable
as any other, and always was.

It asserts structure, not a pixel hash, for the reason `test_ui_golden` already
documents: analytic anti-aliasing rounds differently across compilers and
architectures, so a checksum is a portability trap rather than a regression test.

One assertion in it is worth copying elsewhere: the test checks that the project
fixture is readable **before** it renders anything. Without that line the entire
test passes just as happily against the "cannot read this project" error screen,
which also has a nav rail, also has anti-aliased text, and also has exactly one
primary button. A test that cannot fail on the wrong screen is not testing the
right one.

## What is verified, and what is not

Verified, by running it:

- The full suite, 62 tests, green — including the new UTF-8, tofu, cache-stability
  and `next_action` cases.
- Both `→` and `ế` render as one glyph each, in the AA path and in measurement.
  The Studio and the Guide tab were rendered offscreen at the real 1280×720×2 and
  inspected as images: arrows, em dashes, en dashes, `×` and `…` all appear.
- The Emscripten build still links and produces `demo.js` + `demo.wasm`.
- The frame-time numbers in §6 are measured in both build types across five runs.

Not verified here:

- **The absolute frame times are not reliable to better than ~2×.** They come from
  one arm64 laptop whose scheduler and thermal state move the median between runs.
  The *ratios* in §6 are stable; the point values are not, and should not be
  quoted as a spec.
- **Nothing here measures the web build**, where the same fill runs in WASM and
  where `kAA` is already forced to 1 for precisely this cost reason.
- **The window itself was never opened for this chapter.** Screen capture is
  unavailable in this environment, so every visual claim rests on the offscreen
  render, which uses the identical scene, renderer, framebuffer size and
  supersample factor — but not SDL's present path. If the presented window ever
  disagrees with the offscreen render, the difference is in `present()`, and this
  chapter would not have caught it.
- **No non-Latin script beyond Vietnamese and arrows was exercised.** The decoder
  handles four-byte sequences and there is a test for one, but no CJK face is
  bundled, so shaping-free rendering of such text is untested in practice.
- **No shaping, no kerning, no bidi.** Advances are summed per glyph. That is
  correct for the Latin UI text here and wrong for Arabic, Devanagari, or any
  script needing reordering or contextual forms. Naming the ceiling: this is a
  code-point renderer, not a text shaper.

# Chapter 118 — The window, actually opened

> Code: `src/platform/backend_sdl.cpp` · `src/engine/assets.cpp` ·
> `src/games/farm/farm_scene.cpp` · `web/shell.html` · `CMakeLists.txt` ·
> `.github/workflows/ci.yml`

## Tóm tắt (VI)

Suốt 17 chương, mọi báo cáo đều có dòng "**web build xanh**". Nó chỉ có nghĩa là
**link thành công**. Chương này mở trang đó trong một trình duyệt thật lần đầu tiên —
và nó **không chạy**: canvas đen, `SDL_CreateRenderer failed: Couldn't find matching
render driver`, một dòng trên console không ai đọc.

Năm lỗi thật, không cái nào bắt được bằng cách biên dịch:

1. **Không có renderer tăng tốc ⇒ chết hẳn.** Mọi thứ phía trên platform layer đã vẽ
   vào framebuffer CPU rồi; việc duy nhất của renderer là dán **một** quad có texture
   lên màn hình — phần mềm làm được. Nay có fallback, và **tên driver được in ra**.
2. **`resizable` là khái niệm của desktop.** Trên web, canvas là một element do trang
   định cỡ; xin `SDL_WINDOW_RESIZABLE` khiến SDL lấy cỡ CSS hiện tại thay vì cỡ được
   yêu cầu → Studio ra **canvas 3×3**, tức là vô hình.
3. **Bundle publish kèm theo máy của người phát triển.** `--preload-file assets@assets`
   đóng gói cả `saves/`, `releases/`, `channels/` — ba thư mục **đã gitignore chính vì
   chúng là state cục bộ**. Nghĩa là `saves/device.id` (mã định danh thêm ở chương 117)
   được **phát cho mọi trình duyệt**: hai người lạ dùng chung một tài khoản guest, một
   nông trại, một kho đồ. Kèm theo là file save, con trỏ channel, và `releases/audit.log`.
4. **Web không có trí nhớ.** MEMFS chết theo tab. Save chỉ *trông như* còn vì bản trên
   mây đang âm thầm gánh thay.
5. **`FS.syncfs` chồng nhau thì mất dữ liệu.** Một lần save đã là nhiều lần ghi; gọi
   syncfs mỗi lần ghi thì các lần gọi **đan vào nhau**, và `slot1.sav` quay lại **0 byte**.

**Cách chứng minh (không đoán):** chặn mọi request `/v1/` rồi tải lại — farm vẫn tiếp
tục đúng ngày đã lưu, chip ghi "offline". Thế giới đó **lấy từ đĩa**.

Và lần đầu tiên có **ảnh chụp thật**: cờ vua, farm, và Studio shell chạy trong Chrome.

---

## What "web build green" was worth

The gate was real, and it was measuring the wrong thing:

```sh
emcmake cmake -B build-web && cmake --build build-web --target demo
```

That is a link. It proves no symbol is missing. It says nothing about whether the
program starts, and for seventeen chapters it did not.

The first look at the page:

```
status: Downloading data... (1710695/1710695)
canvas: black
log:    platform: SDL_CreateRenderer failed: Couldn't find matching render driver
```

The message had been there the whole time. Nobody was reading that console, because
nobody was opening that page.

## Five bugs a compiler cannot see

### 1. No accelerated renderer, no program

```cpp
g_renderer = SDL_CreateRenderer(g_window, -1,
                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
if (!g_renderer) return false;      // ...and that was the end of it
```

`SDL_RENDERER_ACCELERATED` on the web means WebGL. A browser with it disabled, a
machine with no GPU, a headless run — all dead ends.

This project draws **every pixel itself**, into a CPU framebuffer. The renderer's
entire job is to put one textured quad on the screen. Software does that perfectly
well, and slightly worse: SDL's software renderer ignores `SDL_HINT_RENDER_SCALE_QUALITY`,
so a supersampled framebuffer is downsampled nearest rather than linear. Worth knowing,
which is why the chosen driver is now printed:

```
platform: no accelerated renderer (Couldn't find matching render driver); using software
platform: renderer 'software' (software)
```

An aliased screenshot and a smooth one are different evidence, and that line is the
difference between them.

### 2. `resizable` is a desktop word

`--shell` asks for `cfg.resizable = true` — "a workspace should fit the screen it is
on". On the web, SDL reads that as "take your size from the canvas element", and the
canvas had no size yet. The Studio came up **3×3 pixels**.

The fix is not in the Studio. The Studio's request is right; what it *means* differs
per platform, and translating that is what the platform seam is for:

```cpp
#ifdef __EMSCRIPTEN__
    const bool resizable = false;   // the page sizes the canvas; let it
#else
    const bool resizable = cfg.resizable;
#endif
```

### 3. The bundle was shipping this machine

This is the serious one.

```
--preload-file=${CMAKE_SOURCE_DIR}/assets@assets
```

packs *everything* under `assets/`. Three of those directories are not content:

```
/assets/saves/device.id            <- the identity token from chapter 117
/assets/saves/farm/slot1.sav
/assets/saves/farm/slot1.sync
/assets/channels/{development,preview,production}
/assets/releases/<hash>/package.txt
/assets/releases/audit.log         <- operator actions and their stated reasons
```

Eleven files. They are in `.gitignore` precisely because they are this machine's
state; the packager had no way to know that.

The consequence is not bloat. `device.id` decides **which player you are**. Every
browser that loaded the page read the same preloaded id, handed it to `auth().guest()`,
and got back the same account — two strangers sharing one farm.

It was found by measuring, not by reading. Two "fresh installations" (IndexedDB wiped
in between) produced **byte-identical** device ids:

```
run1 device.id: 123368817436c32c7b437a96ea6e3111
run2 device.id: 123368817436c32c7b437a96ea6e3111
IDENTICAL — the device id is not unique
```

The first hypothesis was wrong, and worth recording as such: `std::random_device` is
allowed to be a deterministic PRNG, so that looked like the answer, and a mixing step
was written for it. The actual cause was on disk the whole time — `cat assets/saves/device.id`
printed exactly that string. Three `--exclude-file` patterns later, the same test gives
two different ids and two separate accounts, and the preload manifest drops from 47
entries to 36.

(The mixing stayed. It is six lines, and the value must not repeat.)

### 4. The web had no memory

MEMFS lives in the tab and dies with it. Everything under `saves/` — the game's save,
the sync bookmark, the device id — was gone on every reload.

Cloud save was hiding it. Reload, and the farm came back on the right day, so it
looked like it worked; what was actually happening was `decide_sync` seeing no local
save, pulling the cloud copy, and quietly standing in for a filesystem. Take the
network away and there was nothing there.

The page mounts the one directory that is the player's:

```js
preRun: [function () {
  FS.mkdirTree('/assets/saves');
  FS.mount(IDBFS, {}, '/assets/saves');
  addRunDependency('idbfs');
  FS.syncfs(true, function () { removeRunDependency('idbfs'); });
}]
```

`addRunDependency` is load-bearing, not decoration. Without it `main()` starts against
an empty directory, concludes there is no save, and the real one arrives a moment later
with nothing looking at it.

Writes flush through `assets::`, which is already the single door for file I/O — so one
place covers the save, the bookmark, the device id, and whatever is added next, with no
caller remembering to.

### 5. Overlapping `syncfs` calls lose data

The first version called `FS.syncfs(false, …)` on every write. One save is already
several writes: the file, the autosave that is cleared with it, then the bookmark when
the upload returns. Concurrent syncfs calls do not queue — they interleave, and what
reaches IndexedDB is whichever snapshot the last one happened to take.

Measured: `slot1.sav` came back **zero bytes** on the next load.

```cpp
EM_ASM({
    if (Module.__fsSyncing) { Module.__fsSyncAgain = 1; return; }
    Module.__fsSyncing = 1;
    var again = function () {
        FS.syncfs(false, function (err) {
            if (Module.__fsSyncAgain) { Module.__fsSyncAgain = 0; again(); }
            else                      { Module.__fsSyncing = 0; }
        });
    };
    again();
});
```

Serialised, and coalesced: a burst of writes becomes one sync, followed by one more if
anything changed while it ran.

## Proving it came off disk

Persistence is easy to *appear* to have when a cloud save is standing behind it. The
test has to remove the understudy:

```js
await send('Network.setBlockedURLs', { urls: ['*/v1/*'] });
await send('Page.navigate', { url });
```

The page and the WASM still load from the same server; the backend is unreachable. The
farm resumed **on the saved day**, with the cloud chip reading `offline`, and
`FS.readFile('/assets/saves/farm/slot1.sav').length` was 238 rather than 0.

That is the whole argument, and it is not available from a screenshot of a working day.

## The harness

No browser-driving tool was available — the extension was not connected and the
devtools MCP server could not attach to its own profile. Headless Chrome's
`--screenshot` worked and showed a black canvas; `--dump-dom` produced nothing.

What worked was smaller than any of them: Chrome with `--remote-debugging-port`, and
about sixty lines of Node using its **built-in `WebSocket`** (Node 22) to speak CDP
directly. No dependencies, no install:

```js
const list = await (await fetch('http://127.0.0.1:9333/json/list')).json();
const ws = new WebSocket(list.find(t => t.type === 'page').webSocketDebuggerUrl);
await send('Page.navigate', { url });
await send('Input.dispatchKeyEvent', { type: 'keyDown', code: 'F5', key: 'F5', ... });
const r = await send('Page.captureScreenshot', { format: 'png' });
```

`Runtime.consoleAPICalled` and `Log.entryAdded` are how the SDL error was read in the
first place. `Network.setCacheDisabled` matters more than it sounds: two rounds of
"the fix did nothing" were the browser serving a cached `demo.js`.

The server for all of it is the BaaS itself — `baas --static build-web` — so the page
and the API share one origin and the SDK's relative base URL resolves. That is the
deployment the web build was designed for, tested for the first time here.

## What is now known to work in a browser

- **Chess, the farm and the Studio shell all render.** Every pixel is drawn by this
  project's own rasterizer into a CPU framebuffer, presented through one textured quad.
- **The farm reaches the backend from WASM**: guest sign-in, remote config, live
  events, and the cloud-save reconciliation from chapter 117 — the chip reads
  `cloud empty`, then `cloud v1` after a save.
- **Keys reach the game.** F5 pressed through CDP saved and uploaded.
- **A save survives a reload, with the network blocked.**
- **Two fresh installations are two different players.**

## Ceilings

- **Software rendering only, in this harness.** `--disable-gpu` was used throughout, so
  every screenshot is the software path. WebGL was never exercised, and neither was the
  linear downsample that supersampling wants.
- **Nothing was clicked.** Keys were dispatched; the mouse was not. The Studio was
  photographed, not operated.
- **One browser.** Chrome 152, headless, on macOS. No Firefox, no Safari, no mobile, no
  touch — the touch controls in the plan are still unwritten.
- **The web job in CI has never run**, like the rest of `ci.yml`. It links the bundle
  and greps the preload manifest for local-state paths; it does not open a page.
- **`--exclude-file` is a list that has to be maintained.** A fourth gitignored
  directory under `assets/` would ship. The CI grep is the backstop, and it only knows
  the three names.
- **Native ↔ web cloud save is still unproven.** Two installations are two guests by
  design, so sharing a farm between a desktop and a browser needs a real account, and
  there is no UI for one.

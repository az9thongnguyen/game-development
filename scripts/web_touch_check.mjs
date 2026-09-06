#!/usr/bin/env node
// =============================================================================
//  scripts/web_touch_check.mjs  —  does a real finger reach the game in a browser?
// =============================================================================
//  Chapter 126 gave the farm an on-screen d-pad so every verb could be reached by
//  thumb. Chapter 118 put the build in a browser. Nothing had ever checked that the
//  two meet — and chapter 123 is the reason that matters: the web build had NO
//  keyboard for three chapters, and it survived every browser check because every
//  check used the mouse. "SDL synthesizes a mouse from a finger" is a sentence from
//  a manual, not evidence.
//
//  So this drives Chrome over CDP with touch emulation on and dispatches REAL
//  Input.dispatchTouchEvent — not a click, not a mousedown — and then reads a VALUE
//  back out of the game rather than comparing pictures: the player's tile, out of the
//  save file the game itself wrote. A frame diff would prove something changed; only
//  `px` going up proves the finger walked east.
//
//  It aims using the line the game prints at startup ("farm: controls 640x360 …"), so
//  the target is where the game says it drew the button, at the size the browser
//  actually gave it. A harness that recomputed the layout would stop testing the
//  layout and start testing its own copy of it (ch. 126).
//
//  usage:  node scripts/web_touch_check.mjs [--dir build-web] [--chrome PATH] [--head]
//          [--width 390] [--height 844] [--shot page.png]
//  exit:   0 = a finger drove the game · 1 = it did not (message says which step)
// =============================================================================
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { readFile, writeFile } from 'node:fs/promises';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, extname, resolve } from 'node:path';

// ---- arguments --------------------------------------------------------------
const argv = process.argv.slice(2);
const arg = (name, dflt) => {
    const i = argv.indexOf(name);
    return i >= 0 && i + 1 < argv.length ? argv[i + 1] : dflt;
};
const DIR      = resolve(arg('--dir', 'build-web'));
const VW       = +arg('--width', 390);
const VH       = +arg('--height', 844);
const SHOT     = arg('--shot', '');            // write a PNG of the page and carry on
const HEADLESS = !argv.includes('--head');
const CHROME   = arg('--chrome', process.env.CHROME_PATH || defaultChrome());

function defaultChrome() {
    const candidates = [
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
        '/usr/bin/google-chrome',
        '/usr/bin/google-chrome-stable',
        '/usr/bin/chromium-browser',
        '/usr/bin/chromium',
    ];
    return candidates.find(existsSync) || 'google-chrome';
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
function fail(msg) { console.error('FAIL  ' + msg); process.exitCode = 1; throw new Error(msg); }
function ok(msg)   { console.log('ok    ' + msg); }

// ---- a static server, so this is one command and not three ------------------
const MIME = {
    '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
    '.data': 'application/octet-stream', '.css': 'text/css', '.json': 'application/json',
};
function serve(dir) {
    return new Promise((res) => {
        const server = createServer(async (req, rq) => {
            const path = join(dir, decodeURIComponent(req.url.split('?')[0]));
            try {
                const body = await readFile(path);
                // No caching anywhere in this harness: a stale demo.wasm makes a fixed
                // build look broken and a broken build look fixed.
                rq.writeHead(200, {
                    'Content-Type': MIME[extname(path)] || 'application/octet-stream',
                    'Cache-Control': 'no-store',
                });
                rq.end(body);
            } catch { rq.writeHead(404).end('not found'); }
        });
        server.listen(0, '127.0.0.1', () => res({ server, port: server.address().port }));
    });
}

// ---- CDP over the WebSocket Node 22 already has -----------------------------
class CDP {
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = new Map(); }
    // The PAGE target, not the browser one. /json/version hands back the browser-level
    // endpoint, which answers Target.* and Browser.* and refuses Runtime.enable — an
    // error that reads like a Chrome version problem and is really a wrong socket.
    static async attach(port) {
        for (let i = 0; i < 100; ++i) {
            try {
                const r = await fetch(`http://127.0.0.1:${port}/json/list`);
                const page = (await r.json()).find((t) => t.type === 'page');
                if (!page) { await sleep(100); continue; }
                const ws = new WebSocket(page.webSocketDebuggerUrl);
                await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
                const cdp = new CDP(ws);
                ws.onmessage = (e) => cdp.#onMessage(JSON.parse(e.data));
                return cdp;
            } catch { await sleep(100); }
        }
        fail('could not attach to Chrome on port ' + port);
    }
    #onMessage(m) {
        if (m.id && this.pending.has(m.id)) {
            const { res, rej } = this.pending.get(m.id);
            this.pending.delete(m.id);
            m.error ? rej(new Error(JSON.stringify(m.error))) : res(m.result);
        }
    }
    send(method, params = {}) {
        const id = ++this.id;
        this.ws.send(JSON.stringify({ id, method, params }));
        return new Promise((res, rej) => this.pending.set(id, { res, rej }));
    }
    async eval(expression) {
        const r = await this.send('Runtime.evaluate', {
            expression, returnByValue: true, awaitPromise: true,
        });
        if (r.exceptionDetails) throw new Error(r.exceptionDetails.text + ' :: ' + expression);
        return r.result.value;
    }
}

// ---- the touch itself -------------------------------------------------------
async function touch(cdp, x, y, holdMs) {
    const pt = [{ x, y, radiusX: 8, radiusY: 8, force: 1, id: 1 }];
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pt });
    // Hold. A d-pad is not a click — walking east is "the button is DOWN for a while",
    // which is exactly the shape the keyboard check in chapter 123 had to learn too:
    // a press and release inside one 16 ms frame is invisible to a polled input.
    // Jitter while held. A thumb on a d-pad is never still, and a hold that never moves
    // would not notice a build where the first touchMove resets the pointer.
    const step = 50;
    for (let t = 0, k = 0; t < holdMs; t += step, ++k) {
        await sleep(step);
        const j = [{ ...pt[0], x: x + (k % 2 ? 3 : -3), y: y + (k % 3 ? 2 : -2) }];
        await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: j });
    }
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await sleep(120);
}

// ---- main -------------------------------------------------------------------
const userDataDir = mkdtempSync(join(tmpdir(), 'handengine-cdp-'));
let chrome, server;
try {
    if (!existsSync(join(DIR, 'demo.html'))) fail(`no demo.html in ${DIR} — build the web target first`);
    ({ server, port: globalThis.__port } = await serve(DIR));
    const url = `http://127.0.0.1:${globalThis.__port}/demo.html?project=projects/farm.gameproject`;

    const debugPort = 9333 + (process.pid % 500);
    chrome = spawn(CHROME, [
        HEADLESS ? '--headless=new' : '--auto-open-devtools-for-tabs',
        `--remote-debugging-port=${debugPort}`,
        `--user-data-dir=${userDataDir}`,
        '--no-first-run', '--no-default-browser-check', '--disable-extensions',
        '--window-size=390,844',
        'about:blank',
    ], { stdio: 'ignore' });

    const cdp = await CDP.attach(debugPort);
    await cdp.send('Runtime.enable');
    await cdp.send('Page.enable');
    await cdp.send('Network.enable');
    await cdp.send('Network.setCacheDisabled', { cacheDisabled: true });
    // A phone, and a phone's input. Without setTouchEmulationEnabled the page reports
    // no touch support and dispatchTouchEvent is dropped on the floor.
    await cdp.send('Emulation.setDeviceMetricsOverride', {
        width: VW, height: VH, deviceScaleFactor: 3, mobile: true,
    });
    await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 1 });

    await cdp.send('Page.navigate', { url });

    // ---- 1. the build actually starts -------------------------------------
    let running = false;
    for (let i = 0; i < 300 && !running; ++i) {
        await sleep(100);
        try { running = await cdp.eval(`document.getElementById('status')?.textContent === 'running'`); }
        catch { /* the document is still being replaced */ }
    }
    if (!running) fail('the page never reached "running" (WASM did not start)');
    ok('the WASM build started');

    // ---- 2. the page is a page, not a debug shell -------------------------
    const page = await cdp.eval(`(() => {
        const c = document.getElementById('canvas');
        const s = getComputedStyle(c);
        const r = c.getBoundingClientRect();
        const st = document.getElementById('stage').getBoundingClientRect();
        return { touchAction: s.touchAction, userSelect: s.userSelect || s.webkitUserSelect,
                 logShown: getComputedStyle(document.getElementById('log')).display !== 'none',
                 rect: { x: r.x, y: r.y, w: r.width, h: r.height },
                 stage: { w: st.width, h: st.height },
                 backing: { w: c.width, h: c.height },
                 docScrolls: document.documentElement.scrollHeight > window.innerHeight + 1 };
    })()`);
    // A DECLARATION check, not a behaviour one — and the difference is measured, not
    // assumed. Running this whole file with the guard flipped to `auto` still passes,
    // and a separate probe found why: CDP's Input.dispatchTouchEvent ignores
    // touch-action entirely (a moving drag scrolled a scrollable page by the same 110 px
    // with `none` and with `auto`). So emulated touch CANNOT exercise the browser's
    // gesture arbitration, and this line records intent. What IS proved behaviourally is
    // the assertion below it: the page does not scroll, so there is no scroll to steal.
    if (page.touchAction !== 'none') {
        if (!process.env.WEB_TOUCH_ALLOW_DEFAULT)
            fail(`canvas touch-action is "${page.touchAction}", not "none"`);
        console.log('warn  touch-action is "' + page.touchAction + '" (guard deliberately off)');
    }
    if (page.logShown) fail('the runtime log is visible by default');
    if (page.docScrolls) fail('the page scrolls: the game does not fit the viewport');
    // FITTED means it touches an edge of the stage. Asserting a width fraction was
    // wrong the moment the viewport turned landscape: a 16:9 canvas in an 844x357 stage
    // is HEIGHT-bound at 634px wide, which is correct and looked like a failure.
    if (page.rect.w < page.stage.w - 2 && page.rect.h < page.stage.h - 2)
        fail(`the canvas ${Math.round(page.rect.w)}x${Math.round(page.rect.h)} does not fill ` +
             `either axis of the ${Math.round(page.stage.w)}x${Math.round(page.stage.h)} stage`);
    // ...and the page chrome must stay chrome. A bar that grows into a third of the
    // screen is how this page was a debug console in the first place.
    if (page.stage.h < VH - 60)
        fail(`the stage is only ${Math.round(page.stage.h)}px of a ${VH}px viewport`);
    // THE assertion the first version of this file did not have. A canvas whose display
    // box has a different shape from its drawing buffer is stretched — and every other
    // check here still passes, because SDL maps the pointer through the same box. It
    // took a screenshot to see it (390x720 shown from 1280x720 drawn), so it is written
    // down as a number now.
    const shown = page.rect.w / page.rect.h, drawn = page.backing.w / page.backing.h;
    if (Math.abs(shown - drawn) > 0.02)
        fail(`the canvas is stretched: shown ${shown.toFixed(3)} vs drawn ${drawn.toFixed(3)}`);
    if (SHOT) {
        // A frame to LOOK at. Every assertion here is about numbers; whether the page
        // reads as a game rather than as a debug console is a different question, and
        // only an eye answers it (ch. 126, ch. 127).
        const { data } = await cdp.send('Page.captureScreenshot', { format: 'png' });
        await writeFile(SHOT, Buffer.from(data, 'base64'));
        ok('screenshot -> ' + SHOT);
    }
    ok(`canvas ${Math.round(page.rect.w)}x${Math.round(page.rect.h)} css in a ` +
       `${Math.round(page.stage.w)}x${Math.round(page.stage.h)} stage, from ${page.backing.w}x${page.backing.h} ` +
       `backing, ratio kept, no scroll`);

    // ---- 3. ask the GAME where its buttons are ----------------------------
    const line = await cdp.eval(`(document.getElementById('log').textContent.match(/^farm: controls .*$/m) || [''])[0]`);
    if (!line) fail('the farm never printed its control layout');
    const dims = line.match(/controls (\d+)x(\d+)/);
    const boxOf = (name) => {
        // Split, do not regex. A built RegExp needs its backslashes escaped twice on the
        // way through the source, and the version that got it wrong still "found" the
        // line and then reported the field missing — a failure that reads like the game
        // stopped printing it.
        const tok = line.split(/\s+/).find((t) => t.startsWith(name + '='));
        if (!tok) fail(`the control line has no ${name}: ${line}`);
        const [x, y, w, h] = tok.slice(name.length + 1).split(',').map(Number);
        if ([x, y, w, h].some(Number.isNaN)) fail(`unreadable ${name} box: ${tok}`);
        return { x, y, w, h };
    };
    const LOGW = +dims[1], LOGH = +dims[2];
    const right = boxOf('right'), save = boxOf('save');
    if (right.w === 0) fail('the d-pad is not laid out at this size — nothing to touch');
    // logical -> CSS. The canvas keeps its aspect ratio, so one ratio does both axes,
    // but computing each from its own extent survives a page that letterboxes.
    const css = (b) => ({
        x: page.rect.x + (b.x + b.w / 2) * page.rect.w / LOGW,
        y: page.rect.y + (b.y + b.h / 2) * page.rect.h / LOGH,
    });
    ok(`the game says: right=${JSON.stringify(right)} save=${JSON.stringify(save)} in ${LOGW}x${LOGH}`);

    // ---- 4. the value we will compare -------------------------------------
    // The FS handle the page mounted IDBFS with. Reading the save is how a value gets
    // out of the process; a screenshot would only say the picture moved.
    const readSave = `(() => { try {
        const fs = (typeof Module !== 'undefined' && Module.FS) || window.FS || window.__FS;
        if (!fs) return 'NOFS';
        return fs.readFile('/assets/saves/farm/slot1.sav', { encoding: 'utf8' });
    } catch (e) { return 'ERR ' + e; } })()`;

    const s0 = css(save);
    await touch(cdp, s0.x, s0.y, 150);           // tap Save
    await sleep(400);
    const before = await cdp.eval(readSave);
    if (before === 'NOFS') fail('the page does not expose the emscripten filesystem');
    if (typeof before !== 'string' || before.startsWith('ERR'))
        fail(`the save button did not write a save: ${before}`);
    const pxOf = (t) => { const m = t.match(/^var px (-?\d+)/m); return m ? +m[1] : null; };
    const px0 = pxOf(before);
    if (px0 === null) fail('the save has no player position');
    ok(`a touch on Save wrote a save (px=${px0}) — touch reaches the game`);

    // ---- 5. hold the d-pad, and check the player MOVED --------------------
    const r0 = css(right);
    await touch(cdp, r0.x, r0.y, 700);
    await touch(cdp, s0.x, s0.y, 150);
    await sleep(400);
    const after = await cdp.eval(readSave);
    const px1 = pxOf(after);
    if (px1 === null) fail('the second save has no player position');
    if (px1 <= px0) fail(`holding the d-pad's east button did not move the player (px ${px0} -> ${px1})`);
    ok(`held the east button: px ${px0} -> ${px1}`);

    console.log('\nweb touch: PASS — a real finger walked the farm');
} finally {
    if (chrome) chrome.kill();
    if (server) server.close();
    try { rmSync(userDataDir, { recursive: true, force: true }); } catch { /* best effort */ }
}

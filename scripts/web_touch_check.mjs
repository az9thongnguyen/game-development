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
// A THIRD toolchain. With --cmd, this file stops being a touch check and becomes the
// smallest possible one: run a headless command in the WebAssembly build and read
// what it printed. It exists for `creature.verify` — re-playing, in a browser, a
// battle that a native compiler on a different instruction set recorded. Chapter 138
// proved that claim across x86-64/gcc and arm64/clang, which are the two machines CI
// happens to own; wasm is the one this project actually ships on.
const CMD      = arg('--cmd', '');
const CMDARGS  = arg('--args', '');
const EXPECT   = arg('--expect', '');

// WHICH GAME. Two now (chapter 137), and the difference between them is four
// strings: the manifest, the prefix of the line the game prints, the save it writes,
// and how to read a position out of that save. Everything else — the layout probe,
// the touch dispatch, the retry — is the same check, which is the point of both games
// printing the same kind of line.
const GAMES = {
    farm: {
        project: 'projects/farm.gameproject',
        prefix:  'farm',
        save:    '/assets/saves/farm/slot1.sav',
        pos:     (t) => { const m = t.match(/^var px (-?\d+)/m); return m ? +m[1] : null; },
        walked:  'a real finger walked the farm',
    },
    creatures: {
        project: 'projects/creatures.gameproject',
        prefix:  'creatures',
        save:    '/assets/saves/creatures/slot1.sav',
        pos:     (t) => { const m = t.match(/^pos (-?\d+) /m); return m ? +m[1] : null; },
        walked:  'a real finger walked into the grass and out of a fight',
        // The farm's proof does not transfer, and finding that out was the point of
        // running it. There, you hold east and press Save. Here, holding east walks
        // you into long grass, something jumps out, and the SAVE BUTTON IS GONE —
        // during a battle the screen has a menu instead. The game was working
        // perfectly and the check was asking it the wrong question.
        //
        // So the creature game is proved by finishing what it starts: walk until the
        // battle screen announces itself, RUN from the fight, acknowledge it, and
        // only then save. That is a stronger claim than the farm's, because touch had
        // to reach the overworld, the encounter, the battle menu and the end screen.
        drive: true,
    },
};
const GAME = GAMES[arg('--game', 'farm')];
if (!GAME) fail(`--game must be one of ${Object.keys(GAMES).join(', ')}`);
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
    const url = CMD
        ? `http://127.0.0.1:${globalThis.__port}/demo.html?cmd=${encodeURIComponent(CMD)}` +
          `&args=${encodeURIComponent(CMDARGS)}`
        : `http://127.0.0.1:${globalThis.__port}/demo.html?project=${GAME.project}`;

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

    // ---- 1b. --cmd: a headless verb, and what it printed -------------------
    // Deliberately BEFORE the page checks below: a command run has no canvas, no
    // touch and no game, and asserting the shape of a stage it never built would
    // fail for reasons that say nothing about the answer.
    if (CMD) {
        let out = '';
        for (let i = 0; i < 200; ++i) {
            out = await cdp.eval(`document.getElementById('log').textContent`);
            if (out && out.trim()) break;
            await sleep(100);
        }
        if (!out || !out.trim()) fail(`--cmd ${CMD} printed nothing`);
        console.log(out.trim().split('\n').map((l) => '      ' + l).join('\n'));
        if (EXPECT && out.indexOf(EXPECT) < 0)
            fail(`--cmd ${CMD} did not print ${JSON.stringify(EXPECT)}`);
        ok(`WebAssembly ran ${CMD} ${CMDARGS}`);
        console.log(`PASS  a third toolchain agrees: ${CMD} ${CMDARGS}`);
        process.exitCode = 0;
        throw { done: true };
    }

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
    const line = await cdp.eval(
        `(document.getElementById('log').textContent.match(/^${GAME.prefix}: controls .*$/m) || [''])[0]`);
    if (!line) fail(`the ${GAME.prefix} never printed its control layout`);
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
        return fs.readFile('${GAME.save}', { encoding: 'utf8' });
    } catch (e) { return 'ERR ' + e; } })()`;

    const s0 = css(save);
    await touch(cdp, s0.x, s0.y, 150);           // tap Save
    await sleep(400);
    const before = await cdp.eval(readSave);
    if (before === 'NOFS') fail('the page does not expose the emscripten filesystem');
    if (typeof before !== 'string' || before.startsWith('ERR'))
        fail(`the save button did not write a save: ${before}`);
    const pxOf = GAME.pos;
    const px0 = pxOf(before);
    if (px0 === null) fail('the save has no player position');
    ok(`a touch on Save wrote a save (px=${px0}) — touch reaches the game`);

    // ---- 5. hold the d-pad, and check the player MOVED --------------------
    // Up to three holds, not one. The claim is "holding the button moves the player",
    // not "it moves within one particular 700 ms window" — and a browser that stalls
    // on a GC or a first-frame jank turns the second claim into a coin flip. It did:
    // one run in five failed here with px unchanged, and passed on a plain retry.
    // Rounds are REPORTED, so a real regression that needs three of them is visible
    // rather than absorbed.
    const r0 = css(right);
    let px1 = null;
    let rounds = 0;

    if (GAME.drive) {
        // Walk east until the game says a battle screen exists. It announces itself
        // the same way the overworld does, so the rectangles below are the renderer's
        // own rather than a second copy of the layout rule.
        let battle = '';
        for (; rounds < 4 && !battle; ++rounds) {
            await touch(cdp, r0.x, r0.y, 900);
            await sleep(200);
            battle = await cdp.eval(
                `(document.getElementById('log').textContent.match(/^creatures: battle .*$/m) || [''])[0]`);
        }
        if (!battle) fail(`walking east ${rounds} times never met anything in the long grass`);
        ok(`walked into the grass and something jumped out (${rounds} hold(s))`);

        const bbox = (name) => {
            const tok = battle.split(/\s+/).find((t) => t.startsWith(name + '='));
            if (!tok) fail(`the battle line has no ${name}: ${battle}`);
            const [x, y, w, h] = tok.slice(name.length + 1).split(',').map(Number);
            if ([x, y, w, h].some(Number.isNaN) || w === 0) fail(`unreadable ${name}: ${tok}`);
            return { x, y, w, h };
        };
        const run = css(bbox('run')), acknowledge = css(bbox('ack'));

        await touch(cdp, run.x, run.y, 140);            // Run
        await sleep(250);
        await touch(cdp, acknowledge.x, acknowledge.y, 140);   // Continue
        await sleep(250);
        await touch(cdp, s0.x, s0.y, 150);              // ...and only NOW can it save
        await sleep(400);
        const after = await cdp.eval(readSave);
        px1 = pxOf(after);
        if (px1 === null) fail('the save after the battle has no player position');
        if (px1 <= px0)
            fail(`ran from the fight but the player never moved (px ${px0} -> ${px1})`);
        ok(`ran, acknowledged, saved: px ${px0} -> ${px1}`);
    } else {
        for (; rounds < 3; ++rounds) {
            await touch(cdp, r0.x, r0.y, 700);
            await touch(cdp, s0.x, s0.y, 150);
            await sleep(400);
            const after = await cdp.eval(readSave);
            px1 = pxOf(after);
            if (px1 === null) fail('the second save has no player position');
            if (px1 > px0) break;
        }
        if (px1 <= px0)
            fail(`holding the d-pad's east button did not move the player in ${rounds} holds `
                 + `(px ${px0} -> ${px1})`);
        ok(`held the east button: px ${px0} -> ${px1}` + (rounds ? `  (${rounds + 1} holds)` : ''));
    }

    console.log(`\nweb touch: PASS — ${GAME.walked}`);
} catch (e) {
    // `--cmd` finishes early and on purpose. Everything else that lands here is a
    // real failure and must keep its stack and its exit code.
    if (!e || !e.done) throw e;
} finally {
    if (chrome) chrome.kill();
    if (server) server.close();
    try { rmSync(userDataDir, { recursive: true, force: true }); } catch { /* best effort */ }
}

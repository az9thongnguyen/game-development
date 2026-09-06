#!/usr/bin/env node
// =============================================================================
//  scripts/web_collection_check.mjs  —  can a stranger find a game and play it?
// =============================================================================
//  Chapter 128 proved a finger reaches the farm. It proved it at a URL nobody could
//  have guessed: demo.html?project=projects/farm.gameproject. This checks the half
//  that turns that into something you can send someone — a page that lists what
//  exists, and a Play button that lands in a RUNNING game.
//
//  Two things are checked that a "does the page render" test would not:
//
//    * The covers are DECODED, not merely requested. `.hrt` is parsed in the page by
//      hand, and a decoder that quietly produces nothing leaves a black rectangle
//      that looks exactly like art on a dark background. So the check reads the
//      canvas back and counts distinct colours.
//
//    * Play is FOLLOWED. A correct href is not a working link — chapter 123's
//      keyboard was wired to a canvas nobody had focused, and every check that
//      stopped short of the last step passed. This one clicks the button and waits
//      for the game to say "running".
//
//  usage:  node scripts/web_collection_check.mjs [--dir build-web] [--chrome PATH]
//          [--head] [--width 390] [--height 844] [--shot page.png]
//  exit:   0 = the collection works · 1 = it does not (the message says which step)
// =============================================================================
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { readFile, writeFile } from 'node:fs/promises';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, extname, resolve } from 'node:path';

const argv = process.argv.slice(2);
const arg = (name, dflt) => {
    const i = argv.indexOf(name);
    return i >= 0 && i + 1 < argv.length ? argv[i + 1] : dflt;
};
const DIR      = resolve(arg('--dir', 'build-web'));
const VW       = +arg('--width', 390);
const VH       = +arg('--height', 844);
const SHOT     = arg('--shot', '');
const HEADLESS = !argv.includes('--head');
const CHROME   = arg('--chrome', process.env.CHROME_PATH || defaultChrome());

function defaultChrome() {
    return [
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
        '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable',
        '/usr/bin/chromium-browser', '/usr/bin/chromium',
    ].find(existsSync) || 'google-chrome';
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
function fail(msg) { console.error('FAIL  ' + msg); process.exitCode = 1; throw new Error(msg); }
function ok(msg)   { console.log('ok    ' + msg); }

const MIME = {
    '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
    '.data': 'application/octet-stream', '.css': 'text/css', '.json': 'application/json',
    '.md': 'text/markdown', '.hrt': 'application/octet-stream',
};
function serve(dir) {
    return new Promise((res) => {
        const server = createServer(async (req, rq) => {
            const path = join(dir, decodeURIComponent(req.url.split('?')[0]));
            try {
                const body = await readFile(path);
                rq.writeHead(200, {
                    'Content-Type': MIME[extname(path)] || 'application/octet-stream',
                    'Cache-Control': 'no-store',   // a stale bundle lies in both directions
                });
                rq.end(body);
            } catch { rq.writeHead(404).end('not found'); }
        });
        server.listen(0, '127.0.0.1', () => res({ server, port: server.address().port }));
    });
}

class CDP {
    constructor(ws) { this.ws = ws; this.id = 0; this.pending = new Map(); }
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

async function tap(cdp, x, y) {
    const pt = [{ x, y, radiusX: 8, radiusY: 8, force: 1, id: 1 }];
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pt });
    await sleep(60);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await sleep(150);
}

const userDataDir = mkdtempSync(join(tmpdir(), 'handengine-collection-'));
let chrome, server;
try {
    for (const f of ['collection.html', 'collection.json', 'demo.html']) {
        if (!existsSync(join(DIR, f)))
            fail(`no ${f} in ${DIR} — build the web target first (it copies the page and the index)`);
    }
    ({ server, port: globalThis.__port } = await serve(DIR));
    const base = `http://127.0.0.1:${globalThis.__port}`;

    const debugPort = 9833 + (process.pid % 400);
    chrome = spawn(CHROME, [
        HEADLESS ? '--headless=new' : '--auto-open-devtools-for-tabs',
        `--remote-debugging-port=${debugPort}`,
        `--user-data-dir=${userDataDir}`,
        '--no-first-run', '--no-default-browser-check', '--disable-extensions',
        `--window-size=${VW},${VH}`,
        'about:blank',
    ], { stdio: 'ignore' });

    const cdp = await CDP.attach(debugPort);
    await cdp.send('Runtime.enable');
    await cdp.send('Page.enable');
    await cdp.send('Network.enable');
    await cdp.send('Network.setCacheDisabled', { cacheDisabled: true });
    await cdp.send('Emulation.setDeviceMetricsOverride', {
        width: VW, height: VH, deviceScaleFactor: 3, mobile: true,
    });
    await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 1 });

    await cdp.send('Page.navigate', { url: `${base}/collection.html` });

    // ---- 1. the list arrives -----------------------------------------------
    let games = null;
    for (let i = 0; i < 100 && games === null; ++i) {
        await sleep(100);
        try { games = await cdp.eval(`document.body.dataset.games ?? null`); } catch {}
    }
    if (games === 'error')
        fail('the page could not read collection.json: ' +
             await cdp.eval(`document.getElementById('err').textContent`));
    if (!games || +games < 2) fail(`the page listed ${games} games; expected at least 2`);
    ok(`the page listed ${games} games`);

    // ---- 2. the covers were DECODED, not just fetched -----------------------
    // Give the fetch+decode a moment; then count distinct colours in each cover.
    // A canvas that stayed blank has exactly one.
    await sleep(800);
    const covers = await cdp.eval(`(() => {
        const out = [];
        document.querySelectorAll('.shot').forEach((s) => {
            const c = s.querySelector('canvas');
            if (!c) { out.push({ painted: null, colours: 0, note: s.textContent.trim() }); return; }
            const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
            const seen = new Set();
            for (let i = 0; i < d.length; i += 4 * 37)      // a stride, not every pixel
                seen.add((d[i] << 24) | (d[i+1] << 16) | (d[i+2] << 8) | d[i+3]);
            const r = c.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
            out.push({ painted: c.dataset.painted || null, colours: seen.size, note: '',
                       // The drawing buffer against the box it is shown in. Anything but
                       // 1.0 means CSS is resampling pixel art after the page placed it.
                       bufW: c.width, bufH: c.height,
                       wantW: Math.round(r.width * dpr), wantH: Math.round(r.height * dpr) });
        });
        return out;
    })()`);
    covers.forEach((c, i) => {
        if (!c.painted) fail(`card ${i} has no decoded cover${c.note ? ' — ' + c.note : ''}`);
        // > 4 rather than > 1: a decode that produced a solid fill plus the card's
        // background would still be two, and that is not a picture.
        if (c.colours <= 4) fail(`card ${i}'s cover decoded to ${c.colours} colour(s) — blank`);
        // Found by looking at a screenshot, not by any assertion above: a fixed 320x180
        // buffer inside a 258x145 card is scaled 0.81 by CSS, which undoes every integer
        // the page computed. Same failure as chapter 128's stretched canvas, one page over.
        if (Math.abs(c.bufW - c.wantW) > 1 || Math.abs(c.bufH - c.wantH) > 1)
            fail(`card ${i}'s cover buffer is ${c.bufW}x${c.bufH} inside a ` +
                 `${c.wantW}x${c.wantH} box — CSS is resampling the pixel art`);
    });
    ok(`every cover decoded (${covers.map((c) => c.painted + '/' + c.colours + 'c').join(', ')})`);

    // ---- 3. the page fits a phone ------------------------------------------
    const shape = await cdp.eval(`({
        hScroll: document.documentElement.scrollWidth > window.innerWidth + 1,
        cards:   document.querySelectorAll('.card').length,
        plays:   document.querySelectorAll('a.play').length,
        widest:  Math.max(...[...document.querySelectorAll('.card')].map(e => e.getBoundingClientRect().width)),
    })`);
    if (shape.hScroll) fail('the page scrolls sideways on a phone');
    if (shape.widest > VW) fail(`a card is ${shape.widest}px wide on a ${VW}px screen`);
    ok(`${shape.cards} cards fit the viewport, ${shape.plays} of them playable`);

    // ---- 4. the README renders as a document, not as its source -------------
    await cdp.eval(`[...document.querySelectorAll('.card')]
        .find(c => c.querySelector('button'))?.querySelector('button').click()`);
    for (let i = 0; i < 60; ++i) {
        if (await cdp.eval(`!!document.querySelector('.card.open .readme table')`)) break;
        await sleep(100);
    }
    const readme = await cdp.eval(`(() => {
        const r = document.querySelector('.card.open .readme');
        if (!r) return null;
        return { tables: r.querySelectorAll('table').length,
                 heads:  r.querySelectorAll('h2').length,
                 lists:  r.querySelectorAll('li').length,
                 code:   r.querySelectorAll('pre').length,
                 rawHash: /(^|\\n)##\\s/.test(r.textContent) };
    })()`);
    if (!readme)          fail('the "read me" button opened nothing');
    if (!readme.tables)   fail("the README's controls table did not render as a table");
    if (readme.heads < 3) fail(`the README rendered ${readme.heads} headings`);
    if (!readme.lists)    fail('the README rendered no list items');
    if (!readme.code)     fail('the README rendered no code block');
    if (readme.rawHash)   fail('the README is showing its own markdown source');
    ok(`the README rendered (${readme.heads} headings, ${readme.tables} table, ` +
       `${readme.lists} list items, ${readme.code} code block)`);

    if (SHOT) {
        const png = await cdp.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
        await writeFile(SHOT, Buffer.from(png.data, 'base64'));
        ok('wrote ' + SHOT);
    }

    // ---- 5. Play LANDS in a running game -----------------------------------
    // The whole point of the slice, and the only step that cannot be faked by a
    // correct-looking href.
    const target = await cdp.eval(`(() => {
        const a = document.querySelector('a.play');
        const r = a.getBoundingClientRect();
        a.scrollIntoView({ block: 'center' });
        const r2 = a.getBoundingClientRect();
        return { x: r2.x + r2.width / 2, y: r2.y + r2.height / 2, href: a.getAttribute('href') };
    })()`);
    if (!/^demo\.html\?project=/.test(target.href))
        fail('the Play button does not point at the player: ' + target.href);
    await tap(cdp, target.x, target.y);

    let running = false, url = '';
    for (let i = 0; i < 400 && !running; ++i) {
        await sleep(100);
        try {
            url = await cdp.eval(`location.pathname + location.search`);
            running = await cdp.eval(`document.getElementById('status')?.textContent === 'running'`);
        } catch { /* the document is being replaced */ }
    }
    if (!url.includes('demo.html')) fail('tapping Play did not navigate (still at ' + url + ')');
    if (!running) fail('Play arrived at ' + url + ' but the game never reached "running"');
    ok('tapping Play landed in a running game (' + url + ')');

    console.log('\nPASS  a stranger can open the page, see the games, and play one.');
} finally {
    try { chrome?.kill(); } catch {}
    try { server?.close(); } catch {}
    try { rmSync(userDataDir, { recursive: true, force: true }); } catch {}
}

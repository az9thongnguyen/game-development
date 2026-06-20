#!/usr/bin/env python3
# =============================================================================
#  scripts/fetch_pieces.py  —  OFFLINE asset pipeline (not part of the engine)
# =============================================================================
#  Downloads the public-domain "Cburnett" chess piece images from Wikimedia,
#  decodes the PNGs (stdlib zlib only — no Pillow/SDL_image), area-scales them to
#  the board square size, and writes a tiny raw ".hrt" format the engine loads
#  with its own hand-written C++ loader (engine/image.cpp).
#
#  Resumable: re-running skips pieces already produced. Run:
#      python3 scripts/fetch_pieces.py
#  Output: assets/pieces/{w,b}{K,Q,R,B,N,P}.hrt  + assets/pieces/CREDITS.txt
#
#  .hrt format:  magic "HRT1" | uint32 BE width | uint32 BE height | RGBA8 rows
#  Cburnett pieces: CC-BY-SA 3.0 / GFDL / BSD (see CREDITS.txt). Attribution kept.
# =============================================================================
import hashlib
import os
import struct
import sys
import time
import urllib.request
import zlib

TARGET = 80                 # output px (matches the GUI board square)
REQ_SIZES = [120, 90, 60]   # allowed Wikimedia SVG thumb widths to try

PIECES = [
    ("wK", "Chess_klt45.svg"), ("wQ", "Chess_qlt45.svg"), ("wR", "Chess_rlt45.svg"),
    ("wB", "Chess_blt45.svg"), ("wN", "Chess_nlt45.svg"), ("wP", "Chess_plt45.svg"),
    ("bK", "Chess_kdt45.svg"), ("bQ", "Chess_qdt45.svg"), ("bR", "Chess_rdt45.svg"),
    ("bB", "Chess_bdt45.svg"), ("bN", "Chess_ndt45.svg"), ("bP", "Chess_pdt45.svg"),
]

def wikimedia_url(svg, px):
    h = hashlib.md5(svg.encode()).hexdigest()
    return (f"https://upload.wikimedia.org/wikipedia/commons/thumb/"
            f"{h[0]}/{h[0:2]}/{svg}/{px}px-{svg}.png")

def download(url, tries=4):
    last = None
    for _ in range(tries):
        try:
            req = urllib.request.Request(url, headers={
                "User-Agent": "hand-engine-asset-fetch/1.0 (educational; chess)"})
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except Exception as e:
            last = e
            time.sleep(0.8)  # back off (rate limiting)
    raise last

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)

def decode_png(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos = 8
    width = height = bitdepth = colortype = None
    idat = b""
    palette = trns = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bitdepth, colortype = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"PLTE":
            palette = chunk
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
    assert bitdepth == 8, f"unsupported bitdepth {bitdepth}"
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colortype]
    raw = zlib.decompress(idat)
    stride = width * ch
    prev = bytearray(stride)
    rgba = bytearray(width * height * 4)
    i = 0
    for y in range(height):
        ft = raw[i]; i += 1
        line = bytearray(raw[i:i + stride]); i += stride
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if ft == 1:   line[x] = (line[x] + a) & 255
            elif ft == 2: line[x] = (line[x] + b) & 255
            elif ft == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif ft == 4: line[x] = (line[x] + paeth(a, b, c)) & 255
        prev = line
        for x in range(width):
            if colortype == 6:
                r, g, bl, al = line[x*4], line[x*4+1], line[x*4+2], line[x*4+3]
            elif colortype == 2:
                r, g, bl = line[x*3], line[x*3+1], line[x*3+2]; al = 255
            elif colortype == 3:
                idx = line[x]
                r, g, bl = palette[idx*3], palette[idx*3+1], palette[idx*3+2]
                al = trns[idx] if (trns and idx < len(trns)) else 255
            elif colortype == 0:
                r = g = bl = line[x]; al = 255
            else:
                r = g = bl = line[x*2]; al = line[x*2+1]
            o = (y*width + x) * 4
            rgba[o], rgba[o+1], rgba[o+2], rgba[o+3] = r, g, bl, al
    return width, height, rgba

def area_scale(w, h, rgba, tw, th):
    out = bytearray(tw * th * 4)
    for ty in range(th):
        sy0, sy1 = ty * h // th, max(ty * h // th + 1, (ty + 1) * h // th)
        for tx in range(tw):
            sx0, sx1 = tx * w // tw, max(tx * w // tw + 1, (tx + 1) * w // tw)
            r = g = b = asum = cnt = 0
            for sy in range(sy0, sy1):
                for sx in range(sx0, sx1):
                    o = (sy * w + sx) * 4
                    a = rgba[o + 3]
                    r += rgba[o] * a; g += rgba[o+1] * a; b += rgba[o+2] * a
                    asum += a; cnt += 1
            o = (ty * tw + tx) * 4
            if asum > 0:
                out[o], out[o+1], out[o+2], out[o+3] = r//asum, g//asum, b//asum, asum//cnt
            else:
                out[o] = out[o+1] = out[o+2] = out[o+3] = 0
    return out

def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(here, "assets", "pieces")
    os.makedirs(outdir, exist_ok=True)
    for name, svg in PIECES:
        outpath = os.path.join(outdir, name + ".hrt")
        if os.path.exists(outpath):
            print(f"  {name}: skip (exists)")
            continue
        w = h = rgba = None
        last = None
        for px in REQ_SIZES:
            try:
                w, h, rgba = decode_png(download(wikimedia_url(svg, px)))
                break
            except Exception as e:
                last = e
        if rgba is None:
            print(f"FAIL {name} ({svg}): {last}", file=sys.stderr)
            return 1
        scaled = area_scale(w, h, rgba, TARGET, TARGET)
        with open(outpath, "wb") as f:
            f.write(b"HRT1" + struct.pack(">II", TARGET, TARGET) + scaled)
        print(f"  {name}: {w}x{h} -> {TARGET}x{TARGET}  ({svg})")
        time.sleep(0.3)  # be gentle with the server
    with open(os.path.join(outdir, "CREDITS.txt"), "w") as f:
        f.write("Chess piece images: the 'Cburnett' set by Colin M.L. Burnett,\n"
                "from Wikimedia Commons. Licensed CC-BY-SA 3.0 / GFDL / BSD.\n"
                "Downloaded + converted to .hrt by scripts/fetch_pieces.py.\n")
    print("done.")
    return 0

if __name__ == "__main__":
    sys.exit(main())

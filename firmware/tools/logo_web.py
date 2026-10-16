#!/usr/bin/env python3
"""Bakes the chirp! logo into firmware/include/logo_web.h for the setup web page.

The pixels come from the firmware's own logo.h (through the host harness
firmware/test/host/logo_preview.cpp, the same one logo_preview.py uses): the
logo's frame on the page's logo background (logo::WEB_BG, a light colour, so
the edge is the deep outline), as a 4-bit indexed PNG whose background slot is
transparent. The PNG is written here, byte for byte reproducible: stored
chunks, a fixed-Huffman deflate with a greedy longest-match search, no zlib
compressor (its output differs between builds). The page shows it enlarged
with `image-rendering: pixelated`.

  logo_web.py            rewrite include/logo_web.h
  logo_web.py --check    exit 1 unless include/logo_web.h is what logo.h produces
"""

import argparse
import base64
import os
import struct
import sys
import tempfile
import zlib

import logo_preview

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "include", "logo_web.h")
SCALE = 4                   # the page's enlargement: the characters' scale

# deflate (RFC 1951) tables
LEN_BASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
            99, 115, 131, 163, 195, 227, 258]
LEN_EXTRA = [0] * 8 + [1] * 4 + [2] * 4 + [3] * 4 + [4] * 4 + [5] * 4 + [0]
DIST_BASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
             1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577]
DIST_EXTRA = [0, 0, 0, 0] + [n for n in range(1, 14) for _ in (0, 1)]


class Bits:
    """LSB-first bit writer."""

    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, nbits):
        self.acc |= value << self.n
        self.n += nbits
        while self.n >= 8:
            self.out.append(self.acc & 0xFF)
            self.acc >>= 8
            self.n -= 8

    def code(self, value, nbits):               # Huffman codes go MSB first
        for i in range(nbits - 1, -1, -1):
            self.put((value >> i) & 1, 1)

    def finish(self):
        if self.n:
            self.out.append(self.acc & 0xFF)
        return bytes(self.out)


def fixed_symbol(bits, sym):
    if sym < 144:
        bits.code(0x30 + sym, 8)
    elif sym < 256:
        bits.code(0x190 + sym - 144, 9)
    elif sym < 280:
        bits.code(sym - 256, 7)
    else:
        bits.code(0xC0 + sym - 280, 8)


def deflate(data):
    """One fixed-Huffman block, greedy longest match (nearest on ties)."""
    bits = Bits()
    bits.put(1, 1)                               # final block
    bits.put(1, 2)                               # fixed Huffman
    i = 0
    while i < len(data):
        best_len, best_dist = 0, 0
        for j in range(i - 1, max(-1, i - 32769), -1):
            n = 0
            while n < 258 and i + n < len(data) and data[j + n] == data[i + n]:
                n += 1
            if n > best_len:
                best_len, best_dist = n, i - j
        if best_len >= 3:
            li = max(k for k in range(29) if LEN_BASE[k] <= best_len)
            fixed_symbol(bits, 257 + li)
            bits.put(best_len - LEN_BASE[li], LEN_EXTRA[li])
            di = max(k for k in range(30) if DIST_BASE[k] <= best_dist)
            bits.code(di, 5)
            bits.put(best_dist - DIST_BASE[di], DIST_EXTRA[di])
            i += best_len
        else:
            fixed_symbol(bits, data[i])
            i += 1
    fixed_symbol(bits, 256)
    return bits.finish()


def chunk(kind, body):
    return (struct.pack(">I", len(body)) + kind + body
            + struct.pack(">I", zlib.crc32(kind + body)))


def png(w, h, slots, palette):
    """4-bit indexed PNG of slots (h rows of w ints); palette[0] is transparent."""
    raw = bytearray()
    for row in slots:
        raw.append(0)                            # filter: none
        for x in range(0, w, 2):
            raw.append((row[x] << 4) | (row[x + 1] if x + 1 < w else 0))
    z = b"\x78\x01" + deflate(bytes(raw)) + struct.pack(">I", zlib.adler32(bytes(raw)))
    plte = b"".join(bytes(c) for c in palette)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 4, 3, 0, 0, 0))
            + chunk(b"PLTE", plte)
            + chunk(b"tRNS", b"\x00")
            + chunk(b"IDAT", z)
            + chunk(b"IEND", b""))


def logo_frame(exe):
    """(w, h, slots, 8-bit palette, background '#rrggbb') of the logo on the web background."""
    hexbg, bg565 = logo_preview.run(exe, "webbg").split()
    lines = logo_preview.run(exe, "frame", bg565).splitlines()
    w, h = map(int, lines[0].split())
    palette = [logo_preview.rgb(int(v)) for v in lines[1].split()]
    slots = [[int(c, 16) for c in lines[2 + y]] for y in range(h)]
    return w, h, slots, palette, "#" + hexbg


def header_text(exe):
    w, h, slots, palette, bg = logo_frame(exe)
    uri = "data:image/png;base64," + base64.b64encode(png(w, h, slots, palette)).decode()
    parts = [uri[i:i + 76] for i in range(0, len(uri), 76)]
    lit = "\n".join('    "%s"' % p for p in parts)
    return (
        "// logo_web.h — GENERATED by firmware/tools/logo_web.py from logo.h; do not edit.\n"
        "// The chirp! logo for the setup web page (portal.cpp): a 4-bit indexed PNG, the\n"
        "// background transparent, the edge drawn for the light background LOGO_WEB_BG\n"
        "// (logo::WEB_BG); the page enlarges it with image-rendering: pixelated.\n"
        "#pragma once\n\n"
        '#define LOGO_WEB_BG "%s"\n'
        '#define LOGO_WEB_SIZE "width=%d height=%d"\n'
        "static const char LOGO_WEB_PNG[] =\n%s;\n" % (bg, w * SCALE, h * SCALE, lit))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="verify instead of writing; exit 1 when include/logo_web.h is stale")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        text = header_text(logo_preview.build(tmp))
    if args.check:
        with open(HEADER) as f:
            if f.read() != text:
                sys.exit("firmware/include/logo_web.h is stale: run firmware/tools/logo_web.py")
        return
    with open(HEADER, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()

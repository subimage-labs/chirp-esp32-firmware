#!/usr/bin/env python3
"""Tests for firmware/tools/logo_web.py: the logo baked into include/logo_web.h.

The header must be exactly what the generator makes from logo.h (so editing
the art without regenerating fails here), the PNG must decode to the logo's own
frame on the web background, and the deflate writer must round-trip anything.
Run by firmware/test/host/run.sh; needs g++.
"""

import base64
import os
import random
import re
import struct
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
sys.path.insert(0, TOOLS)

import logo_preview  # noqa: E402
import logo_web  # noqa: E402

HEADER = os.path.join(HERE, "..", "..", "include", "logo_web.h")
PORTAL = os.path.join(HERE, "..", "..", "src", "portal.cpp")


def read(path):
    with open(path) as f:
        return f.read()


def header_png(text):
    lit = text.split("LOGO_WEB_PNG[] =")[1]
    uri = "".join(re.findall(r'"([^"]*)"', lit))
    prefix = "data:image/png;base64,"
    assert uri.startswith(prefix)
    return uri, base64.b64decode(uri[len(prefix):], validate=True)


def chunks(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    out, i = [], 8
    while i < len(data):
        n, kind = struct.unpack(">I4s", data[i:i + 8])
        body = data[i + 8:i + 8 + n]
        (crc,) = struct.unpack(">I", data[i + 8 + n:i + 12 + n])
        assert crc == zlib.crc32(kind + body), kind
        out.append((kind, body))
        i += 12 + n
    return out


class LogoWeb(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = logo_preview.build(cls.tmp.name)
        cls.text = read(HEADER)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_header_is_what_the_generator_makes(self):
        self.assertEqual(self.text, logo_web.header_text(self.exe),
                         "include/logo_web.h is stale: run firmware/tools/logo_web.py")

    def test_png_decodes_to_the_logo_frame(self):
        w, h, slots, palette, bg = logo_web.logo_frame(self.exe)
        uri, data = header_png(self.text)
        parts = chunks(data)
        self.assertEqual([k for k, _ in parts], [b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND"])
        ihdr = dict(zip(("w", "h", "depth", "type", "comp", "filter", "interlace"),
                        struct.unpack(">IIBBBBB", parts[0][1])))
        self.assertEqual((ihdr["w"], ihdr["h"], ihdr["depth"], ihdr["type"]), (w, h, 4, 3))
        self.assertEqual(parts[1][1], b"".join(bytes(c) for c in palette))
        self.assertEqual(parts[2][1], b"\x00")              # slot 0, the background, is transparent
        raw = zlib.decompress(parts[3][1])
        stride = 1 + (w + 1) // 2
        self.assertEqual(len(raw), stride * h)
        for y in range(h):
            row = raw[y * stride:(y + 1) * stride]
            self.assertEqual(row[0], 0)
            px = []
            for b in row[1:]:
                px += [b >> 4, b & 15]
            self.assertEqual(px[:w], slots[y])
            self.assertTrue(all(v == 0 for v in px[w:]))     # padding nibble
        self.assertLessEqual(len(uri), 700)                  # served from the ESP32: keep it small

    def test_portal_uses_it(self):
        portal = read(PORTAL)
        self.assertIn('#include "logo_web.h"', portal)
        self.assertIn("LOGO_WEB_PNG", portal)
        self.assertIn("LOGO_WEB_BG", portal)


class Deflate(unittest.TestCase):
    def roundtrip(self, data):
        z = b"\x78\x01" + logo_web.deflate(data) + struct.pack(">I", zlib.adler32(data))
        self.assertEqual(zlib.decompress(z), data)

    def test_edges(self):
        self.roundtrip(b"")
        self.roundtrip(b"\x00")
        self.roundtrip(bytes(range(256)))                    # every literal code, 8 and 9 bits
        self.roundtrip(b"\x00" * 1000)                       # runs longer than one match
        self.roundtrip(b"abc" * 500)
        self.roundtrip(bytes([0, 1, 2]) * 3 + bytes(range(200, 256)) * 5)

    def test_random(self):
        rng = random.Random(7)
        for _ in range(40):
            n = rng.randrange(0, 600)
            alphabet = rng.choice([2, 3, 16, 256])
            self.roundtrip(bytes(rng.randrange(alphabet) for _ in range(n)))

    def test_long_distance(self):
        rng = random.Random(3)
        block = bytes(rng.randrange(256) for _ in range(300))
        self.roundtrip(block + bytes(rng.randrange(256) for _ in range(2000)) + block)


if __name__ == "__main__":
    unittest.main()

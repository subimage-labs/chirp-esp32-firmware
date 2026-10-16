"""VLW smooth-font writer/reader for TFT_eSPI (loadFont from a flash array).

Pure Python on Pillow (FreeType) + fontTools. Shared by make_font.py and the
host-side preview renderer, so previews draw the exact bytes the device gets.

VLW layout (see TFT_eSPI Extensions/Smooth_font.cpp): six big-endian uint32
header words (glyph count, version 11, size, 0, ascent, descent), then per
glyph seven int32 (code, height, width, advance, dY, dX, 0), then the 8-bit
alpha bitmaps back to back, then name strings and the smooth flag.
"""
import struct
from dataclasses import dataclass

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

# Printable ASCII first (the device finds glyphs by linear search, so the
# common ones sit at the front), then Latin-1, then typographic punctuation.
# U+0020 is absent on purpose: the UI draws spaces as U+00A0 so it gets the
# font's real space advance (TFT_eSPI guesses one for U+0020).
CHARSET = (
    list(range(0x21, 0x7F))
    + list(range(0xA0, 0x100))
    + [0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
       0x2022, 0x2026, 0x20AC, 0x2122, 0x2190, 0x2192, 0x2212]
)


@dataclass
class Glyph:
    code: int
    w: int
    h: int
    adv: int
    dy: int   # bitmap top above the baseline
    dx: int   # bitmap left from the cursor
    alpha: bytes


def rasterize(ttf_path, px, gamma=1.0, charset=CHARSET, variation=None, mono=False):
    """Glyphs of the TTF/OTF at px pixels per em, hinted by FreeType.

    gamma < 1 raises mid coverage (heavier-looking strokes on light themes);
    1.0 keeps FreeType's coverage as is. mono renders 1-bit (no anti-aliasing,
    FreeType's monochrome hinting): alpha is 0 or 255, for pixel fonts.
    """
    cmap = TTFont(ttf_path, lazy=True).getBestCmap()
    font = ImageFont.truetype(ttf_path, px, layout_engine=ImageFont.Layout.BASIC)
    if variation is not None:
        font.set_variation_by_axes(variation)
    lut = bytes(round(255 * (a / 255) ** gamma) for a in range(256))
    glyphs = []
    for cp in charset:
        if cp == 0xA0:
            adv = round(font.getlength(" "))
            glyphs.append(Glyph(cp, 0, 0, adv, 0, 0, b""))
            continue
        if cp not in cmap:
            continue
        ch = chr(cp)
        adv = round(font.getlength(ch))
        l, t, r, b = font.getbbox(ch, anchor="ls")
        w, h = r - l, b - t
        if w <= 0 or h <= 0:
            glyphs.append(Glyph(cp, 0, 0, adv, 0, 0, b""))
            continue
        im = Image.new("L", (w, h), 0)
        d = ImageDraw.Draw(im)
        if mono:
            d.fontmode = "1"
        d.text((-l, -t), ch, font=font, fill=255, anchor="ls")
        glyphs.append(Glyph(cp, w, h, adv, -t, l, im.tobytes().translate(lut)))
    return glyphs


def bdf_glyphs(bdf_path, charset=CHARSET):
    """Glyphs of a BDF bitmap font at its one native size: exact hand-made
    pixels (alpha 0 or 255), advance from DWIDTH. U+00A0 falls back to the
    space's advance when the font has no nbsp of its own."""
    found = {}
    cur = None
    rows = None
    with open(bdf_path, encoding="latin-1") as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "STARTCHAR":
                cur = {"code": -1, "adv": 0, "bbx": (0, 0, 0, 0), "rows": []}
            elif cur is None:
                continue
            elif w[0] == "ENCODING":
                cur["code"] = int(w[1])
            elif w[0] == "DWIDTH":
                cur["adv"] = int(w[1])
            elif w[0] == "BBX":
                cur["bbx"] = tuple(int(x) for x in w[1:5])
            elif w[0] == "BITMAP":
                rows = cur["rows"]
            elif w[0] == "ENDCHAR":
                rows = None
                if cur["code"] >= 0:
                    found[cur["code"]] = cur
                cur = None
            elif rows is not None:
                rows.append(w[0])
    glyphs = []
    for cp in charset:
        g = found.get(cp)
        if g is None and cp == 0xA0 and 0x20 in found:
            g = {"adv": found[0x20]["adv"], "bbx": (0, 0, 0, 0), "rows": []}
        if g is None:
            continue
        w, h, xoff, yoff = g["bbx"]
        alpha = bytearray()
        for hexrow in g["rows"][:h]:
            nbits = 4 * len(hexrow)
            for x in range(w):
                alpha.append(255 if (int(hexrow, 16) >> (nbits - 1 - x)) & 1 else 0)
        if w <= 0 or h <= 0 or not any(alpha):
            glyphs.append(Glyph(cp, 0, 0, g["adv"], 0, 0, b""))
            continue
        glyphs.append(trim(Glyph(cp, w, h, g["adv"], h + yoff, xoff, bytes(alpha))))
    return glyphs


def bdf_box(bdf_path):
    """(FONT_ASCENT, FONT_DESCENT) of a BDF: its cell height is the line pitch."""
    asc = desc = None
    with open(bdf_path, encoding="latin-1") as f:
        for line in f:
            w = line.split()
            if w[:1] == ["FONT_ASCENT"]:
                asc = int(w[1])
            elif w[:1] == ["FONT_DESCENT"]:
                desc = int(w[1])
            elif w[:1] == ["STARTCHAR"]:
                break
    return asc, desc


def trim(g):
    """Crops empty rows and columns around the ink (BDF cells are padded to the
    font box); the advance is unchanged."""
    rows = [any(g.alpha[y * g.w:(y + 1) * g.w]) for y in range(g.h)]
    cols = [any(g.alpha[y * g.w + x] for y in range(g.h)) for x in range(g.w)]
    y0, y1 = rows.index(True), g.h - rows[::-1].index(True)
    x0, x1 = cols.index(True), g.w - cols[::-1].index(True)
    alpha = bytes(g.alpha[y * g.w + x] for y in range(y0, y1) for x in range(x0, x1))
    return Glyph(g.code, x1 - x0, y1 - y0, g.adv, g.dy - y0, g.dx + x0, alpha)


def metrics(glyphs):
    """(ascent, descent): the extremes over every glyph, so no glyph pokes out
    of the line box and the box height is ascent + descent."""
    ink = [g for g in glyphs if g.h]
    return max(g.dy for g in ink), max(g.h - g.dy for g in ink)


def write_vlw(glyphs, px, name, box=None):
    """box = (ascent, descent) overrides the glyph extremes: a bitmap font keeps
    its own cell height as the line pitch, leading included."""
    ascent, descent = box or metrics(glyphs)
    out = struct.pack(">6I", len(glyphs), 11, px, 0, ascent, descent)
    for g in glyphs:
        out += struct.pack(">7i", g.code, g.h, g.w, g.adv, g.dy, g.dx, 0)
    for g in glyphs:
        out += g.alpha
    nm = name.encode()
    out += bytes([len(nm)]) + nm + b"\0" + bytes([len(nm)]) + nm + b"\1" + b"\1"
    return out


@dataclass
class Vlw:
    glyphs: dict      # code -> Glyph
    ascent: int
    descent: int

    @property
    def line(self):
        return self.ascent + self.descent

    def adv(self, ch):
        if ch == " ":
            ch = "\u00a0"
        g = self.glyphs.get(ord(ch))
        return g.adv if g else 0

    def width(self, s):
        return sum(self.adv(c) for c in s)


def read_vlw(data):
    n, ver, _size, _z, ascent, descent = struct.unpack_from(">6I", data, 0)
    assert ver == 11
    meta = [struct.unpack_from(">7i", data, 24 + 28 * i) for i in range(n)]
    pos = 24 + 28 * n
    glyphs = {}
    for code, h, w, adv, dy, dx, _ in meta:
        glyphs[code] = Glyph(code, w, h, adv, dy, dx, data[pos:pos + w * h])
        pos += w * h
    return Vlw(glyphs, ascent, descent)

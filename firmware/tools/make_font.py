#!/usr/bin/env python3
"""Generates the device fonts: firmware/src/fonts/font_data.cpp, include/font_data.h
and the license files beside them.

Four families (Atkinson Hyperlegible, Inter, Paper Mono, Terminus), each in three
sizes plus a title cut, all SIL OFL 1.1. Every source file is fetched from its
official location (pins and SHA-256s in font-sources.txt) and checked. The outline
fonts are rasterized by FreeType (via Pillow); Terminus is a bitmap font whose
hand-drawn pixels at each native size are taken as they are (no scaling, no
anti-aliasing). All are written as TFT_eSPI smooth-font (VLW) arrays.

    python3 firmware/tools/make_font.py [--cache DIR]

Needs: Pillow, fontTools. The output depends on the FreeType version inside
Pillow (recorded in the generated file), so regenerate only on purpose.
"""
import argparse
import hashlib
import io
import pathlib
import sys
import tarfile
import urllib.request
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import vlw  # noqa: E402
from PIL import features  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
PINS = pathlib.Path(__file__).with_name("font-sources.txt")
OUT_CPP = ROOT / "firmware/src/fonts/font_data.cpp"
OUT_H = ROOT / "firmware/include/font_data.h"
LICENSE_DIR = ROOT / "firmware/src/fonts"

# Title cut: the idle READY screen and the agent's name in the Chirper tab, so
# printable ASCII (uiFold substitutes what it lacks).
TITLE_CHARSET = list(range(0x21, 0x7F)) + [0xA0]


class Family:
    def __init__(self, name, src, sizes, license_file, title, bitmap=False, gamma=0.85):
        self.name = name              # display name; also the key the choice is stored under
        self.src = src                # id in font-sources.txt
        self.sizes = sizes            # (file in that source, px) for S, M, L
        self.license_file = license_file
        self.title = title            # (file, px)
        self.bitmap = bitmap          # BDF strikes at native sizes: no scaling, no gamma
        self.gamma = gamma

    @property
    def ident(self):
        return self.name.replace(" ", "").upper()


def outline(name, src, regular, sizes, license_file, title):
    return Family(name, src, [(regular, px) for px in sizes], license_file, title)


# Display order = alphabetical, like the themes. Sizes are chosen per family so
# the x-height of S/M/L is about 7/8/9 px (Atkinson 13/15/18). Terminus has hand
# drawn strikes only at whole cell sizes: S/M/L are its 8x14, 8x16 and 10x20 bold
# strikes (x-height 7/8/10), the bold weight because its regular strokes are one
# pixel thin and wash out on light themes.
TER = "terminus-font-4.49.1/"
FAMILIES = [
    outline("Atkinson", "atkinson", "fonts/ttf/AtkinsonHyperlegible-Regular.ttf", (13, 15, 18),
            "OFL.txt", ("fonts/ttf/AtkinsonHyperlegible-Bold.ttf", 24)),
    outline("Inter", "inter", "extras/ttf/Inter-Regular.ttf", (12, 14, 16), "LICENSE.txt",
            ("extras/ttf/Inter-Bold.ttf", 24)),
    outline("Paper Mono", "papermono", "fonts/ttf/PaperMono-Regular.ttf", (13, 15, 17),
            "OFL.txt", ("fonts/ttf/PaperMono-Bold.ttf", 22)),
    Family("Terminus", "terminus",
           [(TER + "ter-u14b.bdf", 14), (TER + "ter-u16b.bdf", 16), (TER + "ter-u20b.bdf", 20)],
           TER + "OFL.TXT", (TER + "ter-u24b.bdf", 24), bitmap=True),
]
SIZE_LETTERS = "SML"
FIXED_FAMILY = "Atkinson"      # the setup portal's font (layout is drawn for its M cut)
DEFAULT_FAMILY = "Atkinson"


def read_pins():
    sources, files = {}, {}
    for line in PINS.read_text().splitlines():
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        if f[0] == "raw":
            sources[f[1]] = ("raw", f[2], None)
        elif f[0] in ("zip", "tar"):
            sources[f[1]] = (f[0], f[2], f[3])
        elif f[0] == "file":
            files[(f[1], f[2])] = f[3]
    return sources, files


def fetch_bytes(url, cache):
    dest = cache / hashlib.sha256(url.encode()).hexdigest()[:16]
    if not dest.exists():
        cache.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=120) as r:
            dest.write_bytes(r.read())
    return dest.read_bytes()


class Sources:
    def __init__(self, cache):
        self.cache = cache
        self.sources, self.files = read_pins()
        self.zips = {}
        self.paths = {}

    def get(self, src, path):
        pin = (src, path)
        if pin in self.paths:
            return self.paths[pin]
        kind, url, zsha = self.sources[src]
        sha = self.files[pin]
        if kind == "raw":
            data = fetch_bytes(url + path, self.cache)
        else:
            if src not in self.zips:
                blob = fetch_bytes(url, self.cache)
                got = hashlib.sha256(blob).hexdigest()
                if got != zsha:
                    sys.exit(f"{url}: sha256 {got} != pinned {zsha}")
                self.zips[src] = (zipfile.ZipFile(io.BytesIO(blob)) if kind == "zip"
                                  else tarfile.open(fileobj=io.BytesIO(blob), mode="r:*"))
            arc = self.zips[src]
            data = arc.read(path) if kind == "zip" else arc.extractfile(path).read()
        got = hashlib.sha256(data).hexdigest()
        if got != sha:
            sys.exit(f"{src}:{path}: sha256 {got} != pinned {sha}")
        dest = self.cache / ("pinned-" + sha[:16] + "-" + pathlib.Path(path).name)
        dest.write_bytes(data)
        self.paths[pin] = dest
        return dest


def c_array(name, items, per_line, fmt):
    lines = []
    for i in range(0, len(items), per_line):
        lines.append("    " + ", ".join(fmt % v for v in items[i:i + per_line]) + ",")
    return f"static const uint8_t {name}[] PROGMEM = {{\n" + "\n".join(lines) + "\n};\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default=str(ROOT / "tmp" / "font-cache"))
    cache = pathlib.Path(ap.parse_args().cache)
    src = Sources(cache)
    ft = features.version("freetype2")

    cpp = [
        "// Generated by firmware/tools/make_font.py. Do not edit.",
        "// Atkinson Hyperlegible, Inter, Paper Mono and Terminus, each under the SIL",
        "// Open Font License 1.1 (OFL-*.txt beside this file); sources and pins in",
        f"// firmware/tools/font-sources.txt; rasterized by FreeType {ft} through Pillow.",
        '#include "font_data.h"',
        "",
    ]
    tables = []
    max_box = 0
    total = 0
    for fam in FAMILIES:
        cuts = []
        for letter, (path, px) in zip(SIZE_LETTERS, fam.sizes):
            cuts.append((f"{fam.ident}_{letter}", path, px, vlw.CHARSET, fam.name + " " + letter))
        tpath, tpx = fam.title
        cuts.append((f"{fam.ident}_TITLE", tpath, tpx, TITLE_CHARSET, fam.name + " title"))
        refs = []
        for var, path, px, charset, label in cuts:
            file = str(src.get(fam.src, path))
            if fam.bitmap:
                glyphs = vlw.bdf_glyphs(file, charset)
                box = vlw.bdf_box(file)
            else:
                glyphs = vlw.rasterize(file, px, fam.gamma, charset)
                box = None
            data = vlw.write_vlw(glyphs, px, label.replace(" ", ""), box)
            asc, desc = box or vlw.metrics(glyphs)
            cpp.append(c_array(var, list(data), 16, "0x%02X"))
            refs.append(f"{{{var}, sizeof {var}, {len(glyphs)}, {asc + desc}}}")
            max_box = max(max_box, asc + desc)
            total += len(data)
            print(f"{label:22s} {px:2d}px {len(glyphs):3d} glyphs box {asc + desc:2d} {len(data):6d} bytes")
        tables.append(f'    {{"{fam.name}", {{{", ".join(refs[:3])}}},\n     {refs[3]}}},')
        lic = src.get(fam.src, fam.license_file)
        (LICENSE_DIR / f"OFL-{fam.name.replace(' ', '')}.txt").write_bytes(lic.read_bytes())

    cpp.append("const FontFamily FONT_FAMILIES[] = {\n" + "\n".join(tables) + "\n};\n")
    OUT_CPP.write_text("\n".join(cpp) + "\n")

    h = [
        "// font_data.h — the device fonts as TFT_eSPI smooth-font (VLW) arrays in flash,",
        "// four families in three sizes (S, M, L) plus a title cut each. Generated by",
        "// firmware/tools/make_font.py; ui.cpp chooses among them, fontpick.h reads them.",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#ifndef PROGMEM",
        "#define PROGMEM",
        "#endif",
        "",
        "struct FontCut {",
        "    const uint8_t* vlw;   // the array TFT_eSPI::loadFont takes",
        "    size_t bytes;",
        "    uint16_t glyphs;",
        "    uint8_t box;          // line box in px: the cell height of a bitmap font, else the glyph extremes",
        "};",
        "",
        "struct FontFamily {",
        "    const char* name;     // display name and the key the choice is stored under",
        "    FontCut size[3];      // S, M, L",
        "    FontCut title;        // printable ASCII: the idle screen, the agent name",
        "};",
        "",
        "extern const FontFamily FONT_FAMILIES[];",
        f"static const size_t FONT_FAMILY_COUNT = {len(FAMILIES)};",
        f"static const int FONT_MAX_BOX = {max_box};      // tallest line box of any cut: the text sprite's height",
        f'#define FONT_DEFAULT_FAMILY "{DEFAULT_FAMILY}"',
        f'#define FONT_FIXED_FAMILY "{FIXED_FAMILY}"      // the setup portal draws in this family\'s M cut',
        "",
    ]
    OUT_H.write_text("\n".join(h))
    print(f"total {total} bytes of VLW data")


if __name__ == "__main__":
    main()

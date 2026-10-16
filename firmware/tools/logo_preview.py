#!/usr/bin/env python3
"""Sheets of the chirp! logo on every theme background.

Every frame comes from the firmware's own logo.h: this script compiles the host
harness (firmware/test/host/logo_preview.cpp) and asks it for frames; nothing
here models the logo. Needs g++ and Pillow.

  logo_preview.py OUTDIR [--art FILE ...] [--scale N] [--only THEME,...]
  logo_preview.py OUTDIR --compare [--art FILE ...] [--scale N]

Writes OUTDIR/logo-<name>.png per art: the logo on all 12 theme backgrounds
(6 families, dark and light), at --scale (default 3, the empty chat screen's).
Without --art it draws the logo compiled into the firmware (logo.h); a FILE is
a text file of art rows (one character per pixel, as in logo.h; lines starting
with ';' are comments), for trying a design before it goes into the header.

--compare draws two sheets instead, with the logo compiled into the firmware
first and each --art after it: OUTDIR/compare.png (every design on all 12
themes, with its size and the scale it gets) and OUTDIR/fit.png (every design
as the boot splash and as the empty chat screen's title, mocked at the panel's
320x170 pixels: the splash area is 148 px tall, the title's about 60 px).
"""

import argparse
import os
import subprocess
import tempfile
import textwrap

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS_SRC = os.path.join(HERE, "..", "test", "host", "logo_preview.cpp")
INCLUDE = os.path.join(HERE, "..", "include")


def build(workdir):
    exe = os.path.join(workdir, "logo_preview")
    subprocess.run(["g++", "-std=c++11", "-O1", "-Wall", "-Wextra", "-I", INCLUDE,
                    HARNESS_SRC, "-o", exe], check=True)
    return exe


def run(exe, *args):
    return subprocess.run([exe, *args], check=True, capture_output=True, text=True).stdout


def rgb(c565):
    r, g, b = (c565 >> 11) & 31, (c565 >> 5) & 63, c565 & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def themes(exe):
    out = []
    for line in run(exe, "themes").splitlines():
        name, mode, bg, text, muted, accent = line.split()
        out.append((f"{name.replace('_', ' ')} {mode}", int(bg), int(text), int(muted), int(accent)))
    return out


def frame(exe, bg, art):
    lines = run(exe, "frame", str(bg), *([art] if art else [])).splitlines()
    w, h = map(int, lines[0].split())
    pal = [rgb(int(v)) for v in lines[1].split()]
    img = Image.new("RGB", (w, h))
    for y in range(h):
        for x in range(w):
            img.putpixel((x, y), pal[int(lines[2 + y][x], 16)])
    return img


def sheet(exe, art, scale, path, only=None):
    cells = [c for c in themes(exe) if not only or c[0] in only]
    first = frame(exe, cells[0][1], art)
    cw, ch = first.width * scale + 40, first.height * scale + 40 + 14
    cols = min(4, len(cells))
    rows = (len(cells) + cols - 1) // cols
    out = Image.new("RGB", (cw * cols, ch * rows))
    d = ImageDraw.Draw(out)
    for i, (name, bg, text, muted, accent) in enumerate(cells):
        x0, y0 = (i % cols) * cw, (i // cols) * ch
        d.rectangle([x0, y0, x0 + cw - 1, y0 + ch - 1], fill=rgb(bg))
        img = frame(exe, bg, art).resize((first.width * scale, first.height * scale), Image.NEAREST)
        out.paste(img, (x0 + 20, y0 + 10))
        d.text((x0 + 6, y0 + ch - 14), name, fill=rgb(muted))
    out.save(path)


PANEL_W, PANEL_H = 320, 170     # the T-Embed's screen
SPLASH_H = 148                  # the boot splash's area: above the status strip
TITLE_H = 60                    # the empty chat screen: the title's room above the hints
MAX_SCALE = 4                   # the characters' scale (logo_draw.h LOGO_MAX_SCALE)


def fit_scale(w, h, max_w, max_h):
    s = MAX_SCALE
    while s > 1 and (w * s > max_w or h * s > max_h):
        s -= 1
    return s


def label(art):
    return os.path.splitext(os.path.basename(art))[0] if art else "current"


def idea(art):
    """The design note: the first comment line of the art file."""
    if not art:
        return "The shipped logo: the chick beside the wordmark."
    with open(art) as f:
        first = f.readline()
    return first[1:].strip() if first.startswith(";") else ""


def font(size):
    return ImageFont.load_default(size)


def compare_sheet(exe, arts, scale, path):
    cells = themes(exe)
    # Dark and light of a family stack in one column.
    cols = [(cells[i], cells[i + 1]) for i in range(0, len(cells), 2)]
    frames = {a: frame(exe, cells[0][1], a) for a in arts}
    tw = max(f.width for f in frames.values()) * scale + 14
    th = max(f.height for f in frames.values()) * scale + 14
    lw, rh = 250, 2 * th + 6
    out = Image.new("RGB", (lw + tw * len(cols), rh * len(arts)), (24, 24, 28))
    d = ImageDraw.Draw(out)
    for r, art in enumerate(arts):
        y0 = r * rh
        f = frames[art]
        aw, ah = f.width - 2, f.height - 2
        ss, cs = fit_scale(f.width, f.height, PANEL_W, SPLASH_H), fit_scale(f.width, f.height, PANEL_W, TITLE_H)
        d.text((10, y0 + 8), label(art), fill=(240, 240, 240), font=font(18))
        info = ["art %d x %d px, frame %d x %d" % (aw, ah, f.width, f.height),
                "splash: %dx, %d x %d px" % (ss, f.width * ss, f.height * ss),
                "chat title: %dx, %d x %d px%s" % (cs, f.width * cs, f.height * cs,
                                                   "" if f.height * cs <= TITLE_H else " (too tall)")]
        for i, line in enumerate(info):
            d.text((10, y0 + 36 + i * 17), line, fill=(190, 190, 195), font=font(13))
        for i, line in enumerate(textwrap.wrap(idea(art), 38)):
            d.text((10, y0 + 100 + i * 15), line, fill=(150, 150, 158), font=font(12))
        for c, pair in enumerate(cols):
            for k, (name, bg, text, muted, accent) in enumerate(pair):
                x0, y1 = lw + c * tw, y0 + k * (th + 3)
                d.rectangle([x0, y1, x0 + tw - 1, y1 + th - 1], fill=rgb(bg))
                img = frame(exe, bg, art).resize((f.width * scale, f.height * scale), Image.NEAREST)
                out.paste(img, (x0 + (tw - img.width) // 2, y1 + 4))
                d.text((x0 + 4, y1 + th - 12), name, fill=rgb(muted), font=font(9))
    out.save(path)


def mock_panel(exe, art, bg, text, muted, splash):
    """One 320x170 panel: the boot splash, or the empty chat screen, at 1:1."""
    f = frame(exe, bg, art)
    img = Image.new("RGB", (PANEL_W, PANEL_H), rgb(bg))
    d = ImageDraw.Draw(img)
    if splash:
        s = fit_scale(f.width, f.height, PANEL_W, SPLASH_H)
        big = f.resize((f.width * s, f.height * s), Image.NEAREST)
        img.paste(big, ((PANEL_W - big.width) // 2, (SPLASH_H - big.height) // 2))
        d.text((8, SPLASH_H + 4), "Joining WiFi...", fill=rgb(muted), font=font(12))
    else:
        s = fit_scale(f.width, f.height, PANEL_W, TITLE_H)
        big = f.resize((f.width * s, f.height * s), Image.NEAREST)
        bottom = 2 + TITLE_H + 2                      # the title ends here; the hints start under it
        img.paste(big, ((PANEL_W - big.width) // 2, bottom - big.height))
        for i, line in enumerate(("Hold encoder to talk", "Double-tap for voice mode")):
            w = d.textlength(line, font=font(13))
            d.text(((PANEL_W - w) // 2, bottom + 8 + i * 17), line, fill=rgb(muted), font=font(13))
        d.line([(0, 150), (PANEL_W, 150)], fill=rgb(muted))
    return img


def fit_sheet(exe, arts, path):
    cells = {c[0]: c for c in themes(exe)}
    dark, light = cells["Tokyo Night dark"], cells["Gruvbox light"]
    pad = 8
    out = Image.new("RGB", (4 * PANEL_W + 5 * pad + 120, len(arts) * (PANEL_H + pad) + pad), (24, 24, 28))
    d = ImageDraw.Draw(out)
    for r, art in enumerate(arts):
        y = pad + r * (PANEL_H + pad)
        d.text((pad, y + 6), label(art), fill=(240, 240, 240), font=font(15))
        d.text((pad, y + 28), "splash | title", fill=(150, 150, 158), font=font(11))
        x = 120 + pad
        for cell in (dark, light):
            for splash in (True, False):
                out.paste(mock_panel(exe, art, cell[1], cell[2], cell[3], splash), (x, y))
                x += PANEL_W + pad
    out.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--art", action="append", help="art rows file (repeatable)")
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--compare", action="store_true",
                    help="write compare.png and fit.png for the compiled-in logo and every --art")
    ap.add_argument("--only", help="comma list of themes to draw, e.g. 'Solarized dark,Gruvbox light'")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(tmp)
        if args.compare:
            arts = [None] + (args.art or [])
            compare_sheet(exe, arts, args.scale, os.path.join(args.outdir, "compare.png"))
            fit_sheet(exe, arts, os.path.join(args.outdir, "fit.png"))
            return
        for art in args.art or [None]:
            name = os.path.splitext(os.path.basename(art))[0] if art else "firmware"
            sheet(exe, art, args.scale, os.path.join(args.outdir, f"logo-{name}.png"),
                  args.only.split(",") if args.only else None)


if __name__ == "__main__":
    main()

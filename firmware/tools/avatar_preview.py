#!/usr/bin/env python3
"""Contact sheets, part sheets and per-state GIFs of the device's characters.

Every frame comes from the firmware's own avatar.h: this script compiles the
host harness (firmware/test/host/avatar_preview.cpp) and asks it for frames;
nothing here models the avatar. Needs g++ and Pillow.

  avatar_preview.py OUTDIR [--sheets] [--parts] [--gifs] [--stats N]

With no selection it writes everything:
  contact-dark.png / contact-light.png   64 characters (variants of one name)
  parts/<vocabulary>.png                 one sheet per vocabulary: every value
                                         on the same seeded base character
  gif/<name>-<state>.gif                 each state and reaction of 3 characters (--gif-looks)
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "host"))
import avatar_look as al  # noqa: E402

HARNESS_SRC = os.path.join(ROOT, "firmware", "test", "host", "avatar_preview.cpp")
STATES = ["idle", "listening", "thinking", "speaking", "error", "sleep"]
REACTIONS = ["pleased", "puzzled", "surprised"]
REACT_MS = {1: 1000, 2: 1200, 3: 600}
FRAME_MS = 66


class Harness:
    def __init__(self, workdir):
        exe = os.path.join(workdir, "avatar_preview")
        subprocess.run(["g++", "-std=c++17", "-O2", "-I", os.path.join(ROOT, "firmware", "include"),
                        HARNESS_SRC, "-o", exe], check=True)
        self.exe = exe
        self.p = subprocess.Popen([exe, "serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.w, self.h, self.slots, self.fields = self.ask("I", 4)
        self.ranges = list(self.read(self.fields))
        self.states, self.reactions = self.read(2)
        self.cache = {}

    def read(self, n):
        b = self.p.stdout.read(n)
        if len(b) != n:
            raise RuntimeError("harness died")
        return b

    def ask(self, line, n):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()
        return self.read(n)

    def variant(self, name, n):
        b = self.ask(f"V {name or '-'} {n}", self.fields + 4)
        return list(b[:self.fields]), struct.unpack("<I", b[self.fields:])[0]

    def background(self, family, light):
        return struct.unpack("<H", self.ask(f"B {family} {int(light)}", 2))[0]

    def frame(self, rec, seed, state, t, state_t, level, bg, stt=0, react=0, react_t=0):
        args = (tuple(rec), seed, state, t, state_t, level, bg, stt, react, react_t)
        if args not in self.cache:
            line = "F %s %d %d %d %d %d %d %d %d %x" % (
                "".join("%02x" % b for b in rec), seed, state, t, state_t, level, stt, react,
                react_t, bg)
            b = self.ask(line, self.slots * 2 + self.w * self.h)
            pal = struct.unpack("<%dH" % self.slots, b[:self.slots * 2])
            self.cache[args] = (pal, b[self.slots * 2:])
        return self.cache[args]

    def close(self):
        self.p.stdin.close()
        self.p.wait()


def rgb(c565):
    r, g, b = (c565 >> 11) & 31, (c565 >> 5) & 63, c565 & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def image(h, rec, seed, state=0, t=123456, state_t=789, level=200, bg=0, scale=4, **kw):
    pal, px = h.frame(rec, seed, state, t, state_t, level, bg, **kw)
    colors = [rgb(c) for c in pal]
    img = Image.new("RGB", (h.w, h.h))
    img.putdata([colors[v] for v in px])
    return img.resize((h.w * scale, h.h * scale), Image.NEAREST)


def sheet(cells, cols, bg565, cell, label_h=0, scale=4, h=None):
    """cells: list of (image, label)."""
    rows = (len(cells) + cols - 1) // cols
    bgc = rgb(bg565)
    out = Image.new("RGB", (cols * cell, rows * (cell + label_h)), bgc)
    d = ImageDraw.Draw(out)
    ink = (255, 255, 255) if sum(bgc) < 384 else (0, 0, 0)
    for i, (im, label) in enumerate(cells):
        x, y = (i % cols) * cell, (i // cols) * (cell + label_h)
        out.paste(im, (x + (cell - im.width) // 2, y))
        if label:
            d.text((x + 3, y + im.height + 1), label, fill=ink)
    return out


def contact(h, name, bg565, path, count=64, cols=8):
    cells = []
    for n in range(count):
        rec, seed = h.variant(name, n)
        cells.append((image(h, rec, seed, bg=bg565), ""))
    sheet(cells, cols, bg565, h.w * 4).save(path)


def part_sheets(h, outdir, bg565, scale=5, since=None):
    """One sheet per vocabulary, every value on the same seeded base character (mouths twice:
    at rest and speaking). since: the vocabulary sizes of an earlier release; only the values
    added after it are drawn."""
    os.makedirs(outdir, exist_ok=True)
    base_rec, base_seed = h.variant("alpha", 0)
    base_rec[al.FIELDS.index("ears")] = 0
    base_rec[al.FIELDS.index("hat")] = 0
    base_rec[al.FIELDS.index("acc")] = 0
    base_rec[al.FIELDS.index("pattern")] = 0
    base_rec[al.FIELDS.index("torso")] = 2
    base_rec[al.FIELDS.index("arms")] = 1
    base_rec[al.FIELDS.index("feet")] = 0
    for fi, field in enumerate(al.FIELDS):
        names = al.VOCAB.get(field)
        cells = []
        for v in range(since[fi] if since else 0, h.ranges[fi]):
            rec = list(base_rec)
            rec[fi] = v
            label = names[v] if names and v < len(names) else str(v)
            cells.append((image(h, rec, base_seed, bg=bg565, scale=scale), label))
            if field == "mouth":
                cells.append((image(h, rec, base_seed, state=3, bg=bg565, level=130, scale=scale), "speaking"))
        if cells:
            sheet(cells, 8, bg565, h.w * scale, label_h=12).save(os.path.join(outdir, f"{field}.png"))


def resolve(h, spec):
    """`name` -> that agent's own character; `name=<24 hex digits>` -> its seed with that
    trait record (each byte an index into the vocabulary, as in a persona look)."""
    name, _, rec_hex = spec.partition("=")
    rec, seed = h.variant(name, 0)
    if rec_hex:
        rec = [int(rec_hex[i:i + 2], 16) for i in range(0, 2 * h.fields, 2)]
    return name, rec, seed


def gifs(h, outdir, bg565, specs=("alpha", "kitchen", "nova")):
    os.makedirs(outdir, exist_ok=True)
    for spec in specs:
        name, rec, seed = resolve(h, spec)
        for si, state in enumerate(STATES):
            frames = []
            for k in range(100 if state in ("idle", "sleep") else 46):   # 6 s or 3 s at 15 fps
                t = 200000 + k * FRAME_MS
                level = 40 + (k * 37) % 200 if state in ("speaking", "listening") else 0
                frames.append(image(h, rec, seed, state=si, t=t, state_t=k * FRAME_MS,
                                    level=level, bg=bg565, scale=6, stt=int(state == "thinking" and k < 20)))
            frames[0].save(os.path.join(outdir, f"{name}-{state}.gif"), save_all=True,
                           append_images=frames[1:], duration=FRAME_MS, loop=0)
        for ri, react in enumerate(REACTIONS, 1):
            frames = []
            for k in range(REACT_MS[ri] // FRAME_MS + 3):
                t = 300000 + k * FRAME_MS
                frames.append(image(h, rec, seed, state=0, t=t, state_t=3000, level=0, bg=bg565,
                                    scale=6, react=ri, react_t=k * FRAME_MS))
            frames[0].save(os.path.join(outdir, f"{name}-{react}.gif"), save_all=True,
                           append_images=frames[1:], duration=FRAME_MS, loop=0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--sheets", action="store_true", help="contact sheets")
    ap.add_argument("--parts", action="store_true", help="one sheet per vocabulary")
    ap.add_argument("--gifs", action="store_true", help="per-state and reaction GIFs")
    ap.add_argument("--name", default="preview", help="agent name the contact sheet draws variants of")
    ap.add_argument("--since", help="vocabulary sizes of an earlier release (12 numbers, comma separated):\n"
                    "the part sheets then show only the parts added since")
    ap.add_argument("--gif-looks", default="alpha,kitchen,nova",
                    help="comma list of `name` or `name=<24 hex trait record>` for the GIFs")
    args = ap.parse_args()
    everything = not (args.sheets or args.parts or args.gifs)
    os.makedirs(args.outdir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        h = Harness(tmp)
        dark, light = h.background(0, False), h.background(0, True)
        if args.sheets or everything:
            contact(h, args.name, dark, os.path.join(args.outdir, "contact-dark.png"))
            contact(h, args.name, light, os.path.join(args.outdir, "contact-light.png"))
        if args.parts or everything:
            since = [int(x) for x in args.since.split(",")] if args.since else None
            part_sheets(h, os.path.join(args.outdir, "parts-new" if since else "parts"), dark, since=since)
        if args.gifs or everything:
            gifs(h, os.path.join(args.outdir, "gif"), dark, tuple(args.gif_looks.split(",")))
        h.close()


if __name__ == "__main__":
    main()

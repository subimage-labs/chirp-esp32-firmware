#!/usr/bin/env python3
"""One-command smoke test for a device running a bench build (-DBENCH_CMDS).

Run it after every bench flash. It drives the device over USB serial the way
host/latency.py does (bench lines written through a raw fd, replies read from
serial-watch.py's log by offset), checks the screens and settings the UI work
touches, and prints one pass/fail table. Exit status: 0 all passed, 1 a check
failed, 2 the device was not ready (nothing was changed).

Needs: host/serial-watch.py running (stty -F /dev/ttyACM0 raw -echo before
starting it), the device idle on the chat screen with no menu open.

Checks (one table row each):
  boot         [SelfTest] ok and [OTA] running <version> in the log
  status       the status line parses; wifi up; volume 0 took
  tabs         side taps walk chat > Chats > Theme > Chirper > Text > Setup > chat
  theme        wheel changes the theme, encoder flips light/dark, restored
  panel        every theme, dark and light, applied from the Theme tab: the panel's
               own inversion bit (`panel status`, RDDIM read back) stays on, also
               after the menu closes (off = every color shown inverted)
  chirper      entering shows the kept look; the wheel changes the shown look and never
               the kept one; leaving shows the kept look again
  text         wheel changes the font, encoder cycles the size, restored
  view         `view` toggles character/transcript and back
  heap         `heap all` in the transcript view: the settled heap ([Heap] bench-start
               vs bench-end, both after the transcript is cleared) loses no more than
               a tolerance; global min >= floor
  turn         (--turn) a "+ New chat" then one text turn: [Speak] end
               done, no [Hermes] errors
  reboot       (--reboot) esptool hard reset; boot health again; theme, font,
               size, view and the kept look set to non-default values survive it
  faults       no Guru Meditation, abort(), task_wdt or unexpected rst: in the log

Nothing asks Hermes unless --turn is given (the Chats tab does read the chat
list). The device is put back to its starting theme, font, size, view and
kept look at the end, even when a check fails or the run is interrupted.
It sets `vol 0` first (the volume resets to 21 on every boot).

    host/smoke.py [--port /dev/ttyACM0] [--turn] [--reboot] [--json out.json]
                  [--heap-tol 6144] [--heap-floor 98304]
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

import latency
import provision

LOG = provision.LOG
ESPTOOL_PYTHON = "/tmp/espvenv/bin/python"
QUESTION = "What is two plus two?"

STATUS_KEYS = ("state", "menu", "sel", "vol", "wifi", "view", "look", "font", "size",
               "theme", "mode", "themecode")
TABS = ("none", "chats", "theme", "chirper", "text", "setup", "none")
SIZES = "SML"
HEAP_WORKLOADS = ("render", "commit", "pending", "idle", "msg", "shimmer")
HEAP_TOL_DEFAULT = 6 * 1024     # settled free/largest may end this far below the start
HEAP_FLOOR_DEFAULT = 96 * 1024  # lowest internal-heap watermark allowed
FAULT_MARKERS = ("Guru Meditation", "abort()", "task_wdt", "Backtrace:")
REBOOT_RST_ALLOWED = 4          # esptool's bootloader entry + hard reset, plus a spare
STATUS_WAIT_S = 3.0
TAB_WAIT_S = 8.0                # the first side tap stops capture/voice before opening
HEAP_WAIT_S = 120.0
TURN_WAIT_S = 150.0
BOOT_WAIT_S = 90.0

STATUS_SPLIT = re.compile(r"\s(?=[A-Za-z_]+=)")
TAG_PAIR_RE = re.compile(r"(orig|[0-9a-f]{8})/(orig|[0-9a-f]{8})")
LOOK_RE = re.compile(r"^\[Bench\] look (orig|[0-9a-f]{32})\s*$")
UI_THEME_RE = re.compile(r"^\[UI\] theme (.+) (light|dark)\s*$")
UI_FONT_RE = re.compile(r"^\[UI\] font (.+) ([SML]) line=(\d+)")
HEAP_RE = re.compile(
    r"\[HeapBench\] (\w+)\s+n=(\d+) avg=(\d+)us max=(\d+)us \| "
    r"int free (\d+)->(\d+) low=(\d+) largest (\d+)->(\d+) low=(\d+) \| "
    r"psram free (\d+)->(\d+) \| min=(\d+)")
HEAP_LINE_RE = re.compile(r"\[Heap\] (\S+) int free=(\d+) largest=(\d+) min=(\d+)")


# ------------------------------------------------------------------ parsing
# Pure functions: everything below this block that touches the device uses them,
# and host/test_smoke.py exercises them on captured lines.

def parse_status(line):
    """`[Bench] state=idle menu=none ... font=Paper Mono size=L ...` -> dict of
    strings, or None when the line is not a status line. Values may contain
    spaces (font and theme names), so the split is on whitespace before `key=`."""
    if not line.startswith("[Bench] state="):
        return None
    out = {}
    for tok in STATUS_SPLIT.split(line[len("[Bench] "):].strip()):
        k, sep, v = tok.partition("=")
        if sep:
            out[k] = v
    return out


def missing_keys(st):
    return [k for k in STATUS_KEYS if k not in st]


def pair(st, key):
    """`sel=2/6` -> (2, 6); None when absent or malformed."""
    m = re.fullmatch(r"(-?\d+)/(-?\d+)", st.get(key, ""))
    return (int(m.group(1)), int(m.group(2))) if m else None


def tag_pair(st, key):
    """`look=orig/1a2b3c4d` -> ("orig", "1a2b3c4d"): the agent's character and the
    one the Chirper tab shows, each `orig` (the agent's Original) or an 8-hex look
    tag. None when absent or malformed."""
    m = TAG_PAIR_RE.fullmatch(st.get(key, ""))
    return (m.group(1), m.group(2)) if m else None


def parse_look(lines):
    """The latest `[Bench] look <32 hex>|orig` reply (the kept look's record, or
    `orig` when there is none), or None."""
    for line in reversed(lines):
        m = LOOK_RE.match(line)
        if m:
            return m.group(1)
    return None


def state_of(st):
    """The settings the run restores: theme code, font, size, view, kept look tag."""
    kept = tag_pair(st, "look")
    return {"themecode": int(st["themecode"]), "font": st["font"], "size": st["size"],
            "view": st["view"], "look": kept[0] if kept else None,
            "theme": st["theme"], "mode": st["mode"]}


def describe(s):
    return (f"{s['theme']} {s['mode']}, {s['font']} {s['size']}, {s['view']}, "
            f"look {s['look']}")


def last_status(lines):
    for line in reversed(lines):
        st = parse_status(line)
        if st:
            return st
    return None


def theme_events(lines):
    return [(m.group(1), m.group(2)) for m in map(UI_THEME_RE.match, lines) if m]


def font_events(lines):
    return [(m.group(1), m.group(2)) for m in map(UI_FONT_RE.match, lines) if m]


PANEL_RE = re.compile(r"\[Bench\] panel st=\S+ .*\bdim=([0-9a-f]+)")


def panel_inverted(lines):
    """The panel's inversion from the last `panel status` reply (RDDIM, read as 9
    bits: a dummy bit, then the register; bit 5 = inversion on); None if absent."""
    found = None
    for line in lines:
        m = PANEL_RE.search(line)
        if m:
            found = bool(int(m.group(1), 16) & 0x40)
    return found


def boot_health(lines):
    """Latest [SelfTest] and [OTA] running lines -> (ok, evidence)."""
    selftest = ota = None
    for line in lines:
        if line.startswith("[SelfTest] "):
            selftest = line[len("[SelfTest] "):].strip()
        elif line.startswith("[OTA] running "):
            ota = line[len("[OTA] running "):].strip()
    ok = selftest == "ok" and bool(ota)
    return ok, f"selftest={selftest or 'missing'} ota={ota or 'missing'}"


def parse_heap(lines):
    out = []
    for line in lines:
        m = HEAP_RE.search(line)
        if m:
            g = m.groups()
            out.append({"name": g[0], "n": int(g[1]), "avg_us": int(g[2]), "max_us": int(g[3]),
                        "free0": int(g[4]), "free1": int(g[5]), "low": int(g[6]),
                        "largest0": int(g[7]), "largest1": int(g[8]), "lowlargest": int(g[9]),
                        "psram0": int(g[10]), "psram1": int(g[11]), "min": int(g[12])})
    return out


def heap_min(lines, workloads):
    """Lowest internal-heap watermark seen: every workload's min= and every [Heap] line's."""
    mins = [w["min"] for w in workloads]
    for line in lines:
        m = HEAP_LINE_RE.search(line)
        if m:
            mins.append(int(m.group(4)))
    return min(mins) if mins else None


def heap_mark(lines, tag, last=False):
    """The `[Heap] <tag>` line as {free, largest, min}, or None. Matches anywhere in the
    line: another task's output can land in front of it on the serial stream."""
    found = None
    for line in lines:
        m = HEAP_LINE_RE.search(line)
        if m and m.group(1) == tag:
            found = {"free": int(m.group(2)), "largest": int(m.group(3)), "min": int(m.group(4))}
            if not last:
                break
    return found


def heap_done(lines):
    """The `heap all` run has ended (or was refused)."""
    return any("[Heap] bench-end" in l or "[HeapBench] refused" in l for l in lines)


def heap_wait_evidence(lines):
    """Why a `heap all` wait timed out: what the log did show."""
    start = heap_mark(lines, "bench-start") is not None
    got = [w["name"] for w in parse_heap(lines)]
    last = lines[-1].strip()[:80] if lines else "-"
    return (f"saw bench-start={'yes' if start else 'no'}, workloads=[{','.join(got)}], "
            f"{len(lines)} new lines, last: {last}")


def check_heap(lines, tol, floor, view=None):
    """-> (ok, evidence). The leak rule compares the settled heap: [Heap] bench-start
    and bench-end are both logged with the transcript cleared, so the end may be
    below the start (free or largest block) by at most `tol` bytes. A workload's own
    before/after is information only: `commit` ends with 40 turns still held, by design.
    Also fails when the lowest watermark is under `floor`, or when an expected workload
    printed nothing (refused, crashed, timed out). `view` is reported with the numbers
    (heap and timing differ between the transcript and the character view)."""
    if any("[HeapBench] refused" in l for l in lines):
        return False, "refused: device not idle"
    workloads = parse_heap(lines)
    seen = {w["name"] for w in workloads}
    absent = [n for n in HEAP_WORKLOADS if n not in seen]
    start = heap_mark(lines, "bench-start")
    end = heap_mark(lines, "bench-end", last=True)
    low = heap_min(lines, workloads)
    bad = []
    if absent:
        bad.append("missing " + ",".join(absent))
    settled = ""
    if start is None or end is None:
        bad.append("no " + " / ".join(t for t, m in (("bench-start", start), ("bench-end", end))
                                       if m is None))
    else:
        dfree, dlargest = end["free"] - start["free"], end["largest"] - start["largest"]
        settled = f"settled free {start['free']}->{end['free']} ({dfree:+d}), " \
                  f"largest {start['largest']}->{end['largest']} ({dlargest:+d}); "
        if dfree < -tol:
            bad.append(f"leak: free {dfree:+d} B settled (tol {tol})")
        if dlargest < -tol:
            bad.append(f"fragmentation: largest block {dlargest:+d} B settled (tol {tol})")
    if low is None:
        bad.append("no min")
    elif low < floor:
        bad.append(f"min {low} < {floor}")
    held = min(workloads, key=lambda w: w["free1"] - w["free0"], default=None)
    heavy = (f"; heaviest {held['name']} {held['free1'] - held['free0']:+d} B, "
             f"low largest {min(w['lowlargest'] for w in workloads)}") if held else ""
    ev = (f"{len(workloads)} workloads{', ' + view + ' view' if view else ''}; "
          f"{settled}min {low}{heavy}")
    return not bad, ev + ("; " + "; ".join(bad) if bad else "")


def scan_faults(lines, rst_allowed=0):
    """Crash markers in `lines`; rst: boot-ROM lines count only beyond rst_allowed."""
    found = [l.strip() for l in lines if any(m in l for m in FAULT_MARKERS)]
    rst = [l.strip() for l in lines if "rst:" in l]
    if len(rst) > rst_allowed:
        found += rst[rst_allowed:]
    return found


def next_size(letter):
    return SIZES[(SIZES.index(letter) + 1) % len(SIZES)]


def turn_verdict(lines):
    """-> (ok, evidence) for one text turn's log lines."""
    end = next((l for l in lines if l.startswith("[Speak] end")), None)
    errs = [l.strip() for l in lines
            if l.startswith("[Hermes]") and ("HTTP" in l or "refused" in l)
            or "Backend unreachable" in l]
    if any(l.startswith("[Bench] ask refused") for l in lines):
        return False, "ask refused: device not idle"
    if not end:
        return False, "no [Speak] end"
    ok = end.startswith("[Speak] end done") and not errs
    return ok, end.strip() + (f"; {len(errs)} Hermes error(s): {errs[0]}" if errs else "")


def table(rows):
    w = max([len(r["name"]) for r in rows] + [5])
    out = [f"{'check':{w}}  result  evidence", f"{'-' * w}  ------  --------"]
    for r in rows:
        out.append(f"{r['name']:{w}}  {'PASS' if r['ok'] else 'FAIL':6}  {r['evidence']}")
    n = sum(1 for r in rows if r["ok"])
    out.append(f"\n{n}/{len(rows)} passed" + ("" if n == len(rows) else "  <-- FAILED"))
    return "\n".join(out)


# ------------------------------------------------------------------- device
class DeviceError(Exception):
    """The device stopped answering: nothing further can be checked."""


class Check(Exception):
    """A check failed in a way that carries its own evidence."""


def log_size():
    return os.path.getsize(LOG)


def restart_watcher(port):
    """Start serial-watch.py detached on `port`, putting the tty in raw no-echo
    mode first (a freshly enumerated port in cooked mode swallows bench lines)."""
    subprocess.run(["stty", "-F", port, "raw", "-echo"], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    watcher = os.path.join(os.path.dirname(os.path.abspath(__file__)), "serial-watch.py")
    subprocess.Popen([watcher, "--port", port], stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=True)


def read_log(pos, end=None):
    """Complete lines written between byte offsets `pos` and `end` (default: the
    end of the log), and the new offset (a half-written last line waits for its
    newline). A log that is now shorter than `pos` was truncated or replaced: it is
    read from its start. Lines split on newlines only (str.splitlines also cuts at
    form feeds and other control characters that serial noise can carry)."""
    with open(LOG, "rb") as f:
        if os.fstat(f.fileno()).st_size < pos:
            pos = 0
        f.seek(pos)
        data = f.read() if end is None else f.read(max(0, end - pos))
    cut = data.rfind(b"\n") + 1
    text = data[:cut].decode(errors="replace").replace("\r", "")
    return text.split("\n")[:-1] if cut else [], pos + cut


class Smoke:
    def __init__(self, port):
        self.port = port
        self.dev = latency.Device(port)
        self.rows = []
        self.start_pos = log_size()
        self.start = None               # the device's state when the run began
        self.reboot_pos = None          # log offset where the esptool reset began
        self.theme_count = 6            # themes; the Theme tab's row count replaces it
        self.start_look = None          # the starting kept look's record (hex), unless `orig`

    def close(self):
        try:
            os.close(self.dev.fd)
        except OSError:
            pass

    def send(self, line, pause=0.3):
        self.dev.send(line, pause)

    def tap(self, key, hold=0.2, after=0.5):
        self.send(f"{key} down", hold)
        self.send(f"{key} up", after)

    def wait_lines(self, pred, pos, timeout, poll=0.25):
        """Poll the log after `pos` until pred(lines) is truthy; -> (lines, ok)."""
        deadline = time.time() + timeout
        while True:
            lines, _ = read_log(pos)
            if pred(lines):
                return lines, True
            if time.time() >= deadline:
                return lines, False
            time.sleep(poll)

    def status(self):
        # The read position is kept across retries: USB CDC can hold a reply's last
        # packet (a transfer ending on a full 64-byte packet waits for more data),
        # and the next request's output releases it, completing the earlier line.
        pos = log_size()
        for _ in range(4):
            self.send("status", 0.1)
            lines, ok = self.wait_lines(lambda ls: last_status(ls) is not None, pos, 1.5)
            if ok:
                return last_status(lines)
        raise DeviceError("no status reply (watcher stopped, wrong port, or release build)")

    def until(self, pred, timeout=STATUS_WAIT_S, what="state"):
        """Re-request status until pred(status) holds; -> that status. Raises Check."""
        deadline = time.time() + timeout
        st = self.status()
        while not pred(st):
            if time.time() >= deadline:
                raise Check(f"{what} not reached; last {self.brief(st)}")
            time.sleep(0.3)
            st = self.status()
        return st

    @staticmethod
    def brief(st):
        return (f"menu={st.get('menu')} sel={st.get('sel')} theme={st.get('theme')}/"
                f"{st.get('mode')} font={st.get('font')}/{st.get('size')} view={st.get('view')} "
                f"look={st.get('look')}")

    def goto(self, tab):
        """Steps (wrapping through the chat) until the menu shows `tab`."""
        st = self.status()
        for _ in range(len(TABS)):
            if st["menu"] == tab:
                return st
            self.tap("side")
            st = self.until(lambda s, was=st["menu"]: s["menu"] != was, TAB_WAIT_S, "next tab")
        raise Check(f"cannot reach tab {tab}; menu={st['menu']}")

    def check(self, name, fn):
        """Runs fn() -> evidence string (or (ok, evidence)); records a row."""
        try:
            res = fn()
            ok, ev = res if isinstance(res, tuple) else (True, res)
        except Check as exc:
            ok, ev = False, str(exc)
        except DeviceError as exc:
            self.rows.append({"name": name, "ok": False, "evidence": str(exc)})
            raise
        except Exception as exc:                          # e.g. the port vanished mid-run
            ok, ev = False, f"{type(exc).__name__}: {exc}"
        self.rows.append({"name": name, "ok": ok, "evidence": ev})

    def reopen(self, timeout=30.0):
        """Reopen the serial port after a reset (it may come back under another name)."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            port = provision.pick_port(self.port)
            if port:
                try:
                    self.dev = latency.Device(port)
                    self.port = port
                    return
                except OSError:
                    pass
            time.sleep(0.5)
        raise Check("serial port did not come back after the reset")

    # ------------------------------------------------------------ checks
    def c_boot(self, since):
        lines, _ = read_log(since)
        return boot_health(lines)

    def c_status(self):
        st = self.status()
        bad = missing_keys(st)
        if bad:
            raise Check("status lacks " + ",".join(bad) + " (flash a current bench build)")
        if st["wifi"] != "1":
            raise Check("wifi=0")
        self.send("vol 0", 0.3)
        st = self.until(lambda s: s.get("vol") == "0", what="vol 0")
        return f"state={st['state']} wifi=1 vol=0 {describe(state_of(st))}"

    def c_tabs(self):
        seen = [self.status()["menu"]]
        if seen[0] != "none":
            raise Check(f"menu already open: {seen[0]}")
        for want in TABS[1:]:
            self.tap("side")
            st = self.until(lambda s, w=want: s["menu"] == w, TAB_WAIT_S, f"tab {want}")
            seen.append(st["menu"])
        return " > ".join("chat" if m == "none" else m for m in seen)

    def c_theme(self):
        st = self.goto("theme")
        sel, count = pair(st, "sel")
        self.theme_count = count
        clicks = 1 if sel > 0 else -1                   # clockwise = previous row
        before = (st["theme"], st["mode"])
        pos = log_size()
        self.send(f"wheel {clicks}")
        st1 = self.until(lambda s: s["theme"] != before[0], what="theme change by wheel")
        _, ok = self.wait_lines(lambda ls: bool(theme_events(ls)), pos, 2.0)
        if not ok or st1["mode"] != before[1]:
            raise Check(f"wheel: no [UI] theme line or mode changed ({self.brief(st1)})")
        pos = log_size()
        self.tap("enc")
        st2 = self.until(lambda s: s["mode"] != before[1], what="light/dark flip by encoder")
        evs = theme_events(read_log(pos)[0])
        if not evs or evs[-1] != (st2["theme"], st2["mode"]):
            raise Check(f"encoder: [UI] theme line mismatch {evs}")
        if st2["theme"] != st1["theme"]:
            raise Check("encoder changed the family")
        code = int(self.start["themecode"])
        self.send(f"theme {code}")
        self.until(lambda s: int(s["themecode"]) == code, what="theme restore")
        return (f"wheel {before[0]} -> {st1['theme']}; encoder {before[1]} -> {st2['mode']}; "
                f"restored")

    def inverted(self):
        # Retries keep the first position, as status() does: a reply held in a full
        # 64-byte USB packet is released by the next request's output.
        pos = log_size()
        for _ in range(4):
            self.send("panel status", 0.1)
            lines, ok = self.wait_lines(lambda ls: panel_inverted(ls) is not None, pos, 1.5)
            if ok:
                return panel_inverted(lines)
        raise DeviceError("no `panel status` reply (flash a current bench build)")

    def c_panel(self):
        try:
            return self.panel_walk()
        finally:                                         # the start theme back, pass or fail
            code = int(self.start["themecode"])
            self.leave_menu()
            self.send(f"theme {code}")
            self.until(lambda s: int(s["themecode"]) == code, what="theme restore")

    def panel_walk(self):
        st = self.goto("theme")
        if not self.inverted():
            raise Check("panel inversion already off on entering the Theme tab")
        _, count = pair(st, "sel")
        seen = []
        self.send(f"wheel {count}")                      # to the first row (clockwise = up)
        st = self.until(lambda s: pair(s, "sel")[0] == 0, what="first theme")
        for row in range(count):
            if row:
                self.send("wheel -1")
                st = self.until(lambda s, r=row: pair(s, "sel")[0] == r, what="next theme")
            for _ in range(2):
                was = st["mode"]
                self.tap("enc")
                st = self.until(lambda s, w=was: s["mode"] != w, what="light/dark flip")
                time.sleep(0.3)                          # the loop pass after the render
                if not self.inverted():
                    raise Check(f"panel inversion off after {st['theme']} {st['mode']}")
                seen.append(f"{st['theme']} {st['mode']}")
        self.leave_menu()
        time.sleep(0.3)
        if not self.inverted():
            raise Check("panel inversion off after closing the menu")
        return f"inversion on through {len(seen)} theme renders and the menu closing"

    def kept_hex(self):
        """The kept look's record as the device prints it: 32 hex digits, or `orig`."""
        for _ in range(3):
            pos = log_size()
            self.send("avatar look", 0.1)
            lines, ok = self.wait_lines(lambda ls: parse_look(ls) is not None, pos,
                                        STATUS_WAIT_S)
            if ok:
                return parse_look(lines)
        raise DeviceError("no `avatar look` reply (flash a current bench build)")

    def c_chirper(self):
        st = self.goto("chirper")
        kept, shown = tag_pair(st, "look")
        if shown != kept:
            raise Check(f"entering the tab shows {shown}, not the kept look {kept}")
        self.send("wheel -1")                            # counter-clockwise = next character
        st1 = self.until(lambda s: (tag_pair(s, "look") or (None, shown))[1] != shown,
                         what="shown look change")
        k1, s1 = tag_pair(st1, "look")
        if k1 != kept:
            raise Check(f"browsing changed the kept look {kept} -> {k1}")
        self.tap("side")                                 # leave without keeping
        st2 = self.until(lambda s: s["menu"] != "chirper", TAB_WAIT_S, "leave chirper")
        k2, s2 = tag_pair(st2, "look")
        if (k2, s2) != (kept, kept):
            raise Check(f"after leaving look={k2}/{s2}, expected {kept}/{kept}")
        return f"kept {kept}; browsed to {s1}; left, back to {k2}/{s2}"

    def c_text(self):
        st = self.goto("text")
        sel, count = pair(st, "sel")
        clicks = 1 if sel > 0 else -1
        font0, size0 = st["font"], st["size"]
        pos = log_size()
        self.send(f"wheel {clicks}")
        st1 = self.until(lambda s: s["font"] != font0, what="font change by wheel")
        _, ok = self.wait_lines(lambda ls: bool(font_events(ls)), pos, 2.0)
        if not ok:
            raise Check("wheel: no [UI] font line")
        self.tap("enc")
        want = next_size(st1["size"])
        st2 = self.until(lambda s: s["size"] == want, what=f"size {st1['size']} -> {want}")
        if st2["font"] != st1["font"]:
            raise Check("encoder changed the family")
        self.send(f"font {self.start['font']} {self.start['size']}", 0.5)
        self.until(lambda s: (s["font"], s["size"]) == (self.start["font"], self.start["size"]),
                   what="font restore")
        return f"wheel {font0} -> {st1['font']}; encoder {size0} -> {st2['size']}; restored"

    def c_view(self):
        orig = self.start["view"]
        other = "transcript" if orig == "character" else "character"
        self.send(f"view {other}")
        self.until(lambda s: s["view"] == other, what=f"view {other}")
        self.send(f"view {orig}")
        self.until(lambda s: s["view"] == orig, what=f"view {orig}")
        # The chord (dial key held, side key tapped) flips the view too; the
        # recording the dial press started is dropped, and no menu opens. It
        # needs the chat screen: in the menu the side key steps tabs.
        self.leave_menu()
        for want in (other, orig):
            self.send("enc down", 0.5)
            self.tap("side", hold=0.15, after=0.3)
            self.send("enc up", 0.8)
            st = self.until(lambda s, w=want: s["view"] == w, what=f"chord: view {want}")
            if st["state"] != "idle" or st["menu"] != "none":
                raise Check(f"chord left state={st['state']} menu={st['menu']}")
        return f"{orig} -> {other} -> {orig}; chord {orig} -> {other} -> {orig}"

    def leave_menu(self):
        st = self.status()
        for _ in range(len(TABS)):
            if st["menu"] == "none":
                return st
            self.tap("side")
            st = self.until(lambda s, was=st["menu"]: s["menu"] != was, TAB_WAIT_S, "leave menu")
        raise Check("menu will not close")

    def c_heap(self):
        """`heap all` with the transcript view up (the character view does not paint
        the transcript, so its render/commit numbers are not comparable); the view is
        put back afterwards."""
        self.leave_menu()
        orig = self.status()["view"]
        if orig != "transcript":
            self.send("view transcript")
            self.until(lambda s: s["view"] == "transcript", what="view transcript")
        try:
            time.sleep(1.0)
            pos = log_size()
            self.send("heap all", 0.3)
            lines, ok = self.wait_lines(heap_done, pos, HEAP_WAIT_S, poll=1.0)
            if not ok:
                raise Check("no [Heap] bench-end within %ds (%s)"
                            % (HEAP_WAIT_S, heap_wait_evidence(lines)))
            return check_heap(lines, self.args.heap_tol, self.args.heap_floor, "transcript")
        finally:
            if orig != "transcript":
                self.send(f"view {orig}")
                self.until(lambda s: s["view"] == orig, what=f"view {orig}")

    def new_conversation(self):
        self.leave_menu()
        self.tap("side")
        st = self.until(lambda s: s["menu"] == "chats", TAB_WAIT_S, "Chats tab")
        if pair(st, "sel")[0] != 0:
            raise Check("Chats selection is not row 0")
        self.tap("enc", 0.2, 1.5)
        self.until(lambda s: s["menu"] == "none", what="menu closed by New chat")

    def c_turn(self):
        self.new_conversation()
        self.until(lambda s: s["state"] == "idle", what="idle")
        pos = log_size()
        self.send("ask " + QUESTION)
        lines, ok = self.wait_lines(
            lambda ls: any(l.startswith("[Speak] end") or l.startswith("[Bench] ask refused")
                           for l in ls), pos, TURN_WAIT_S, poll=0.5)
        if not ok:
            raise Check("no [Speak] end within %ds" % TURN_WAIT_S)
        time.sleep(0.5)
        return turn_verdict(read_log(pos)[0])

    def c_reboot(self):
        """Change every persisted setting, hard-reset, and see them come back."""
        s0 = self.start
        self.leave_menu()
        self.send(f"theme {(s0['themecode'] + 2) % (2 * self.theme_count)}")   # next family, same mode
        other_font = "Atkinson" if s0["font"] != "Atkinson" else "Inter"
        self.send(f"font {other_font} {next_size(s0['size'])}", 0.5)
        self.send(f"view {'transcript' if s0['view'] == 'character' else 'character'}")
        self.send(f"avatar variant {1 if s0['look'] == 'orig' else 0}")
        want = self.until(lambda s: s["font"] == other_font and s["size"] == next_size(s0["size"])
                          and s["view"] != s0["view"]
                          and (tag_pair(s, "look") or (s0["look"],))[0] != s0["look"],
                          what="settings changed before reboot")
        want = state_of(want)
        if want["themecode"] == s0["themecode"]:
            raise Check("theme did not change before the reboot")

        self.reboot_pos = log_size()
        self.close()
        # esptool needs the port to itself: with the watcher still reading, the
        # trailing reset can fail and leave the S3 in ROM download mode
        # (AGENTS.md flashing steps 1 and 4). Stop the watcher, reset (retry
        # once, which also recovers download mode), restart it detached.
        subprocess.run(["pkill", "-f", "serial-watch.py"], stdout=subprocess.DEVNULL)
        time.sleep(0.5)
        cmd = [ESPTOOL_PYTHON, "-m", "esptool", "--chip", "esp32s3", "--port", self.port,
               "--after", "hard_reset", "chip_id"]
        rc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
        if rc != 0:
            time.sleep(2.0)
            self.port = provision.pick_port(None) or self.port   # may have renumbered
            cmd[cmd.index("--port") + 1] = self.port
            rc = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL).returncode
        time.sleep(1.5)                          # the S3 re-enumerates after the reset
        self.port = provision.pick_port(None) or self.port
        restart_watcher(self.port)
        time.sleep(1.5)
        self.reopen()

        lines, ok = self.wait_lines(
            lambda ls: any(l.startswith("[OTA] running") or
                           l.startswith("[OTA] skipped: no manifest source")
                           for l in ls) and
            any(l.startswith("[SelfTest]") for l in ls), self.reboot_pos, BOOT_WAIT_S, poll=1.0)
        healthy, boot_ev = boot_health(lines)
        if not (ok and healthy):
            raise Check(f"after reboot (esptool rc={rc}): {boot_ev}")
        time.sleep(1.0)
        self.send("vol 0", 0.3)                 # the volume is back at 21 after a boot
        st = state_of(self.until(lambda s: s["state"] == "idle", 30, "idle after boot"))
        diffs = [f"{k} {want[k]} -> {st[k]}" for k in
                 ("themecode", "font", "size", "view", "look") if st[k] != want[k]]
        if diffs:
            raise Check("not persisted: " + "; ".join(diffs))
        return f"{boot_ev}; persisted {describe(st)}"

    def c_faults(self):
        if self.reboot_pos is None:
            lines, _ = read_log(self.start_pos)
            bad = scan_faults(lines)
        else:
            before, _ = read_log(self.start_pos, self.reboot_pos)
            after, _ = read_log(self.reboot_pos)
            lines = before + after
            bad = scan_faults(before) + scan_faults(after, REBOOT_RST_ALLOWED)
        if bad:
            raise Check(f"{len(bad)}: {bad[0]}")
        return f"none in {len(lines)} log lines"

    # ------------------------------------------------------------ restore
    def restore(self):
        """Back to the starting theme, font, size, view and kept look, menu closed."""
        s0 = self.start
        try:
            self.leave_menu()
            self.send(f"theme {s0['themecode']}")
            self.send(f"font {s0['font']} {s0['size']}", 0.5)
            self.send(f"view {s0['view']}")
            if s0["look"] == "orig":
                self.send("avatar variant 0")
            elif self.start_look:
                self.send(f"avatar look {self.start_look}")
            else:
                raise Check("the starting look record was not read")
            st = self.until(lambda s: state_of(s) == s0, 5.0, "restore")
        except (Check, DeviceError) as exc:
            return False, f"restore failed: {exc}"
        return True, describe(state_of(st))

    # ------------------------------------------------------------ run
    def run(self, args):
        self.args = args
        try:
            self.check("boot", lambda: self.c_boot(self.start_pos - min(self.start_pos, 1 << 20)))
            self.send("vol 0", 0.3)
            st = self.status()
            if st.get("state") != "idle" or st.get("menu") != "none":
                raise SystemExit(f"device not idle on the chat screen "
                                 f"(state={st.get('state')} menu={st.get('menu')}); nothing changed")
            if missing_keys(st):
                self.rows.append({"name": "status", "ok": False,
                                  "evidence": "status lacks " + ",".join(missing_keys(st)) +
                                              " (flash a current bench build)"})
                return
            self.start = state_of(st)
            if self.start["look"] != "orig":
                self.start_look = self.kept_hex()          # restored exactly at the end
            self.check("status", self.c_status)
            self.check("tabs", self.c_tabs)
            self.check("theme", self.c_theme)
            self.check("panel", self.c_panel)
            self.check("chirper", self.c_chirper)
            self.check("text", self.c_text)
            self.check("view", self.c_view)
            self.check("heap", self.c_heap)
            if args.turn:
                self.check("turn", self.c_turn)
            if args.reboot:
                self.check("reboot", self.c_reboot)
        except DeviceError as exc:
            if not self.rows or self.rows[-1]["evidence"] != str(exc):
                self.rows.append({"name": "device", "ok": False, "evidence": str(exc)})
        finally:
            if self.start:
                try:
                    ok, ev = self.restore()
                except Exception as exc:                  # restore never hides the real failure
                    ok, ev = False, f"restore failed: {exc}"
                self.rows.append({"name": "restore", "ok": ok, "evidence": ev})
            try:
                self.check("faults", self.c_faults)
            except Exception as exc:
                self.rows.append({"name": "faults", "ok": False, "evidence": str(exc)})
            self.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="serial port (default: the first /dev/ttyACM*)")
    ap.add_argument("--turn", action="store_true",
                    help="also run one text turn in a new conversation (asks Hermes)")
    ap.add_argument("--reboot", action="store_true",
                    help="also hard-reset the device and check settings persist")
    ap.add_argument("--json", help="write the results here")
    ap.add_argument("--heap-tol", type=int, default=HEAP_TOL_DEFAULT,
                    help="bytes the settled heap (bench-end vs bench-start, free and largest "
                         "block) may end below its start (%d)" % HEAP_TOL_DEFAULT)
    ap.add_argument("--heap-floor", type=int, default=HEAP_FLOOR_DEFAULT,
                    help="fail when the internal-heap low watermark is below this (%d)"
                         % HEAP_FLOOR_DEFAULT)
    args = ap.parse_args()

    if not os.path.exists(LOG):
        sys.exit(f"{LOG} missing: start host/serial-watch.py first")
    if not provision.watcher_running():
        sys.exit("serial-watch.py is not running: start it first")
    port = provision.pick_port(args.port) or (sorted(glob.glob("/dev/ttyACM*")) or [None])[0]
    if not port:
        sys.exit("no /dev/ttyACM* port")

    smoke = Smoke(port)
    interrupted = None
    try:
        smoke.run(args)
    except KeyboardInterrupt:
        interrupted = "interrupted"
    except SystemExit as exc:
        print(exc, file=sys.stderr)
        smoke.close()
        sys.exit(2)
    rows = smoke.rows
    if interrupted:
        rows.append({"name": "run", "ok": False, "evidence": interrupted})
    print(table(rows))
    ok = all(r["ok"] for r in rows)
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"ok": ok, "port": smoke.port, "start": smoke.start, "rows": rows}, f,
                      indent=1)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

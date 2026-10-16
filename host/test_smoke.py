#!/usr/bin/env python3
"""Offline tests for host/smoke.py's log and status parsing (no device needed).

Sample lines follow the formats printed by firmware/src/bench.cpp,
firmware/src/bench_heap.cpp and the boot code.

    python3 host/test_smoke.py
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smoke  # noqa: E402

STATUS = ("[Bench] state=idle menu=none sel=0/0 voice=0 hearing=0 scroll=0/0 follow=1 vol=0 "
          "playing=0 wifi=1 power=usb view=character avatar=idle look=orig/orig font=Paper Mono "
          "size=L line=21 theme=Rose Pine mode=dark themecode=6")
STATUS_THEME_TAB = ("[Bench] state=idle menu=theme sel=3/6 voice=0 hearing=0 scroll=0/0 follow=1 "
                    "vol=0 playing=0 wifi=1 power=usb view=transcript avatar=idle look=1a2b3c4d/orig "
                    "font=Atkinson size=M line=18 theme=Gruvbox mode=light themecode=5")
OLD_STATUS = ("[Bench] state=idle menu=none sel=0/0 voice=0 hearing=0 scroll=0/0 follow=1 vol=0 "
              "playing=0 wifi=1 power=usb view=character avatar=idle look=orig/orig font=Paper Mono "
              "size=L line=21")

BOOT = ["[WiFi] connected (rssi -30)", "[SelfTest] ok", "[OTA] running v1.2-3-gabc123",
        "[OTA] no firmware published; skipping",
        "[Heap] boot int free=166424 largest=155636 min=161448 psram free=7392463 largest=7340020"]


TOL, FLOOR = smoke.HEAP_TOL_DEFAULT, smoke.HEAP_FLOOR_DEFAULT


def heap_line(name, free0=150000, free1=150000, low=148000, mn=140000, n=40):
    return (f"[HeapBench] {name:<8} n={n} avg=1500us max=9000us | int free {free0}->{free1} "
            f"low={low} largest 98000->97000 low=90000 | psram free 7000000->7000000 | min={mn}")


def mark(tag, free=150000, largest=98000, mn=140000):
    return f"[Heap] {tag} int free={free} largest={largest} min={mn} psram free=7000000 largest=6990000"


# A whole `heap all` run as the device prints it: `commit` ends with its 40 turns still
# held (its own after is far below its before, by design); the settled end is back at start.
def run_lines(start=None, end=None, workloads=None):
    ws = workloads or [heap_line(n) if n != "commit" else heap_line(n, 150000, 105000, 105000)
                       for n in smoke.HEAP_WORKLOADS]
    return ([start or mark("bench-start")] + ws +
            ["[HeapBench] font-cost body_loaded_free=151000 title_loaded_free=150500 "
             "(title minus body -500 B)", end or mark("bench-end", 149000)])


HEAP_OK = run_lines()


class Status(unittest.TestCase):
    def test_parse_keeps_values_with_spaces(self):
        st = smoke.parse_status(STATUS)
        self.assertEqual(st["font"], "Paper Mono")
        self.assertEqual(st["theme"], "Rose Pine")
        self.assertEqual(st["size"], "L")
        self.assertEqual(st["themecode"], "6")
        self.assertEqual(st["mode"], "dark")
        self.assertEqual(smoke.missing_keys(st), [])

    def test_not_a_status_line(self):
        self.assertIsNone(smoke.parse_status("[Bench] > status"))
        self.assertIsNone(smoke.parse_status("[UI] theme Gruvbox dark"))
        self.assertIsNone(smoke.parse_status(""))

    def test_old_firmware_lacks_theme(self):
        st = smoke.parse_status(OLD_STATUS)
        self.assertEqual(smoke.missing_keys(st), ["theme", "mode", "themecode"])

    def test_pairs(self):
        st = smoke.parse_status(STATUS_THEME_TAB)
        self.assertEqual(smoke.pair(st, "sel"), (3, 6))
        self.assertIsNone(smoke.pair(st, "nope"))
        self.assertIsNone(smoke.pair(st, "look"))

    def test_tag_pairs(self):
        st = smoke.parse_status(STATUS_THEME_TAB)
        self.assertEqual(smoke.tag_pair(st, "look"), ("1a2b3c4d", "orig"))
        self.assertEqual(smoke.tag_pair(smoke.parse_status(STATUS), "look"), ("orig", "orig"))
        self.assertEqual(smoke.tag_pair({"look": "00ff00ff/89abcdef"}, "look"),
                         ("00ff00ff", "89abcdef"))
        self.assertIsNone(smoke.tag_pair(st, "nope"))
        for bad in ("3/7", "orig", "orig/", "1a2b3c4/orig", "1A2B3C4D/orig", "1a2b3c4d/xyz",
                    "orig/orig/orig"):
            self.assertIsNone(smoke.tag_pair({"look": bad}, "look"), bad)

    def test_state_and_describe(self):
        s = smoke.state_of(smoke.parse_status(STATUS))
        self.assertEqual(s, {"themecode": 6, "font": "Paper Mono", "size": "L",
                             "view": "character", "look": "orig", "theme": "Rose Pine",
                             "mode": "dark"})
        self.assertEqual(smoke.describe(s), "Rose Pine dark, Paper Mono L, character, look orig")
        s = smoke.state_of(smoke.parse_status(STATUS_THEME_TAB))
        self.assertEqual(s["look"], "1a2b3c4d")

    def test_parse_look(self):
        rec = "0102030405060708090a0b0c0d0e0f10"
        self.assertEqual(smoke.parse_look(["[Bench] > avatar look", f"[Bench] look {rec}"]), rec)
        self.assertEqual(smoke.parse_look(["[Bench] look orig"]), "orig")
        self.assertEqual(smoke.parse_look([f"[Bench] look {rec}", "[Bench] look orig"]), "orig")
        self.assertIsNone(smoke.parse_look(["[Bench] > avatar look", "[Bench] avatar look: x"]))
        self.assertIsNone(smoke.parse_look(["[Bench] look 0102"]))
        self.assertIsNone(smoke.parse_look([]))

    def test_last_status_ignores_echo_and_other_lines(self):
        lines = ["[Bench] > status", STATUS_THEME_TAB, "[Bench] > status", "[Menu] x"]
        self.assertEqual(smoke.last_status(lines)["menu"], "theme")
        self.assertIsNone(smoke.last_status(["[Bench] > status"]))

    def test_next_size(self):
        self.assertEqual([smoke.next_size(c) for c in "SML"], ["M", "L", "S"])


class Events(unittest.TestCase):
    def test_theme_and_font_lines(self):
        lines = ["[UI] theme Rose Pine light", "[UI] theme Tokyo Night dark", "noise"]
        self.assertEqual(smoke.theme_events(lines),
                         [("Rose Pine", "light"), ("Tokyo Night", "dark")])
        self.assertEqual(smoke.font_events(["[UI] font Departure Mono M line=18"]),
                         [("Departure Mono", "M")])

    def test_panel_inversion_bit(self):
        on = "[Bench] panel st=0d4532400 dpm=138 madctl=150 colmod=00b dim=040"
        off = "[Bench] panel st=0d4530400 dpm=138 madctl=150 colmod=00b dim=000"
        self.assertIs(smoke.panel_inverted([on]), True)
        self.assertIs(smoke.panel_inverted([off]), False)
        self.assertIs(smoke.panel_inverted([on, "noise", off]), False)   # the last reply counts
        self.assertIsNone(smoke.panel_inverted(["[Bench] > panel status", "noise"]))


class Boot(unittest.TestCase):
    def test_healthy(self):
        ok, ev = smoke.boot_health(BOOT)
        self.assertTrue(ok)
        self.assertIn("v1.2-3-gabc123", ev)

    def test_selftest_problem(self):
        ok, ev = smoke.boot_health(["[SelfTest] no microphone", "[OTA] running v1"])
        self.assertFalse(ok)
        self.assertIn("no microphone", ev)

    def test_missing(self):
        ok, ev = smoke.boot_health(["[WiFi] x"])
        self.assertFalse(ok)
        self.assertIn("missing", ev)

    def test_latest_boot_wins(self):
        ok, _ = smoke.boot_health(["[SelfTest] bad", "[SelfTest] ok", "[OTA] running a"])
        self.assertTrue(ok)


class Heap(unittest.TestCase):
    def test_parse(self):
        w = smoke.parse_heap(HEAP_OK)
        self.assertEqual([x["name"] for x in w], list(smoke.HEAP_WORKLOADS))
        self.assertEqual(w[0]["free0"], 150000)
        self.assertEqual(w[0]["min"], 140000)
        self.assertEqual(w[0]["n"], 40)

    def test_pass_although_commit_ends_below_its_start(self):
        commit = next(w for w in smoke.parse_heap(HEAP_OK) if w["name"] == "commit")
        self.assertLess(commit["free1"], commit["free0"] - 40000)   # the old per-workload rule failed this
        ok, ev = smoke.check_heap(HEAP_OK, TOL, FLOOR)
        self.assertTrue(ok, ev)
        self.assertIn("settled free 150000->149000 (-1000)", ev)
        self.assertIn("heaviest commit -45000 B", ev)      # per-workload numbers stay as information

    def test_view_is_reported(self):
        ok, ev = smoke.check_heap(HEAP_OK, TOL, FLOOR, "transcript")
        self.assertTrue(ok)
        self.assertIn("transcript view", ev)

    def test_settled_leak(self):
        ok, ev = smoke.check_heap(run_lines(end=mark("bench-end", 150000 - TOL - 1)), TOL, FLOOR)
        self.assertFalse(ok)
        self.assertIn("leak: free", ev)

    def test_settled_fragmentation(self):
        ok, ev = smoke.check_heap(run_lines(end=mark("bench-end", 150000, 98000 - TOL - 1)),
                                  TOL, FLOOR)
        self.assertFalse(ok)
        self.assertIn("fragmentation", ev)

    def test_tolerance_boundary(self):
        self.assertTrue(smoke.check_heap(run_lines(end=mark("bench-end", 150000 - TOL)), TOL, FLOOR)[0])
        self.assertFalse(smoke.check_heap(run_lines(end=mark("bench-end", 150000 - TOL - 1)),
                                          TOL, FLOOR)[0])
        self.assertTrue(smoke.check_heap(run_lines(end=mark("bench-end", 150000 - 5000)), 5000,
                                         FLOOR)[0])

    def test_gain_is_not_a_leak(self):
        ok, ev = smoke.check_heap(run_lines(end=mark("bench-end", 152000, 100000)), 0, FLOOR)
        self.assertTrue(ok, ev)

    def test_low_floor(self):
        lines = run_lines(workloads=[heap_line(n, mn=80000) for n in smoke.HEAP_WORKLOADS])
        ok, ev = smoke.check_heap(lines, TOL, FLOOR)
        self.assertFalse(ok)
        self.assertIn(f"min 80000 < {FLOOR}", ev)

    def test_floor_is_a_parameter(self):
        lines = run_lines(workloads=[heap_line(n, mn=100000) for n in smoke.HEAP_WORKLOADS])
        self.assertTrue(smoke.check_heap(lines, TOL, 100000)[0])
        self.assertFalse(smoke.check_heap(lines, TOL, 100001)[0])

    def test_defaults(self):
        self.assertEqual(smoke.HEAP_FLOOR_DEFAULT, 96 * 1024)
        self.assertEqual(smoke.HEAP_TOL_DEFAULT, 6 * 1024)

    def test_heap_line_min_counts(self):
        lines = HEAP_OK + [mark("bench-start", 1, 1, 50000)]
        self.assertEqual(smoke.heap_min(lines, smoke.parse_heap(lines)), 50000)

    def test_missing_workload(self):
        ok, ev = smoke.check_heap([mark("bench-start"), heap_line("render"), mark("bench-end")],
                                  TOL, FLOOR)
        self.assertFalse(ok)
        self.assertIn("missing commit", ev)

    def test_missing_start_or_end(self):
        ok, ev = smoke.check_heap(HEAP_OK[1:], TOL, FLOOR)
        self.assertFalse(ok)
        self.assertIn("no bench-start", ev)
        ok, ev = smoke.check_heap(HEAP_OK[:-1], TOL, FLOOR)
        self.assertFalse(ok)
        self.assertIn("no bench-end", ev)

    def test_refused(self):
        ok, ev = smoke.check_heap(["[HeapBench] refused: needs idle, no menu, nothing playing"],
                                  TOL, FLOOR)
        self.assertFalse(ok)
        self.assertIn("not idle", ev)

    def test_done_detection(self):
        self.assertTrue(smoke.heap_done(HEAP_OK))
        self.assertFalse(smoke.heap_done(HEAP_OK[:-1]))
        self.assertTrue(smoke.heap_done(["[HeapBench] refused: needs idle"]))

    def test_output_interleaved_in_front_of_a_line(self):
        # Another task's output can land mid-line on the serial stream: the markers are
        # matched anywhere in the line, not only at its start.
        noisy = ["[W][wifi] reconnect" + l if l.startswith(("[Heap] bench", "[HeapBench] c"))
                 else l for l in HEAP_OK]
        self.assertTrue(all(not l.startswith("[Heap]") for l in noisy if "bench-end" in l))
        self.assertTrue(smoke.heap_done(noisy))
        ok, ev = smoke.check_heap(noisy, TOL, FLOOR)
        self.assertTrue(ok, ev)

    def test_wait_evidence(self):
        ev = smoke.heap_wait_evidence(HEAP_OK[:3])
        self.assertIn("bench-start=yes", ev)
        self.assertIn("workloads=[render,commit]", ev)
        self.assertIn("bench-start=no", smoke.heap_wait_evidence([]))



class Faults(unittest.TestCase):
    def test_clean(self):
        self.assertEqual(smoke.scan_faults(BOOT + [STATUS]), [])

    def test_markers(self):
        for bad in ("Guru Meditation Error: Core  1 panic'ed", "abort() was called at PC 0x4",
                    "E (123) task_wdt: Task watchdog got triggered", "Backtrace: 0x4:0x3"):
            self.assertEqual(len(smoke.scan_faults(["[x] ok", bad])), 1, bad)

    def test_unexpected_rst(self):
        line = "rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)"
        self.assertEqual(smoke.scan_faults([line]), [line])
        self.assertEqual(smoke.scan_faults([line], rst_allowed=1), [])
        self.assertEqual(len(smoke.scan_faults([line] * 3, rst_allowed=2)), 1)


class Turn(unittest.TestCase):
    def test_ok(self):
        ok, ev = smoke.turn_verdict(["[Lat] play0 +900ms", "[Speak] end done chunk=1/1"])
        self.assertTrue(ok)
        self.assertEqual(ev, "[Speak] end done chunk=1/1")

    def test_abort_and_fetch_fail(self):
        self.assertFalse(smoke.turn_verdict(["[Speak] end abort chunk=1/2"])[0])
        self.assertFalse(smoke.turn_verdict(["[Speak] end fetch-fail chunk=1/2"])[0])

    def test_hermes_error(self):
        ok, ev = smoke.turn_verdict(["[Hermes] HTTP 401: nope", "[Speak] end done chunk=1/1"])
        self.assertFalse(ok)
        self.assertIn("Hermes error", ev)

    def test_refused_and_no_end(self):
        self.assertFalse(smoke.turn_verdict(["[Bench] ask refused: not idle"])[0])
        self.assertFalse(smoke.turn_verdict([])[0])


class Table(unittest.TestCase):
    def test_table(self):
        out = smoke.table([{"name": "boot", "ok": True, "evidence": "a"},
                           {"name": "heap", "ok": False, "evidence": "leak"}])
        self.assertIn("boot", out)
        self.assertIn("FAIL", out)
        self.assertIn("1/2 passed", out)
        self.assertIn("FAILED", out)


class ReadLog(unittest.TestCase):
    def test_offsets_and_partial_lines(self):
        with tempfile.NamedTemporaryFile("wb", delete=False) as f:
            f.write(b"one\r\ntwo\nthr")
            path = f.name
        old, smoke.LOG = smoke.LOG, path
        try:
            lines, pos = smoke.read_log(0)
            self.assertEqual(lines, ["one", "two"])
            self.assertEqual(pos, 9)
            with open(path, "ab") as f:
                f.write(b"ee\nfour\n")
            self.assertEqual(smoke.read_log(pos)[0], ["three", "four"])
            self.assertEqual(smoke.read_log(0, 5)[0], ["one"])
            self.assertEqual(smoke.log_size(), os.path.getsize(path))
        finally:
            smoke.LOG = old
            os.unlink(path)

    def with_log(self, content):
        f = tempfile.NamedTemporaryFile("wb", delete=False)
        f.write(content)
        f.close()
        self.addCleanup(os.unlink, f.name)
        self.addCleanup(setattr, smoke, "LOG", smoke.LOG)
        smoke.LOG = f.name
        return f.name

    def wait_heap(self, pos, timeout=0.3):
        return smoke.Smoke.wait_lines(None, smoke.heap_done, pos, timeout, poll=0.02)

    def test_run_split_across_reads_is_seen_once_complete(self):
        body = ("\n".join(HEAP_OK) + "\n").encode()
        path = self.with_log(b"[Bench] > status\n")
        pos = smoke.log_size()
        cut = body.index(b"bench-end") + 3          # mid-way through the last line
        with open(path, "ab") as f:
            f.write(body[:cut])
        self.assertFalse(self.wait_heap(pos)[1])
        with open(path, "ab") as f:
            f.write(body[cut:])
        lines, ok = self.wait_heap(pos)
        self.assertTrue(ok)
        self.assertTrue(smoke.check_heap(lines, TOL, FLOOR)[0])

    def test_control_characters_do_not_split_the_marker_line(self):
        # The run is in the log, whole; str.splitlines() cut its marker line at a form
        # feed / NEL and the wait never saw "[Heap] bench-end" (a full-length timeout).
        for noise in (b"\x0c", b"\x1c", b"\xc2\x85", b"\xe2\x80\xa8"):
            lines = [l.encode() for l in HEAP_OK]
            lines[-1] = lines[-1].replace(b"bench-end", b"bench-" + noise + b"end")
            self.with_log(b"[Bench] > heap all\n" + b"\n".join(lines) + b"\n")
            got, _ = smoke.read_log(0)
            self.assertEqual(len(got), len(HEAP_OK) + 1, noise)
            self.assertTrue(any("bench-" in l and "end" in l for l in got), noise)

    def test_truncated_log_is_read_from_its_start(self):
        # The log shrank under the saved offset (replaced or truncated): the wait read
        # nothing, forever, though the run was in the new file.
        path = self.with_log(b"x" * 5000 + b"\n")
        pos = smoke.log_size()
        with open(path, "wb") as f:
            f.write(("\n".join(HEAP_OK) + "\n").encode())
        self.assertLess(smoke.log_size(), pos)
        lines, ok = self.wait_heap(pos)
        self.assertTrue(ok)
        self.assertTrue(smoke.check_heap(lines, TOL, FLOOR)[0])

    def test_real_log_shape(self):
        # CRLF line ends, the echoed command, other tasks' lines around the run.
        text = ("[Bench] > heap all\r\n" + "\r\n".join(
            [HEAP_OK[0], "[Menu] 18 sessions loaded"] + HEAP_OK[1:]) + "\r\n[Bench] > status\r\n")
        self.with_log(text.encode())
        lines, ok = self.wait_heap(0)
        self.assertTrue(ok)
        self.assertTrue(smoke.check_heap(lines, TOL, FLOOR)[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""Measure answer latency on a bench build (-DBENCH_CMDS) over USB serial.

Runs text turns (`ask`, STT skipped) and reads the firmware's `[Lat]` lines
(firmware/include/turn.h) from the serial-watch log. Milestones, measured from
the moment the Hermes request starts:

    first_token    first streamed word from Hermes
    chunk0_queued  first speech chunk handed to TTS (sentence packing done)
    chunk0_staged  first chunk synthesized and downloaded
    play0          first audio starts            <- what the listener waits for
    stream_end     Hermes finished streaming
    gap            silence between consecutive chunks (eof -> next play)

Needs serial-watch.py running (it owns the reads). Sets volume 0. With
--new-chat it first picks "+ New conversation" so bench turns don't land in
an existing chat.

    host/latency.py [--port /dev/ttyACM0] [--runs 2] [--new-chat] [--label NAME]
                    [--json out.json]
"""

import argparse
import json
import os
import re
import statistics
import time
import tty

import provision

QUESTIONS = [
    "What is two plus two?",
    "Name three primary colors.",
    "Tell me a fun fact about octopuses.",
    "How many days are in a leap year?",
    "Give me one tip for sleeping better.",
    "What is the capital of Japan, and what is it known for?",
    "Explain in a few sentences why the sky is blue.",
    "Suggest a quick dinner I can make with eggs and rice.",
]
MILESTONES = ("first_token", "chunk0_queued", "chunk0_staged", "play0", "stream_end")
LAT_RE = re.compile(r"^\[Lat\] (\S+) \+(\d+)ms")
TURN_TIMEOUT_S = 150


class Device:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)

    def send(self, line, pause=0.3):
        os.write(self.fd, (line + "\n").encode())
        time.sleep(pause)          # the firmware reads one line per loop pass


def log_lines_since(pos):
    with open(provision.LOG, "rb") as f:
        f.seek(pos)
        return f.read().decode(errors="replace").splitlines()


def run_turn(dev, question):
    pos = os.path.getsize(provision.LOG)
    dev.send("ask " + question)
    deadline = time.time() + TURN_TIMEOUT_S
    while time.time() < deadline:
        time.sleep(0.5)
        lines = log_lines_since(pos)
        if any(l.startswith("[Bench] ask refused") for l in lines):
            return {"error": "refused (device not idle)"}
        done = any(l.startswith("[Speak] end") for l in lines)
        failed = next((l for l in lines if "Hermes unreachable" in l or "[TTSF] chunk" in l
                       and "failed" in l), None)
        if done or failed:
            time.sleep(0.3)
            return parse(log_lines_since(pos), failed)
    return {"error": "timeout"}


def parse(lines, failed):
    events = [(m.group(1), int(m.group(2))) for m in map(LAT_RE.match, lines) if m]
    r = {k: next((t for e, t in events if e == k), None) for k in MILESTONES}
    gaps, last_eof, played = [], None, False
    for e, t in events:
        if e.startswith("play"):
            if played and last_eof is not None:
                gaps.append(t - last_eof)
            played = True
        elif e == "eof" and played:
            last_eof = t
    r["gaps"] = gaps
    r["chunks"] = sum(1 for e, _ in events if e.startswith("play"))
    if failed:
        r["error"] = failed.strip()
    return r


def med(values):
    values = [v for v in values if v is not None]
    return int(statistics.median(values)) if values else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--runs", type=int, default=1, help="passes over the question list")
    ap.add_argument("--new-chat", action="store_true")
    ap.add_argument("--label", default="run")
    ap.add_argument("--ask", action="append", metavar="TEXT",
                    help="ask this instead of the built-in list (repeatable)")
    ap.add_argument("--json", help="write raw results here")
    args = ap.parse_args()

    if not os.path.exists(provision.LOG):
        raise SystemExit(f"{provision.LOG} missing: start host/serial-watch.py first")
    dev = Device(provision.pick_port(args.port) or args.port)
    dev.send("vol 0")
    if args.new_chat:
        dev.send("side down", 0.2)
        dev.send("side up", 6)               # menu loads the chat list
        dev.send("enc down", 0.2)
        dev.send("enc up", 1.5)              # row 0: + New conversation

    results = []
    for _ in range(args.runs):
        for q in args.ask or QUESTIONS:
            r = run_turn(dev, q)
            r["q"] = q
            results.append(r)
            print(f"{q[:38]:38}  " + "  ".join(
                f"{k}={r.get(k)}" for k in ("first_token", "chunk0_queued", "play0"))
                + f"  chunks={r.get('chunks')} gaps={r.get('gaps')}"
                + (f"  ERROR {r['error']}" if r.get("error") else ""), flush=True)
            time.sleep(2)

    ok = [r for r in results if not r.get("error")]
    all_gaps = [g for r in ok for g in r["gaps"]]
    print(f"\n{args.label}: {len(ok)}/{len(results)} turns ok (medians, ms)")
    for k in MILESTONES:
        print(f"  {k:14} {med([r[k] for r in ok])}")
    print(f"  {'gap':14} {med(all_gaps)} (n={len(all_gaps)}, max={max(all_gaps) if all_gaps else None})")
    print(f"  {'play0-token':14} {med([r['play0'] - r['first_token'] for r in ok if r['play0'] and r['first_token'] is not None])}"
          "   <- device-side wait after Hermes starts talking")
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"label": args.label, "results": results}, f, indent=1)


if __name__ == "__main__":
    main()

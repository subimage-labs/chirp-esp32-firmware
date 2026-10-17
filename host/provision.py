#!/usr/bin/env python3
"""Write the device settings over USB serial — setup without the phone portal.

Reads the gitignored firmware/device.env (template: firmware/device.env.example)
and sends one `settings {json}` line (firmware/include/console.h). The device
checks it with the setup form's rules, stores it in NVS and restarts. The
file's profile + key become one of the device's agents (up to 4) and the one
in use; agents added earlier stay. backend="venice" switches the device to
the Venice backend instead: veniceKey is required then, and the Hermes
members are ignored (they stay stored for a switch back).

Values are never printed: agents run this, and their output lands in
transcripts. Works alongside serial-watch.py (the reply is read from its log).

    host/provision.py [--port /dev/ttyACM1] [--env firmware/device.env]

host/pair.py imports send_settings() for the wired handoff and --forget.
"""

import argparse
import glob
import json
import os
import subprocess
import sys
import time
import tty

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELDS = ("ssid", "pass", "backend", "hermes", "key", "profile", "shim",
          "venice", "veniceKey")
LOG = "/tmp/chirp-serial.log"
REPLY_S = 25.0          # a booting device reads serial only after its WiFi wait (15 s)


def read_env(path):
    vals = {}
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            k, sep, v = line.partition("=")
            k, v = k.strip(), v.strip()
            if not sep or k not in FIELDS:
                sys.exit(f"{path}:{n}: expected one of {', '.join(FIELDS)} as key=\"value\"")
            if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
                v = v[1:-1]
            vals[k] = v
    required = (("veniceKey",) if vals.get("backend") == "venice"
                else ("ssid", "hermes", "key"))
    for k in required:
        if not vals.get(k):
            sys.exit(f"{path}: {k} is required")
    for k, v in vals.items():
        if v.startswith("your-"):
            sys.exit(f"{path}: {k} still has the template placeholder")
    out = {k: vals.get(k, "") for k in FIELDS}
    if not out["backend"]:
        out["backend"] = "local"      # a local env is an explicit local choice
    return out


def pick_port(preferred=None):
    if preferred and os.path.exists(preferred):
        return preferred
    ports = sorted(glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def watcher_running():
    return subprocess.run(["pgrep", "-f", "serial-watch.py"],
                          stdout=subprocess.DEVNULL).returncode == 0


def reply_in(text):
    for line in text.splitlines():
        if line.startswith("[Settings] saved") or line.startswith("[Settings] rejected"):
            return line.strip()
    return None


def send_settings(settings, port=None):
    """Send one `settings {json}` line to the device; return its reply line.

    `settings` holds only the members to change (console.h): absent ones keep
    the device's stored values. Opens the port raw (no echo), and reads the
    reply from serial-watch.py's log when the watcher runs, else from the port
    itself. Raises RuntimeError when no reply arrives within REPLY_S. Values
    are never printed; only the chosen port and the reply are.
    """
    # Empty members are omitted, not sent: an absent member keeps the stored
    # value on the device, while an explicit "" clears it (ssid, hosts, keys).
    # `pass` is the exception — its empty string means an open network.
    members = {k: v for k, v in settings.items() if v != "" or k == "pass"}
    line = ("settings " + json.dumps(members, separators=(",", ":")) + "\n").encode()

    port = pick_port(port)
    if not port:
        raise RuntimeError("no /dev/ttyACM* port")
    from_log = watcher_running() and os.path.exists(LOG)
    log_pos = os.path.getsize(LOG) if from_log else 0

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd)                     # no echo: device output must not bounce back
        sent = 0
        while sent < len(line):
            try:
                sent += os.write(fd, line[sent:])
            except BlockingIOError:
                time.sleep(0.01)
        print(f"sent settings to {port}")

        seen = b""
        deadline = time.time() + REPLY_S
        result = None
        while time.time() < deadline and not result:
            time.sleep(0.1)
            if from_log:
                with open(LOG, "rb") as f:
                    f.seek(log_pos)
                    seen = f.read()
            else:
                try:
                    seen += os.read(fd, 4096)
                except (BlockingIOError, OSError):
                    pass
            result = reply_in(seen.decode(errors="replace"))
    finally:
        os.close(fd)

    if not result:
        raise RuntimeError("no reply from the device (firmware without provisioning, or port busy)")
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default="/dev/ttyACM1")
    ap.add_argument("--env", default=os.path.join(REPO, "firmware", "device.env"))
    args = ap.parse_args()

    if not os.path.exists(args.env):
        sys.exit(f"{args.env} missing: copy firmware/device.env.example and fill it in")
    try:
        result = send_settings(read_env(args.env), args.port)
    except (ValueError, RuntimeError) as exc:
        sys.exit(str(exc))
    print(result)
    sys.exit(0 if result.startswith("[Settings] saved") else 1)


if __name__ == "__main__":
    main()

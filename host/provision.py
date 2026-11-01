#!/usr/bin/env python3
"""Write the device settings over USB serial — setup without the phone portal.

Reads the gitignored firmware/device.env (template: firmware/device.env.example)
and sends one `settings {json}` line (firmware/include/console.h). The device
checks it with the setup form's rules, stores it in NVS and restarts. Only the
keys the file mentions are sent; a key left out keeps the device's stored
value, while a key set to an empty value ("") clears that member on the device
(`pass=""` is an open network).

The file's ssid + pass add a WiFi network (the device keeps up to 5 and joins
the strongest in range; a name it already holds is edited, the oldest is
dropped past 5). The file's profile + key become one of the device's agents
(up to 4) and the one in use; agents added earlier stay. Local mode (backend
absent or "local") requires hermes and key, not ssid: the device keeps its
networks and rejects local mode when it holds none. backend="venice" switches
the device to the Venice backend instead: veniceKey is the only member
required then, and the Hermes members are ignored (they stay stored for a
switch back). Template placeholders ("your-...") are rejected.

Values are never printed: agents run this, and their output lands in
transcripts. Works alongside serial-watch.py (the reply is read from its log).

    host/provision.py [--port /dev/ttyACM1] [--env firmware/device.env]

--port names one device and is never replaced: when that path is absent the
tool stops. Without --port it uses the first /dev/ttyACM*.

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
    """The members device.env mentions, as {key: value}. An empty value is kept
    as "" (it clears that member on the device); an absent key is absent."""
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
                else ("hermes", "key"))
    for k in required:
        if not vals.get(k):
            sys.exit(f"{path}: {k} is required")
    for k, v in vals.items():
        if v.startswith("your-"):
            sys.exit(f"{path}: {k} still has the template placeholder")
    return vals


def settings_members(settings):
    """The members to send: every given key, "" included (an explicit empty
    value clears that member on the device), None omitted."""
    return {k: v for k, v in settings.items() if v is not None}


def pick_port(preferred=None):
    """`preferred` when it exists, else None: an explicitly named port is one
    device, and an absent one must never become another device's port (with
    two boards plugged in that would send settings or bench lines to the wrong
    one). Only preferred=None picks the first ttyACM."""
    if preferred is not None:
        return preferred if os.path.exists(preferred) else None
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
    the device's stored values, an explicit "" clears that member. Opens the
    port raw (no echo), and reads the reply from serial-watch.py's log when
    the watcher runs, else from the port itself. Raises RuntimeError when the
    port is absent (an explicit `port` is never replaced by another device's)
    or when no reply arrives within REPLY_S. Values are never printed; only
    the chosen port and the reply are.
    """
    line = ("settings " + json.dumps(settings_members(settings),
                                     separators=(",", ":")) + "\n").encode()

    chosen = pick_port(port)
    if not chosen:
        raise RuntimeError(f"no device on {port}" if port is not None
                           else "no /dev/ttyACM* port")
    port = chosen
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
    ap.add_argument("--port", help="serial port; stops when absent "
                    "(default: the first /dev/ttyACM*)")
    ap.add_argument("--env", default=os.path.join(REPO, "firmware", "device.env"),
                    help="settings file (default: firmware/device.env)")
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

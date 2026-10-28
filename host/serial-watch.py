#!/usr/bin/env python3
"""Append the device's serial console to a log file, surviving re-enumeration.

Holds the port open only while it exists; reopens after USB resets/flashes.
Must be stopped before flashing (any open handle breaks esptool mid-write):
    pkill -f serial-watch.py

Start detached so it never ties up a shell or tool call:
    setsid nohup host/serial-watch.py >/dev/null 2>&1 </dev/null &
"""

import argparse
import glob
import os
import termios
import time
import tty


def pick_port(preferred):
    if os.path.exists(preferred):
        return preferred
    ports = sorted(glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM1")
    ap.add_argument("--log", default="/tmp/chirp-serial.log")
    ap.add_argument("--hours", type=float, default=2.0)
    args = ap.parse_args()

    deadline = time.time() + args.hours * 3600
    fd = -1
    # The console log can hold settings echoes and conversation text: private
    # to this user (a looser file left by an older run is tightened too).
    log_fd = os.open(args.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW, 0o600)
    os.fchmod(log_fd, 0o600)
    with os.fdopen(log_fd, "ab") as out:
        midline = False                                 # the log's last byte is not a newline
        while time.time() < deadline:
            if fd >= 0 and not os.path.exists(port):   # re-enumerated away (e.g. ACM1 -> ACM0)
                os.close(fd)
                fd = -1
            if fd < 0:
                port = pick_port(args.port)
                if not port:
                    time.sleep(0.2)
                    continue
                try:
                    fd = os.open(port, os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
                except OSError:
                    time.sleep(0.2)
                    continue
                # Raw, no echo, on every open: a fresh port echoes what it
                # receives, so the device's own log would come back to it as
                # bench commands, flooding its loop after each reset or replug.
                try:
                    tty.setraw(fd, termios.TCSANOW)
                except termios.error:
                    pass                                # not a tty (a test pipe)
                if midline:                             # a line cut by the hangup ends here,
                    out.write(b"\n")                    # not glued to the new port's first one
                    midline = False
            try:
                data = os.read(fd, 4096)
                if data:
                    out.write(data)
                    out.flush()
                    midline = not data.endswith(b"\n")
                else:                                   # end of file: the device hung up
                    os.close(fd)
                    fd = -1
                    time.sleep(0.2)
                    continue
            except BlockingIOError:
                pass
            except OSError:
                os.close(fd)
                fd = -1
            time.sleep(0.02)


if __name__ == "__main__":
    main()

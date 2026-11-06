"""Offline serial contracts, using temporary paths and pseudo-terminals only."""
import contextlib
import errno
import importlib.util
import io
import os
from pathlib import Path
import select
import sys
import tempfile
import threading
import unittest
from unittest import mock

HOST = Path(__file__).resolve().parent
sys.path.insert(0, str(HOST))
import provision

spec = importlib.util.spec_from_file_location("serial_watch", HOST / "serial-watch.py")
serial_watch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serial_watch)


class WatcherPorts(unittest.TestCase):
    def test_existing_preferred_paths_are_kept(self):
        with tempfile.TemporaryDirectory() as tmp:
            for suffix in ("port", "by-id/board"):
                path = Path(tmp) / suffix
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
                with mock.patch.object(serial_watch.glob, "glob") as glob:
                    self.assertEqual(serial_watch.pick_port(str(path)), str(path))
                    glob.assert_not_called()

    def test_missing_stable_identity_never_falls_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(serial_watch.glob, "glob", return_value=["/dev/ttyACM0"]) as glob:
                self.assertIsNone(serial_watch.pick_port(str(Path(tmp) / "by-id/missing")))
                glob.assert_not_called()

    def test_missing_bare_path_follows_sorted_ports(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = str(Path(tmp) / "ttyACM1")
            for ports, expected in ((["/dev/ttyACM2", "/dev/ttyACM0"], "/dev/ttyACM0"), ([], None)):
                with self.subTest(ports=ports), mock.patch.object(serial_watch.glob, "glob", return_value=ports) as glob:
                    self.assertEqual(serial_watch.pick_port(path), expected)
                    glob.assert_called_once_with("/dev/ttyACM*")
            with mock.patch.object(serial_watch.glob, "glob", return_value=["/dev/ttyACM0"]) as glob:
                self.assertIsNone(provision.pick_port(path))
                glob.assert_not_called()
                self.assertEqual(serial_watch.pick_port(path), "/dev/ttyACM0")


class Replies(unittest.TestCase):
    def test_first_protocol_reply_only(self):
        # The reason is arbitrary payload, not a pinned UI error message.
        cases = (
            ("boot\n[Settings] saved\nrest", "[Settings] saved"),
            ("[Settings] rejected: synthetic reason\r\n", "[Settings] rejected: synthetic reason"),
            ("[Settings] saved \t\r\n", "[Settings] saved"),
            ("[Settings] loaded\n", None), ("", None), ("[Settings] sav", None),
            ("  [Settings] saved", None), ("x [Settings] saved", None),
            ("[Settings] saved\n[Settings] rejected: later", "[Settings] saved"),
            ("[Settings] rejected: first\n[Settings] saved", "[Settings] rejected: first"),
            (b"\xff[Settings] saved\n".decode(errors="replace"), None),
            ("é\n[Settings] saved", "[Settings] saved"),
        )
        for text, expected in cases:
            with self.subTest(text=text):
                self.assertEqual(provision.reply_in(text), expected)


class SendSettings(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = Path(self.tmp.name) / "watch.log"
        self.master, self.slave = os.openpty()
        self.addCleanup(os.close, self.master)
        self.addCleanup(os.close, self.slave)
        self.port = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        os.set_blocking(self.slave, False)
        self.timers = []
        self.writer_errors = []
        self.addCleanup(self.finish_writers)

    def finish_writers(self):
        for timer in self.timers:
            timer.cancel()
            timer.join()
        self.assertEqual(self.writer_errors, [])

    def later(self, action, delay=0.3):
        def guarded():
            try:
                action()
            except Exception as exc:
                self.writer_errors.append(exc)
        timer = threading.Timer(delay, guarded)
        self.timers.append(timer)
        timer.start()

    def send(self, settings, *, watcher=False, timeout=1.5, fails=False):
        stdout = io.StringIO()
        opened = []
        real_open = os.open

        def track_open(*args, **kwargs):
            fd = real_open(*args, **kwargs)
            opened.append(fd)
            return fd

        with mock.patch.object(provision, "watcher_running", return_value=watcher), \
             mock.patch.object(provision, "LOG", str(self.log)), \
             mock.patch.object(provision, "REPLY_S", timeout), \
             mock.patch.object(provision.glob, "glob") as glob, \
             mock.patch.object(provision.os, "open", side_effect=track_open), \
             contextlib.redirect_stdout(stdout):
            if fails:
                with self.assertRaises(RuntimeError) as caught:
                    provision.send_settings(settings, self.port)
                result = str(caught.exception)
                self.assertIn("no reply", result)
            else:
                result = provision.send_settings(settings, self.port)
            glob.assert_not_called()
        self.assertEqual(len(opened), 1)
        with self.assertRaises(OSError) as closed:
            os.fstat(opened[0])
        self.assertEqual(closed.exception.errno, errno.EBADF)
        self.assertIn(f"sent settings to {self.port}", stdout.getvalue())
        for value in settings.values():
            if value:
                self.assertNotIn(value, stdout.getvalue())
                if fails:
                    self.assertNotIn(value, result)
        return result

    def wire(self):
        chunks = []
        while select.select([self.master], [], [], 0)[0]:
            chunks.append(os.read(self.master, 4096))
        return b"".join(chunks)

    def test_port_reply_and_no_echo(self):
        self.later(lambda: os.write(self.master, b"junk\n[Settings] saved\n"))
        self.assertEqual(self.send({"hermes": "gw:8642", "key": "K1"}), "[Settings] saved")
        self.assertEqual(self.wire(), b'settings {"hermes":"gw:8642","key":"K1"}\n')
        try:
            remaining = os.read(self.slave, 4096)
        except BlockingIOError:
            remaining = b""
        self.assertNotIn(b"settings", remaining)

    def test_member_encoding(self):
        for settings, expected in (
            ({"pass": "", "key": None}, b'settings {"pass":""}\n'),
            ({"ssid": "Café"}, b'settings {"ssid":"Caf\\u00e9"}\n'),
            ({}, b'settings {}\n'),
            ({"key": "SECRET-KEY-123", "hermes": "gw:8642"},
             b'settings {"key":"SECRET-KEY-123","hermes":"gw:8642"}\n'),
        ):
            with self.subTest(settings=settings):
                self.later(lambda: os.write(self.master, b"[Settings] saved\n"))
                self.assertEqual(self.send(settings), "[Settings] saved")
                wire = self.wire()
                self.assertEqual(wire, expected)
                self.assertTrue(wire.isascii())

    def append_log(self, text):
        with self.log.open("a", encoding="utf-8") as stream:
            stream.write(text)

    def test_log_reply_ignores_old_lines(self):
        for old in ("[Settings] saved\n", "[Settings] rejected: old\n"):
            with self.subTest(old=old):
                self.log.write_text(old, encoding="utf-8")
                self.later(lambda: self.append_log("[Settings] rejected: synthetic reason\n"))
                self.assertEqual(self.send({}, watcher=True), "[Settings] rejected: synthetic reason")
                self.assertEqual(self.wire(), b"settings {}\n")

    def test_old_log_reply_does_not_prevent_timeout(self):
        self.log.write_text("[Settings] saved\n", encoding="utf-8")
        self.send({}, watcher=True, timeout=0.4, fails=True)
        self.assertEqual(self.wire(), b"settings {}\n")

    def test_missing_watcher_log_reads_port(self):
        self.later(lambda: os.write(self.master, b"[Settings] saved\n"))
        self.assertEqual(self.send({}, watcher=True), "[Settings] saved")

    def test_timeout_closes_fd_and_keeps_secret_private(self):
        self.send({"key": "SECRET-KEY-123"}, timeout=0.4, fails=True)
        self.assertEqual(self.wire(), b'settings {"key":"SECRET-KEY-123"}\n')
        # The owned PTY endpoints remain usable after the temporary descriptor closes.
        os.write(self.master, b"probe\n")
        self.assertTrue(select.select([self.slave], [], [], 1)[0])
        self.assertEqual(os.read(self.slave, 4096), b"probe\n")

    def test_split_reply(self):
        self.later(lambda: os.write(self.master, b"[Settings] sav"))
        self.later(lambda: os.write(self.master, b"ed\n"), delay=0.6)
        self.assertEqual(self.send({}), "[Settings] saved")

    def test_absent_port_errors(self):
        missing = str(Path(self.tmp.name) / "absent")
        with mock.patch.object(provision.glob, "glob") as glob:
            with self.assertRaises(RuntimeError) as caught:
                provision.send_settings({}, missing)
            self.assertIn(f"no device on {missing}", str(caught.exception))
            glob.assert_not_called()
        with mock.patch.object(provision, "pick_port", return_value=None):
            with self.assertRaises(RuntimeError) as caught:
                provision.send_settings({}, None)
            self.assertIn("no /dev/ttyACM* port", str(caught.exception))


if __name__ == "__main__":
    unittest.main()

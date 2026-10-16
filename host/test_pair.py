#!/usr/bin/env python3
"""Tests for the QR handoff in host/pair.py (run by firmware/test/host/run.sh).

Names, hosts and keys here are synthetic.
"""

import base64
import contextlib
import io
import json
import os
import stat
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pair  # noqa: E402

KEY = "k3y-for-tests-only-0123456789abcdef"


def decode(code):
    return json.loads(base64.urlsafe_b64decode(code + "=" * (-len(code) % 4)))


class Url(unittest.TestCase):
    def test_code_rides_in_the_fragment(self):
        url = pair.pairing_url("gw.example:8642", "alpha", KEY, "00ff")
        self.assertIn("/#pair=", url)
        self.assertNotIn("?", url)                 # nothing a request line could carry
        self.assertTrue(url.startswith("http://" + pair.SOFT_AP + "/#pair="))

    def test_code_members(self):
        url = pair.pairing_url("gw.example:8642", "alpha", KEY)
        m = decode(url.split("#pair=", 1)[1])
        self.assertEqual(m, {"hermes": "gw.example:8642", "profile": "alpha",
                             "shim": "", "key": KEY})
        m = decode(pair.pair_code("gw.example", "", KEY, "0a0b"))
        self.assertEqual(m["look"], "0a0b")
        self.assertEqual(m["profile"], "")


class QrHandoff(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = os.path.join(self.tmp.name, "home")
        os.makedirs(self.home)
        env = mock.patch.dict(os.environ, {"HOME": self.home})
        env.start()
        self.addCleanup(env.stop)

    def run_handoff(self, qrencode):
        out = io.StringIO()
        with mock.patch.object(pair.shutil, "which",
                               lambda n: "/usr/bin/qrencode" if qrencode else None), \
                mock.patch.object(pair.subprocess, "run") as run, \
                contextlib.redirect_stdout(out):
            pair.handoff_qr("gw.example:8642", "alpha", KEY)
        return out.getvalue(), run

    def test_qrencode_gets_the_url_on_stdin_only(self):
        text, run = self.run_handoff(True)
        run.assert_called_once()
        argv = run.call_args.args[0]
        self.assertEqual(argv[0], "qrencode")
        self.assertFalse(any("pair=" in a or KEY in a or "http" in a for a in argv))
        self.assertTrue(run.call_args.kwargs["input"].decode().startswith("http://"))
        sent = run.call_args.kwargs["input"].decode()
        self.assertEqual(decode(sent.split("#pair=", 1)[1])["key"], KEY)
        self.assertNotIn(KEY, text)
        self.assertNotIn("pair=", text)
        self.assertFalse(os.path.exists(os.path.join(self.home, ".config", "chirp")))

    def test_without_qrencode_only_a_private_path_is_printed(self):
        text, run = self.run_handoff(False)
        run.assert_not_called()
        path = os.path.join(self.home, ".config", "chirp", pair.PAIR_URL_FILE)
        self.assertIn(path, text)
        self.assertNotIn(KEY, text)
        self.assertNotIn("pair=", text)
        self.assertNotIn("http://", text)
        self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(os.stat(os.path.dirname(path)).st_mode), 0o700)
        with open(path) as f:
            body = f.read().strip()
        self.assertEqual(decode(body.split("#pair=", 1)[1])["key"], KEY)

    def test_a_looser_existing_file_is_tightened(self):
        d = os.path.join(self.home, ".config", "chirp")
        os.makedirs(d)
        path = os.path.join(d, pair.PAIR_URL_FILE)
        with open(path, "w") as f:
            f.write("old")
        os.chmod(path, 0o644)
        pair.write_pairing_url("http://x/#pair=y")
        self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
        with open(path) as f:
            self.assertEqual(f.read(), "http://x/#pair=y\n")


class Notes(unittest.TestCase):
    def test_lan_note_names_the_port_and_the_firewall(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            pair.lan_note()
        text = out.getvalue()
        self.assertIn("plain HTTP", text)
        self.assertIn(str(pair.HERMES_PORT), text)
        self.assertIn("firewall", text)


if __name__ == "__main__":
    unittest.main()

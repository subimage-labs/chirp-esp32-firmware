#!/usr/bin/env python3
"""Offline tests for host/audio_shim.py's request limits: body and chunk
caps, socket timeout, whole-request deadline, concurrency cap, TTS input cap,
and the firmware routes serving only the published files. Whisper and the TTS
worker are stubbed; the server listens on 127.0.0.1 on an ephemeral port.

    python3 host/test_audio_shim.py
"""

import http.client
import json
import os
import socket
import sys
import tempfile
import threading
import time
import unittest
from http.server import ThreadingHTTPServer
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_shim as shim  # noqa: E402


class _Seg:
    text = " hello there "


class _Info:
    language = "en"


class _FakeWhisper:
    def transcribe(self, _audio, **_kw):
        return [_Seg()], _Info()


STREAM_HEAD = (b"POST /v1/audio/transcriptions/stream HTTP/1.1\r\nHost: x\r\n"
               b"Content-Type: audio/wav\r\nTransfer-Encoding: chunked\r\n\r\n")


def chunk(data):
    return b"%x\r\n%s\r\n" % (len(data), data)


class ShimCase(unittest.TestCase):
    def setUp(self):
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), shim.Handler)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self._patches = [
            mock.patch.object(shim, "get_whisper", lambda: _FakeWhisper()),
            mock.patch.object(shim, "_worker_speech", lambda text: b"ID3" + text.encode()[:8]),
            mock.patch.object(shim, "_request_slots",
                              threading.BoundedSemaphore(shim.MAX_CONCURRENT)),
        ]
        for p in self._patches:
            p.start()
            self.addCleanup(p.stop)
        self.addCleanup(self._stop)

    def _stop(self):
        self.server.shutdown()
        self.server.server_close()

    def patch(self, name, value):
        p = mock.patch.object(shim, name, value)
        p.start()
        self.addCleanup(p.stop)

    def patch_handler(self, name, value):
        p = mock.patch.object(shim.Handler, name, value)
        p.start()
        self.addCleanup(p.stop)

    def sock(self, timeout=5):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=timeout)
        self.addCleanup(s.close)
        return s

    def response(self, sock):
        resp = http.client.HTTPResponse(sock)
        resp.begin()
        return resp.status, resp.read()

    def eof_within(self, sock, seconds):
        """True when the server closes the connection within `seconds`."""
        deadline = time.monotonic() + seconds
        sock.settimeout(0.2)
        while time.monotonic() < deadline:
            try:
                if sock.recv(4096) == b"":
                    return True
            except socket.timeout:
                continue
            except OSError:
                return True
        return False

    def wait_slots(self, free):
        """Waits for the handler thread to take/give back request slots (it
        releases after the response is on the wire)."""
        deadline = time.monotonic() + 3
        while shim._request_slots._value != free and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertEqual(shim._request_slots._value, free)

    def post(self, path, body, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        self.addCleanup(conn.close)
        conn.request("POST", path, body=body, headers=headers or {})
        r = conn.getresponse()
        return r.status, r.read()


class Stream(ShimCase):
    def test_normal_stream_transcribes(self):
        s = self.sock()
        s.sendall(STREAM_HEAD + chunk(b"R" * 44) + chunk(b"\0" * 3200) + b"0\r\n\r\n")
        status, body = self.response(s)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["text"], "hello there")

    def test_recording_cap(self):
        self.patch("MAX_AUDIO_BYTES", 100_000)
        s = self.sock()
        s.sendall(STREAM_HEAD + chunk(b"\0" * 40_000) + chunk(b"\0" * 40_000) +
                  chunk(b"\0" * 40_000))
        status, body = self.response(s)
        self.assertEqual(status, 413)
        self.assertIn("too long", json.loads(body)["error"])
        self.assertTrue(self.eof_within(s, 3))

    def test_exactly_the_cap_is_accepted(self):
        self.patch("MAX_AUDIO_BYTES", 80_000)
        s = self.sock()
        s.sendall(STREAM_HEAD + chunk(b"\0" * 40_000) + chunk(b"\0" * 40_000) + b"0\r\n\r\n")
        self.assertEqual(self.response(s)[0], 200)

    def test_chunk_cap(self):
        s = self.sock()
        s.sendall(STREAM_HEAD + b"%x\r\n" % (shim.MAX_CHUNK_BYTES + 1))   # header only
        status, body = self.response(s)
        self.assertEqual(status, 413)
        self.assertIn("chunk", json.loads(body)["error"])
        s = self.sock()
        s.sendall(STREAM_HEAD + chunk(b"\0" * shim.MAX_CHUNK_BYTES) + b"0\r\n\r\n")
        self.assertEqual(self.response(s)[0], 200)

    def test_bad_chunk_sizes(self):
        for line in (b"zz", b"-5", b"0x10", b"1_0", b"fffffffff", b"+4"):
            s = self.sock()
            s.sendall(STREAM_HEAD + line + b"\r\n")
            self.assertEqual(self.response(s)[0], 400, line)

    def test_must_be_chunked(self):
        status, _ = self.post("/v1/audio/transcriptions/stream", b"abc",
                              {"Content-Type": "audio/wav"})
        self.assertEqual(status, 400)

    def test_silent_client_is_dropped_by_the_socket_timeout(self):
        self.patch_handler("timeout", 0.3)
        s = self.sock()
        s.sendall(STREAM_HEAD + chunk(b"\0" * 100))     # then nothing
        self.assertTrue(self.eof_within(s, 3))

    def test_trickle_is_cut_by_the_request_deadline(self):
        self.patch("REQUEST_DEADLINE_S", 0.5)
        s = self.sock()
        s.sendall(STREAM_HEAD)

        def drip():
            try:
                for _ in range(100):
                    s.sendall(b"\r\n")                  # blank lines: no read ever stalls
                    time.sleep(0.05)
            except OSError:
                pass
        threading.Thread(target=drip, daemon=True).start()
        self.assertTrue(self.eof_within(s, 4))


class Concurrency(ShimCase):
    def test_busy_server_answers_503_then_recovers(self):
        self.patch("_request_slots", threading.BoundedSemaphore(1))
        a = self.sock()
        a.sendall(STREAM_HEAD + chunk(b"\0" * 100))     # holds the only slot
        self.wait_slots(0)

        status, body = self.post("/v1/audio/speech", json.dumps({"input": "hi"}),
                                 {"Content-Type": "application/json"})
        self.assertEqual(status, 503)
        self.assertEqual(json.loads(body)["error"], "busy")

        a.sendall(b"0\r\n\r\n")
        self.assertEqual(self.response(a)[0], 200)
        self.wait_slots(1)
        status, _ = self.post("/v1/audio/speech", json.dumps({"input": "hi"}),
                              {"Content-Type": "application/json"})
        self.assertEqual(status, 200)

    def test_slot_released_after_a_rejected_request(self):
        self.patch("_request_slots", threading.BoundedSemaphore(1))
        for _ in range(3):
            status, _ = self.post("/v1/audio/speech", "not json",
                                  {"Content-Type": "application/json"})
            self.assertEqual(status, 400)
        self.wait_slots(1)


class Speech(ShimCase):
    def speak(self, payload):
        body = payload if isinstance(payload, str) else json.dumps(payload)
        return self.post("/v1/audio/speech", body, {"Content-Type": "application/json"})

    def test_text_cap(self):
        status, body = self.speak({"input": "a" * shim.MAX_SPEECH_CHARS})
        self.assertEqual((status, body[:3]), (200, b"ID3"))
        status, body = self.speak({"input": "a" * (shim.MAX_SPEECH_CHARS + 1)})
        self.assertEqual(status, 413)
        self.assertIn("too long", json.loads(body)["error"])

    def test_body_cap_without_reading_it(self):
        s = self.sock()
        s.sendall(b"POST /v1/audio/speech HTTP/1.1\r\nHost: x\r\n"
                  b"Content-Length: %d\r\n\r\n" % (shim.MAX_SPEECH_BODY + 1))
        self.assertEqual(self.response(s)[0], 413)

    def test_malformed_requests(self):
        self.assertEqual(self.speak("not json")[0], 400)
        self.assertEqual(self.speak("[1,2]")[0], 400)
        self.assertEqual(self.speak({"input": 5})[0], 400)
        self.assertEqual(self.speak({"input": "   "})[0], 400)
        self.assertEqual(self.speak({})[0], 400)
        self.assertEqual(self.post("/v1/audio/speech", b"\xff\xfe")[0], 400)
        s = self.sock()
        s.sendall(b"POST /v1/audio/speech HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n")
        self.assertEqual(self.response(s)[0], 400)


class Transcriptions(ShimCase):
    def test_multipart_upload_within_the_cap(self):
        boundary = "B"
        body = (b"--B\r\nContent-Disposition: form-data; name=\"file\"; "
                b"filename=\"clip.wav\"\r\nContent-Type: audio/wav\r\n\r\n" +
                b"R" * 1000 + b"\r\n--B--\r\n")
        status, out = self.post(
            "/v1/audio/transcriptions", body,
            {"Content-Type": "multipart/form-data; boundary=" + boundary})
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(out)["text"], "hello there")

    def test_body_over_the_cap_is_refused_unread(self):
        s = self.sock()
        s.sendall(b"POST /v1/audio/transcriptions HTTP/1.1\r\nHost: x\r\n"
                  b"Content-Type: multipart/form-data; boundary=B\r\n"
                  b"Content-Length: %d\r\n\r\n" % (50 * 1024 * 1024))
        self.assertEqual(self.response(s)[0], 413)

    def test_stalled_body_times_out(self):
        self.patch_handler("timeout", 0.3)
        s = self.sock()
        s.sendall(b"POST /v1/audio/transcriptions HTTP/1.1\r\nHost: x\r\n"
                  b"Content-Type: multipart/form-data; boundary=B\r\n"
                  b"Content-Length: 5000\r\n\r\nabc")
        self.assertTrue(self.eof_within(s, 3))

    def test_unknown_post_path(self):
        self.assertEqual(self.post("/v1/audio/other", b"x")[0], 404)


class FirmwareRoutes(ShimCase):
    def setUp(self):
        super().setUp()
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        for name, data in (("manifest.json", b"{}\n"), ("chirp.bin", b"BIN"),
                           ("secret.txt", b"nope")):
            with open(os.path.join(tmp.name, name), "wb") as fh:
                fh.write(data)
        self.patch("FIRMWARE_DIR", tmp.name)

    def get(self, path):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        self.addCleanup(conn.close)
        conn.request("GET", path)
        r = conn.getresponse()
        return r.status, r.read()

    def test_only_the_published_files_are_served(self):
        self.assertEqual(self.get("/firmware/manifest.json"), (200, b"{}\n"))
        self.assertEqual(self.get("/firmware/chirp.bin"), (200, b"BIN"))
        for path in ("/firmware/secret.txt", "/firmware/../secret.txt",
                     "/firmware/chirp.bin?x=1", "/firmware/", "/firmware",
                     "/firmware/%2e%2e/secret.txt", "/firmware//chirp.bin"):
            self.assertEqual(self.get(path)[0], 404, path)

    def test_missing_publication_is_404(self):
        self.patch("FIRMWARE_DIR", os.path.join(tempfile.gettempdir(), "no-such-chirp-dir"))
        self.assertEqual(self.get("/firmware/manifest.json")[0], 404)


if __name__ == "__main__":
    unittest.main(verbosity=2)

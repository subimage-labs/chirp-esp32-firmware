#!/usr/bin/env python3
"""Offline configuration contracts; run with python -m unittest host/test_audio_shim_config.py.

Each startup imports a fresh shim under an isolated environment. Only an
ephemeral loopback server is used; Whisper and the TTS worker are stubbed.
"""

import contextlib
import http.client
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from types import SimpleNamespace
from unittest import mock


TOKEN = "synthetic-shim-config-token"
WRONG_TOKEN = "synthetic-wrong-token"
MULTIPART = (b'--B\r\nContent-Disposition: form-data; name="file"; '
             b'filename="clip.wav"\r\nContent-Type: audio/wav\r\n\r\n'
             b'RIFF-test-audio\r\n--B--\r\n')


class _FakeWhisper:
    def __init__(self):
        self.calls = []
        self.decoded = threading.Event()

    def transcribe(self, audio, **kwargs):
        self.calls.append(kwargs)
        self.decoded.set()
        return [SimpleNamespace(text=" hello there ")], SimpleNamespace(language="en")


class ShimConfig(unittest.TestCase):
    @contextlib.contextmanager
    def startup(self, language=None, token=None):
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.dict(os.environ))
            for name, value in (("SHIM_STT_LANGUAGE", language), ("SHIM_TOKEN", token)):
                os.environ.pop(name, None)
                if value is not None:
                    os.environ[name] = value
            os.environ["SHIM_PORT"] = "0"
            directory = stack.enter_context(tempfile.TemporaryDirectory())
            os.environ["SHIM_FIRMWARE_DIR"] = directory
            Path(directory, "manifest.json").write_bytes(b"{}\n")
            Path(directory, "chirp.bin").write_bytes(b"BIN")
            self.logs = io.StringIO()
            stack.enter_context(contextlib.redirect_stderr(self.logs))
            stack.enter_context(contextlib.redirect_stdout(self.logs))
            spec = importlib.util.spec_from_file_location(
                "audio_shim_config_test", Path(__file__).with_name("audio_shim.py"))
            self.shim = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.shim)
            self.whisper = _FakeWhisper()
            self.get_whisper = stack.enter_context(mock.patch.object(
                self.shim, "get_whisper", return_value=self.whisper))
            self.tts = stack.enter_context(mock.patch.object(
                self.shim, "_worker_speech", return_value=b"ID3-test"))
            # Partial decode passes immutable bytes to the WAV patcher today.
            # Header repair is unrelated to language selection; bypass it so
            # both decode sites actually reach our model stub.
            stack.enter_context(mock.patch.object(self.shim, "_patch_wav"))
            server = ThreadingHTTPServer(("127.0.0.1", 0), self.shim.Handler)
            self.port = server.server_address[1]
            self.assertNotEqual(self.port, 8643)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                yield
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=5)

    def request(self, method, path, body=None, headers=None):
        with contextlib.closing(http.client.HTTPConnection(
                "127.0.0.1", self.port, timeout=5)) as conn:
            conn.request(method, path, body=body, headers=headers or {})
            response = conn.getresponse()
            return response.status, response.getheader("Content-Type", ""), response.read()

    def audio(self, path, authorization=None):
        headers = {}
        if authorization is not None:
            headers["Authorization"] = authorization
        if path.endswith("/speech"):
            body = b'{"input":"hello"}'
            headers["Content-Type"] = "application/json"
        elif path.endswith("/stream"):
            body = b"4\r\nRIFF\r\n0\r\n\r\n"
            headers["Transfer-Encoding"] = "chunked"
            headers["Content-Type"] = "audio/wav"
        else:
            body = MULTIPART
            headers["Content-Type"] = "multipart/form-data; boundary=B"
        return self.request("POST", path, body, headers)

    def assert_private(self, body):
        for secret in (TOKEN, WRONG_TOKEN):
            self.assertNotIn(secret.encode(), body)
            self.assertNotIn(secret, self.logs.getvalue())

    def test_language_one_shot(self):
        for setting, expected in ((None, "en"), ("auto", None), ("es", "es")):
            with self.subTest(setting=setting), self.startup(language=setting):
                # Configuration is captured at startup, not on each request.
                os.environ["SHIM_STT_LANGUAGE"] = "fr"
                status, _, body = self.audio("/v1/audio/transcriptions")
                self.assertEqual(status, 200, body)
                self.assertEqual(len(self.whisper.calls), 1)
                self.assertEqual(self.whisper.calls[0]["language"], expected)

    def test_language_stream_finalize(self):
        for setting, expected in ((None, "en"), ("auto", None), ("de", "de")):
            with self.subTest(setting=setting), self.startup(language=setting):
                os.environ["SHIM_STT_LANGUAGE"] = "fr"
                status, _, body = self.audio("/v1/audio/transcriptions/stream")
                self.assertEqual(status, 200, body)
                self.assertEqual(len(self.whisper.calls), 1)
                self.assertEqual(self.whisper.calls[0]["language"], expected)

    def test_language_stream_partial(self):
        for setting, expected in ((None, "en"), ("auto", None), ("ja", "ja")):
            with self.subTest(setting=setting), self.startup(language=setting):
                os.environ["SHIM_STT_LANGUAGE"] = "fr"
                with contextlib.closing(http.client.HTTPConnection(
                        "127.0.0.1", self.port, timeout=5)) as conn:
                    conn.putrequest("POST", "/v1/audio/transcriptions/stream")
                    conn.putheader("Transfer-Encoding", "chunked")
                    conn.putheader("Content-Type", "audio/wav")
                    conn.endheaders()
                    conn.send(b"fa00\r\n" + b"R" * 64000 + b"\r\n")
                    partial_done = self.whisper.decoded.wait(timeout=5)
                    partial_calls = list(self.whisper.calls)
                    # Always finish the request, including on a failed wait.
                    conn.send(b"0\r\n\r\n")
                    response = conn.getresponse()
                    body = response.read()
                self.assertTrue(partial_done, "partial decode never reached Whisper")
                self.assertEqual(response.status, 200, body)
                self.assertEqual(len(partial_calls), 1)
                self.assertEqual(len(self.whisper.calls), 2)
                self.assertEqual(partial_calls[0]["language"], expected)

    def test_token_rejects_audio_before_work(self):
        for path in ("/v1/audio/transcriptions", "/v1/audio/transcriptions/stream",
                     "/v1/audio/speech"):
            for auth in (None, "Bearer " + WRONG_TOKEN, "Basic " + TOKEN, TOKEN):
                with self.subTest(path=path, auth=auth), self.startup(token=TOKEN):
                    status, ctype, body = self.audio(path, auth)
                    self.assert_private(body)
                    # Separate subtests expose both denial and no-work failures.
                    with self.subTest(contract="status"):
                        self.assertEqual(status, 401, body)
                        self.assertIn("application/json", ctype)
                        self.assertIsInstance(json.loads(body), dict)
                    with self.subTest(contract="no work"):
                        self.assertEqual((self.get_whisper.call_count, self.tts.call_count),
                                         (0, 0))

    def test_token_protects_firmware_and_unknown_prefixed_paths(self):
        routes = [("GET", "/firmware/manifest.json"), ("GET", "/firmware/chirp.bin")]
        routes += [(method, path) for method in ("GET", "POST")
                   for path in ("/v1/", "/v1/unknown", "/firmware/", "/firmware/unknown")]
        for method, path in routes:
            for auth in (None, "Bearer " + WRONG_TOKEN):
                with self.subTest(method=method, path=path, auth=auth), self.startup(token=TOKEN):
                    headers = {} if auth is None else {"Authorization": auth}
                    status, ctype, body = self.request(method, path, headers=headers)
                    self.assert_private(body)
                    self.get_whisper.assert_not_called()
                    self.tts.assert_not_called()
                    self.assertEqual(status, 401, body)
                    self.assertIn("application/json", ctype)
                    self.assertIsInstance(json.loads(body), dict)

    def test_valid_token_allows_audio_and_firmware_without_leaking(self):
        with self.startup(token=TOKEN):
            for path in ("/v1/audio/transcriptions", "/v1/audio/transcriptions/stream",
                         "/v1/audio/speech"):
                status, _, body = self.audio(path, "Bearer " + TOKEN)
                self.assertEqual(status, 200, body)
                self.assert_private(body)
            self.assertEqual(self.get_whisper.call_count, 2)
            self.tts.assert_called_once_with("hello")
            for path, expected in (("/firmware/manifest.json", b"{}\n"),
                                   ("/firmware/chirp.bin", b"BIN")):
                status, _, body = self.request("GET", path, headers={
                    "Authorization": "Bearer " + TOKEN})
                self.assertEqual((status, body), (200, expected))
                self.assert_private(body)

    def test_unset_or_empty_token_preserves_open_access(self):
        for token in (None, ""):
            with self.subTest(token=token), self.startup(token=token):
                for path in ("/v1/audio/transcriptions", "/v1/audio/transcriptions/stream",
                             "/v1/audio/speech"):
                    self.assertEqual(self.audio(path)[0], 200)
                for path in ("/firmware/manifest.json", "/firmware/chirp.bin"):
                    self.assertEqual(self.request("GET", path)[0], 200)
                self.assertEqual(self.get_whisper.call_count, 2)
                self.tts.assert_called_once_with("hello")

    def test_health_stays_open_without_leaking_token(self):
        with self.startup(token=TOKEN):
            status, ctype, body = self.request("GET", "/health")
            self.assertEqual(status, 200)
            self.assertIn("application/json", ctype)
            self.assertEqual(json.loads(body)["status"], "ok")
            self.assert_private(body)
            self.get_whisper.assert_not_called()
            self.tts.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)

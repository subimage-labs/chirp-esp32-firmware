#!/usr/bin/env python3
"""OpenAI-compatible audio shim for the ESP32 voice device.

Endpoints:
  POST /v1/audio/transcriptions         (multipart: file=<wav>) -> {"text": ...}
  POST /v1/audio/transcriptions/stream  (chunked raw wav while recording)
                                        -> {"text": ..., "partial": ...}
  POST /v1/audio/speech            (json: {input}) -> mp3 bytes (single voice)
  GET  /health                          -> {"status": "ok"}
  GET  /firmware/manifest.json          -> {"build","version","size","sha256",
                                        "sig"} the device's boot-time OTA
                                        check reads (signed; see
                                        host/publish-firmware.py)
  GET  /firmware/chirp.bin              -> app image (both 404 until
                                        host/publish-firmware.py has run)

STT: faster-whisper, model "base" (SHIM_WHISPER_MODEL), int8 on CPU. Speech
language SHIM_STT_LANGUAGE: a whisper language code (default "en"), or "auto"
to let whisper detect it.
TTS: resident Paradee worker (host/local_tts.py --serve) on 127.0.0.1:8644,
spawned on demand — 8M distillation of Kokoro-82M, single voice af_heart.
Runs from host/.venv-tts (see host/setup.sh). No network, no API keys.

Access: with SHIM_TOKEN set, every /v1/ and /firmware/ request needs
"Authorization: Bearer <SHIM_TOKEN>" (else 401). /health stays open. Unset,
anyone who reaches the port can use it.
"""

import contextlib
import hmac
import io
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from email.parser import BytesParser
from email.policy import default as default_policy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST = "0.0.0.0"
PORT = int(os.environ.get("SHIM_PORT", "8643"))
WHISPER_MODEL = os.environ.get("SHIM_WHISPER_MODEL", "base")
_lang = os.environ.get("SHIM_STT_LANGUAGE", "").strip() or "en"
STT_LANGUAGE = None if _lang.lower() == "auto" else _lang   # None = whisper detects
SHIM_TOKEN = os.environ.get("SHIM_TOKEN", "")
PROTECTED_PREFIXES = ("/v1/", "/firmware/")

# Device firmware for boot-time OTA, published by host/publish-firmware.py.
# Same directory via its --dir/SHIM_FIRMWARE_DIR default.
FIRMWARE_DIR = os.environ.get(
    "SHIM_FIRMWARE_DIR",
    os.path.join(os.path.expanduser("~"), ".local", "share", "chirp",
                 "firmware"))

# TTS worker runs in this same interpreter/venv, from this directory.
_HERE = os.path.dirname(os.path.abspath(__file__))
LOCAL_TTS = os.path.join(_HERE, "local_tts.py")
TTS_WORKER_PORT = int(os.environ.get("TTS_WORKER_PORT", "8644"))
TTS_TIMEOUT = 100

# Request limits. The shim listens on the LAN and runs whisper and TTS for
# whoever connects, so every request is bounded in bytes, time and number.
MAX_RECORD_SECS = 30                 # firmware REC_MAX_SECS
MAX_AUDIO_BYTES = MAX_RECORD_SECS * 32000 + 64 * 1024   # 16 kHz s16 mono + WAV header + slack
MAX_CHUNK_BYTES = 64 * 1024          # one chunk of the streamed body
MAX_SPEECH_CHARS = 4096              # /v1/audio/speech input text
MAX_SPEECH_BODY = 32 * 1024          # its JSON body (escapes inflate the text)
SOCKET_TIMEOUT_S = 30                # one socket read or write
REQUEST_DEADLINE_S = MAX_RECORD_SECS + 30   # a whole request body, however it trickles in
MAX_CONCURRENT = 4                   # POST requests served at once; the rest get 503
CHUNK_SIZE_RE = re.compile(rb"[0-9A-Fa-f]{1,8}")
_request_slots = threading.BoundedSemaphore(MAX_CONCURRENT)

_tts_worker_proc = None


_tts_worker_lock = threading.Lock()


def _spawn_tts_worker():
    """Start the resident Paradee worker if it isn't running.

    It lives in the shim's systemd cgroup, so it dies with every shim restart;
    main() respawns it at startup so the model is warm before the first turn."""
    global _tts_worker_proc
    with _tts_worker_lock:
        if _tts_worker_proc and _tts_worker_proc.poll() is None:
            return
        _tts_worker_proc = subprocess.Popen(
            [sys.executable, LOCAL_TTS, "--serve", "--threads", "2",
             "--port", str(TTS_WORKER_PORT)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _worker_ready(wait_s):
    """Poll the worker's GET until it answers or wait_s elapses."""
    deadline = time.time() + wait_s
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(
                    f"http://127.0.0.1:{TTS_WORKER_PORT}/", timeout=1) as r:
                if r.status == 200:
                    return True
        except Exception:
            time.sleep(0.25)
    return False


def _worker_speech(text):
    """Synthesize via the resident worker; (re)spawn and wait if it is down.

    Returns MP3 bytes, or None if the worker never came up."""
    body = json.dumps({"text": text}).encode()
    for attempt in range(2):
        try:
            req = urllib.request.Request(
                f"http://127.0.0.1:{TTS_WORKER_PORT}/", data=body,
                headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=TTS_TIMEOUT) as resp:
                return resp.read()
        except Exception:
            if attempt == 0:
                _spawn_tts_worker()
                if not _worker_ready(20):                 # model load ~3-5s
                    return None
    return None

_whisper_model = None
_whisper_lock = threading.Lock()


def get_whisper():
    global _whisper_model
    if _whisper_model is None:
        from faster_whisper import WhisperModel
        _whisper_model = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")
    return _whisper_model


def parse_multipart(content_type, body):
    """Return {field: (filename, bytes)} from a multipart/form-data body."""
    header = f"Content-Type: {content_type}".encode()
    msg = BytesParser(policy=default_policy).parsebytes(
        header + b"\r\nMIME-Version: 1.0\r\n\r\n" + body
    )
    fields = {}
    for part in msg.iter_parts():
        name = part.get_param("name", header="content-disposition")
        if name is None:
            continue
        filename = part.get_filename()
        payload = part.get_payload(decode=True)
        fields[name] = (filename, payload)
    return fields


def _patch_wav(ba):
    """Rewrite RIFF sizes for a streamed WAV whose header predates the data."""
    if len(ba) <= 44:
        return
    data_len = len(ba) - 44
    ba[4:8] = struct.pack("<I", data_len + 36)
    ba[40:44] = struct.pack("<I", data_len)


class _Reject(Exception):
    """A request refused before its body was fully read: the connection is
    dropped after the answer."""

    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "HermesAudioShim/1.0"
    timeout = SOCKET_TIMEOUT_S          # any single socket read or write

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    def _send_json(self, code, obj):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        if self.close_connection:
            self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def _reject(self, code, message):
        """Answer and close: the request body was not (fully) read."""
        self.close_connection = True
        self._send_json(code, {"error": message})

    @contextlib.contextmanager
    def _body_deadline(self):
        """Cuts the connection if the body is still arriving after
        REQUEST_DEADLINE_S; a trickle of bytes never trips the per-read
        socket timeout."""
        timer = threading.Timer(REQUEST_DEADLINE_S, self._abort_connection)
        timer.daemon = True
        timer.start()
        try:
            yield
        finally:
            timer.cancel()

    def _abort_connection(self):
        try:
            self.connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

    def _read_body(self, limit):
        """The request body (Content-Length 1..limit bytes) or a _Reject."""
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            raise _Reject(400, "bad Content-Length")
        if length <= 0:
            raise _Reject(400, "bad body")
        if length > limit:
            raise _Reject(413, "body too large")
        parts = []
        remaining = length
        with self._body_deadline():
            while remaining > 0:
                data = self.rfile.read1(min(remaining, MAX_CHUNK_BYTES))
                if not data:
                    raise _Reject(400, "truncated body")
                parts.append(data)
                remaining -= len(data)
        return b"".join(parts)

    def _authorized(self):
        """True unless SHIM_TOKEN is set and this protected path lacks it."""
        if not SHIM_TOKEN or not self.path.startswith(PROTECTED_PREFIXES):
            return True
        scheme, _, given = self.headers.get("Authorization", "").partition(" ")
        return scheme.lower() == "bearer" and hmac.compare_digest(
            given.strip().encode(), SHIM_TOKEN.encode())

    def do_GET(self):
        if not self._authorized():
            self._send_json(401, {"error": "unauthorized"})
            return
        if self.path == "/health":
            self._send_json(200, {"status": "ok", "stt": "faster-whisper/" + WHISPER_MODEL,
                                  "tts": "paradee-local"})
        elif self.path == "/firmware/manifest.json":
            self._send_firmware("manifest.json", "application/json")
        elif self.path == "/firmware/chirp.bin":
            self._send_firmware("chirp.bin", "application/octet-stream")
        else:
            self._send_json(404, {"error": "not found"})

    def _send_firmware(self, name, ctype):
        """One published firmware file; 404 until publish-firmware.py ran."""
        path = os.path.join(FIRMWARE_DIR, name)
        if not os.path.isfile(path):
            self._send_json(404, {"error": "not found"})
            return
        try:
            with open(path, "rb") as fh:
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length",
                                 str(os.fstat(fh.fileno()).st_size))
                self.end_headers()
                shutil.copyfileobj(fh, self.wfile, 64 * 1024)
        except (BrokenPipeError, ConnectionResetError):
            pass                                   # device rebooted mid-download

    def do_POST(self):
        if not self._authorized():
            self._reject(401, "unauthorized")         # body left unread
            return
        route = {
            "/v1/audio/transcriptions": self._transcribe,
            "/v1/audio/transcriptions/stream": self._stream_transcribe,
            "/v1/audio/speech": self._speech,
        }.get(self.path)
        if route is None:
            self._reject(404, "not found")
            return
        if not _request_slots.acquire(blocking=False):
            self._reject(503, "busy")
            return
        try:
            route()
        except _Reject as rej:
            self._reject(rej.code, rej.message)
        finally:
            _request_slots.release()

    def _stream_transcribe(self):
        """Chunked WAV upload: whisper re-decodes the growing buffer in the
        background while audio arrives; final text returns when the body ends.
        The body is capped (MAX_AUDIO_BYTES, MAX_CHUNK_BYTES per chunk) and
        must arrive within REQUEST_DEADLINE_S."""
        try:
            if self.headers.get("Transfer-Encoding", "").lower() != "chunked":
                raise _Reject(400, "expected chunked body")
            buf = bytearray()
            st = {"decoded": 0, "busy": False, "partial": ""}

            def decode_worker():
                snap = bytes(buf)
                st["decoded"] = len(buf)
                try:
                    _patch_wav(snap)
                    with io.BytesIO(bytes(snap)) as wav_io:
                        with _whisper_lock:
                            segments, _info = get_whisper().transcribe(
                                wav_io, beam_size=1, vad_filter=True,
                                language=STT_LANGUAGE)
                            text = "".join(s.text for s in segments).strip()
                    st["partial"] = text
                except Exception:
                    pass
                finally:
                    st["busy"] = False

            with self._body_deadline():
                while True:
                    size_line = self.rfile.readline(64)
                    if not size_line:
                        raise ConnectionError("client disconnected")
                    line = size_line.strip()
                    if not line:
                        continue
                    digits = line.split(b";")[0].strip()
                    if not CHUNK_SIZE_RE.fullmatch(digits):
                        raise _Reject(400, "bad chunk size")
                    size = int(digits, 16)
                    if size == 0:
                        self.rfile.readline(64)               # final CRLF
                        break
                    if size > MAX_CHUNK_BYTES:
                        raise _Reject(413, "chunk too large")
                    if len(buf) + size > MAX_AUDIO_BYTES:
                        raise _Reject(413, "recording too long")
                    remaining = size
                    while remaining > 0:
                        data = self.rfile.read1(min(remaining, MAX_CHUNK_BYTES))
                        if not data:
                            raise ConnectionError("truncated chunk")
                        buf.extend(data)
                        remaining -= len(data)
                    self.rfile.read(2)                        # chunk CRLF
                    if not st["busy"] and len(buf) - st["decoded"] >= 64000:
                        st["busy"] = True
                        threading.Thread(target=decode_worker,
                                         daemon=True).start()

            _patch_wav(buf)
            with io.BytesIO(bytes(buf)) as wav_io:
                with _whisper_lock:
                    segments, _info = get_whisper().transcribe(
                        wav_io, beam_size=1, vad_filter=True, language=STT_LANGUAGE)
                    text = "".join(s.text for s in segments).strip()
            self._send_json(200, {"text": text, "partial": st["partial"]})
        except _Reject:
            raise
        except Exception as exc:
            self.close_connection = True              # the body may be half read
            try:
                self._send_json(500, {"error": str(exc)})
            except Exception:
                pass

    def _transcribe(self):
        body = self._read_body(MAX_AUDIO_BYTES)
        ctype = self.headers.get("Content-Type", "")
        if "multipart/form-data" not in ctype:
            self._send_json(400, {"error": "expected multipart/form-data"})
            return
        try:
            fields = parse_multipart(ctype, body)
        except Exception as exc:
            self._send_json(400, {"error": f"multipart parse failed: {exc}"})
            return
        entry = fields.get("file")
        if not entry or not entry[1]:
            self._send_json(400, {"error": "missing file field"})
            return
        _, audio = entry
        try: sys.stderr.write(f"[stt] rx {len(audio)} bytes\n")
        except Exception: pass
        suffix = ".wav"
        fd, path = tempfile.mkstemp(suffix=suffix)
        try:
            with os.fdopen(fd, "wb") as fh:
                fh.write(audio)
            with _whisper_lock:
                segments, info = get_whisper().transcribe(path, beam_size=1, vad_filter=True,
                                                          language=STT_LANGUAGE)
                text = "".join(seg.text for seg in segments).strip()
            self._send_json(200, {"text": text, "language": info.language})
        except Exception as exc:
            self._send_json(500, {"error": str(exc)})
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def _speech(self):
        body = self._read_body(MAX_SPEECH_BODY)
        try:
            req = json.loads(body)
        except ValueError:                         # bad JSON or bad UTF-8
            self._send_json(400, {"error": "invalid json"})
            return
        text = req.get("input") if isinstance(req, dict) else None
        text = text.strip() if isinstance(text, str) else ""
        if not text:
            self._send_json(400, {"error": "missing input"})
            return
        if len(text) > MAX_SPEECH_CHARS:
            raise _Reject(413, "input too long")
        try:
            mp3 = _worker_speech(text)
            if mp3 is None:
                self._send_json(502, {"error": "tts worker unavailable"})
                return
            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            self.send_header("Content-Length", str(len(mp3)))
            self.end_headers()
            self.wfile.write(mp3)
        except Exception as exc:
            self._send_json(500, {"error": str(exc)})


def main():
    _spawn_tts_worker()                       # warm before the first device turn
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"hermes-audio-shim listening on {HOST}:{PORT} (stt={WHISPER_MODEL})", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

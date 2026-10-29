#!/usr/bin/env python3
"""OpenAI-shaped mock of the venice backend for bench builds — no real key.

Serves exactly the four calls a venice-mode device makes:

  POST /api/v1/chat/completions            -> SSE deltas of a fixed answer + [DONE]
  POST /api/v1/audio/transcriptions        -> {"text": ...}   (multipart: model + wav)
  POST /api/v1/audio/speech                -> mp3 bytes       (json {input, model, voice})
  GET  /api/v1/models                      -> {"data": []}    (the boot self-test probe)

Every request's shape is logged so a bench run shows exactly what the
firmware sent (roles and message count, the STT model field, the TTS model
and voice). The spoken audio reuses the firmware's own embedded chirp,
parsed out of chirp_mp3.h, so the on-device MP3 decoder plays something
real. Requests without a Bearer key are answered 401 — a provisioning
mistake must show up as "Bad API key" on the screen, not as silence.

Not a Venice emulator: one answer, no errors, no streaming STT.

    host/venice_mock.py [--port 8699] [--answer TEXT] [--verbose]
"""

import argparse
import io
import json
import os
import re
import sys
from email.parser import BytesParser
from email.policy import default as default_policy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ANSWER = ("Four. The mock backend says two plus two is four. "
                  "Ask me another one!")

ANSWER = DEFAULT_ANSWER
MP3 = b""
VERBOSE = False


def log(msg):
    sys.stderr.write(f"[venmock] {msg}\n")
    sys.stderr.flush()


def load_chirp(path):
    """The embedded chirp mp3 as bytes: real audio the decoder can play."""
    txt = open(path).read()
    m = re.search(r"CHIRP_MP3\[\] PROGMEM = \{(.*?)\};", txt, re.S)
    if not m:
        raise SystemExit(f"no CHIRP_MP3 array in {path}")
    return bytes(int(x) for x in m.group(1).replace("\n", " ").split(",") if x.strip())


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass                                   # the per-endpoint logs below say more

    def _send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code, obj):
        self._send(code, "application/json", json.dumps(obj).encode())

    def _authed(self):
        auth = self.headers.get("Authorization", "")
        if auth.startswith("Bearer ") and len(auth) > len("Bearer xx"):
            if VERBOSE:
                log(f"{self.path} auth key len {len(auth) - 7}")
            return True
        log(f"{self.path} REJECTED: Authorization '{auth[:12]}...'")
        self._json(401, {"error": {"message": "Missing or bad API key",
                                   "type": "invalid_request_error"}})
        return False

    def do_GET(self):
        if self.path == "/api/v1/models":
            if self._authed():
                self._json(200, {"data": []})
        else:
            self._json(404, {"error": "not found"})

    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if length < 0 or length > 2 * 1024 * 1024:
            self._json(413, {"error": "bad size"})
            return
        body = self.rfile.read(length)

        if self.path == "/api/v1/chat/completions":
            if not self._authed():
                return
            req = json.loads(body)
            roles = [m.get("role") for m in req.get("messages", [])]
            log(f"chat model={req.get('model')} messages={len(roles)} "
                f"roles={roles[:4]}{'...' if len(roles) > 4 else ''} "
                f"stream={req.get('stream')} "
                f"venice_parameters={'venice_parameters' in req}")
            events = []
            for i in range(0, len(ANSWER), 12):        # a few deltas, sentence-ish
                events.append(b'data: {"choices":[{"delta":{"content":' +
                              json.dumps(ANSWER[i:i + 12]).encode() + b'}}]}\n\n')
            events.append(b"data: [DONE]\n\n")
            self._send(200, "text/event-stream", b"".join(events))
            return

        if self.path == "/api/v1/audio/transcriptions":
            if not self._authed():
                return
            ctype = self.headers.get("Content-Type", "")
            fields = {}
            try:
                msg = BytesParser(policy=default_policy).parsebytes(
                    f"Content-Type: {ctype}".encode() +
                    b"\r\nMIME-Version: 1.0\r\n\r\n" + body)
                for part in msg.iter_parts():
                    name = part.get_param("name", header="content-disposition")
                    if name:
                        fields[name] = part.get_payload(decode=True) or b""
            except Exception as exc:
                log(f"stt multipart parse failed: {exc}")
            wav = fields.get("file", b"")
            log(f"stt model={fields.get('model', b'').decode(errors='replace')!r} "
                f"wav={len(wav)} bytes")
            self._json(200, {"text": "what is two plus two"})
            return

        if self.path == "/api/v1/audio/speech":
            if not self._authed():
                return
            req = json.loads(body)
            log(f"tts model={req.get('model')} voice={req.get('voice')} "
                f"input={len(req.get('input', ''))} chars")
            self._send(200, "audio/mpeg", MP3)
            return

        self._json(404, {"error": "not found"})


def main():
    global ANSWER, MP3, VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=int(os.environ.get("VENMOCK_PORT", "8699")))
    ap.add_argument("--answer", default=DEFAULT_ANSWER)
    ap.add_argument("--mp3", default=os.path.join(HERE, "..", "firmware", "include",
                                                  "chirp_mp3.h"))
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    ANSWER, VERBOSE = args.answer, args.verbose
    MP3 = load_chirp(args.mp3)
    if len(MP3) < 1000:
        raise SystemExit(f"chirp mp3 too small for the firmware's chunk check: {len(MP3)}")
    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    log(f"venice mock on :{args.port} (mp3 {len(MP3)} bytes, "
        f"answer {len(ANSWER)} chars)")
    server.serve_forever()


if __name__ == "__main__":
    main()

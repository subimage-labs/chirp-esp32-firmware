#!/usr/bin/env python3
"""Local-only TTS backend for the Hermes audio shim.

Synthesizes entirely on this machine with Paradee (8M-parameter distillation
of Kokoro-82M, Apache-2.0). No network, no API keys. Single voice: af_heart.

Output is MP3 (libmp3lame via lameenc) at 24 kHz mono, which is what the
device's ESP32-audioI2S player expects.

Modes: `--serve` runs the resident worker the shim talks to (127.0.0.1:8644);
`--text-file … --out …` is a one-shot CLI. Setup: host/setup.sh.
"""

import argparse
import json
import os
import shutil
import sys
import tempfile

# ONNX graph accepts at most 512 phoneme tokens; ~800 chars of English fits.
MAX_CHARS = 800
SAMPLE_RATE = 24_000

# Model files live here so inference never contacts the HF Hub. First run
# fetches them (9 MB); every run after that is fully offline.
MODEL_DIR = os.environ.get(
    "PARADEE_MODEL_DIR",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "models"))
ONNX_NAME = "paradee_int8.onnx"
CONFIG_NAME = "config.json"
HF_REPO = "sahilmahendrakar/Paradee-8M-v1.0"

_model = None


def get_model(threads):
    global _model
    if _model is None:
        from paradee import Paradee

        onnx_path = os.path.join(MODEL_DIR, ONNX_NAME)
        config_path = os.path.join(MODEL_DIR, CONFIG_NAME)
        if not (os.path.exists(onnx_path) and os.path.exists(config_path)):
            from huggingface_hub import hf_hub_download
            os.makedirs(MODEL_DIR, exist_ok=True)
            for remote, dest in ((f"onnx/{ONNX_NAME}", onnx_path),
                                 (CONFIG_NAME, config_path)):
                shutil.copy2(hf_hub_download(HF_REPO, remote), dest)

        os.environ["HF_HUB_OFFLINE"] = "1"   # load below must not touch network
        _model = Paradee(quantized=True, model_path=onnx_path,
                         config_path=config_path, threads=threads)
    return _model


def synthesize_mp3(text, speed, threads):
    import lameenc
    import numpy as np

    if len(text) > MAX_CHARS:
        print(f"Warning: truncating {len(text)} -> {MAX_CHARS} chars",
              file=sys.stderr)
        text = text[:MAX_CHARS]

    model = get_model(threads)
    audio = model(text, speed=speed)                  # float32 [-1, 1]

    pcm = (np.clip(audio, -1.0, 1.0) * 32767).astype(np.int16).tobytes()

    enc = lameenc.Encoder()
    enc.set_bit_rate(64)
    enc.set_in_sample_rate(SAMPLE_RATE)
    enc.set_channels(1)
    enc.set_quality(2)                                # 2 = high
    return bytes(enc.encode(pcm)) + bytes(enc.flush())


def serve(port, threads):
    """Resident worker: model loads once, each synth skips ONNX init.

    POST /  {"text": ..., "speed": 1.0}  ->  audio/mpeg
    GET  /health-ish (any GET)           ->  {"ok": true}
    """
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

    get_model(threads)                                # warm before serving

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *a):
            sys.stderr.write("[tts-worker] " + (fmt % a) + "\n")

        def do_GET(self):
            body = b'{"ok": true, "model": "paradee"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            try:
                n = int(self.headers.get("Content-Length", 0))
                req = json.loads(self.rfile.read(n) or b"{}")
                text = (req.get("text") or req.get("input") or "").strip()
                if not text:
                    raise ValueError("empty text")
                mp3 = synthesize_mp3(text, float(req.get("speed", 1.0)),
                                     threads)
                self.send_response(200)
                self.send_header("Content-Type", "audio/mpeg")
                self.send_header("Content-Length", str(len(mp3)))
                self.end_headers()
                self.wfile.write(mp3)
            except Exception as exc:
                body = json.dumps({"error": str(exc)}).encode()
                self.send_response(500)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

    print(f"[tts-worker] paradee serving on 127.0.0.1:{port}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Text -> MP3 locally via Paradee (no network).")
    parser.add_argument("--text-file", default=None,
                        help="Required for one-shot mode (not --serve).")
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--out", default=None, help="Output MP3 path.")
    parser.add_argument("--serve", action="store_true",
                        help="Resident HTTP worker instead of one-shot CLI.")
    parser.add_argument("--port", type=int, default=8644)
    args = parser.parse_args(argv)

    if args.serve:
        serve(args.port, args.threads)
        return 0

    if not args.text_file:
        parser.error("--text-file is required without --serve")
    try:
        with open(args.text_file, "r", encoding="utf-8") as fh:
            text = fh.read()
    except OSError as err:
        print(f"Error: cannot read text file: {err}", file=sys.stderr)
        return 1
    if not text.strip():
        print("Error: empty text", file=sys.stderr)
        return 1

    out_path = args.out
    if out_path:
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    else:
        fd, out_path = tempfile.mkstemp(suffix=".mp3")
        os.close(fd)

    try:
        mp3 = synthesize_mp3(text, args.speed, args.threads)
    except Exception as err:
        print(f"Error: synthesis failed: {err}", file=sys.stderr)
        return 1

    with open(out_path, "wb") as fh:
        fh.write(mp3)
    print(f"Output: {out_path}")
    print(f"Bytes: {len(mp3)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

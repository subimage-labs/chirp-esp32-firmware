#!/usr/bin/env python3
"""Generate publisher-produced manifests and independent cross-language expectations."""
import base64
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import re
import struct
import sys
import tempfile

REPO = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("publish_firmware", REPO / "host/publish-firmware.py")
pf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pf)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def make_image(version, build, pub):
    data = bytearray(0x200)
    data[0] = pf.ESP_IMAGE_MAGIC
    data[0x20:0x24] = struct.pack("<I", pf.ESP_APP_DESC_MAGIC)
    data += b"chirp_fw_version=" + version.encode("ascii") + b"\0"
    data += b"chirp_fw_build=" + str(build).encode("ascii") + b"\0"
    return bytes(data) + pub


def generate(out):
    out.mkdir(parents=True, exist_ok=True)
    version_match = re.search(r"\{1,(\d+)\}", pf.VERSION_OK.pattern)
    require(version_match is not None, "publisher version limit is missing")
    facts = {"manifest_max": pf.MANIFEST_MAX, "version_max": version_match[1]}
    with tempfile.TemporaryDirectory() as tmp:
        key_path, pub_path = str(Path(tmp) / "key.pem"), str(Path(tmp) / "pub.pem")
        with contextlib.redirect_stdout(io.StringIO()):
            pf.init_key(key_path, pub_path)
        key = pf.load_key(key_path)
        pub = pf.public_pem(key)
        bodies = {}
        for stem, version, build in (
            ("good", "v0.9-3-gabc1234", 1700000000),
            ("max", "v" + "0123456789" * 4 + "abcdef", 4294967295),
            ("dirty", "v1.0-2-gabc1234-dirty", 1700000000),
        ):
            m = pf.build_manifest(make_image(version, build, pub), key)
            require(isinstance(m, dict), f"{stem}: publisher rejected fixture")
            body = pf.manifest_bytes(m)
            canon = pf.canonical(m["build"], m["version"], m["size"], m["sha256"])
            require(pf.verify(pub, canon, m["sig"]), f"{stem}: signature verification failed")
            require(len(body) <= pf.MANIFEST_MAX, f"{stem}: manifest exceeds limit")
            (out / f"{stem}.json").write_bytes(body)
            bodies[stem] = body
            for field in ("build", "version", "size", "sha256"):
                facts[f"{stem}.{field}"] = m[field]
            facts.update({f"{stem}.canonical": canon, f"{stem}.sig_b64": m["sig"],
                          f"{stem}.sig_hex": base64.b64decode(m["sig"], validate=True).hex(),
                          f"{stem}.bytes": len(body)})
        version = "v" * 48
        data = make_image(version, 1700000000, pub)
        require(isinstance(pf.build_manifest(data, key), str), "publisher accepted 48-char version")
        m = {"build": 1700000000, "version": version, "size": len(data),
             "sha256": hashlib.sha256(data).hexdigest()}
        m["sig"] = pf.sign(key, pf.canonical(m["build"], version, m["size"], m["sha256"]))
        (out / "v48.json").write_bytes(pf.manifest_bytes(m))
        require(bodies["good"].endswith(b"\n"), "publisher body lacks final newline")
        for size in (512, 513):
            body = bodies["good"][:-1].ljust(size, b" ")
            require(len(body) == size, "padding fixture exceeds requested size")
            (out / f"pad{size}.json").write_bytes(body)
    (out / "expect.txt").write_text("".join(f"{k}={v}\n" for k, v in facts.items()), encoding="ascii")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: gen_ota_manifest.py OUTDIR")
    try:
        generate(Path(sys.argv[1]))
    except Exception as exc:
        sys.exit(f"OTA fixture generation failed: {exc}")

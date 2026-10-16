#!/usr/bin/env python3
"""Publish a signed firmware build for the device's boot-time OTA check.

Copies an app image and its signed manifest into the shim's firmware
directory (default ~/.local/share/chirp/firmware; override with --dir or
SHIM_FIRMWARE_DIR, which the shim reads too):
  chirp.bin       the image (written first)
  manifest.json   {"build","version","size","sha256","sig"} (written after)

The device fetches the manifest on every successful boot. It installs only
when the signature verifies against the public key compiled into the image it
runs and the build number is higher than every build it has run or recorded.
The bin must be complete before the manifest points at it; a boot between the
two writes sees the old manifest, and a mixed pair fails the device's sha256
check.

Signing: an ECDSA P-256 key pair made once by `--init-key`. The private key
lives at ~/.config/chirp/ota-signing.pem (mode 0600, never printed, never
passed on a command line); the public key goes to firmware/ota_pub.pem
(gitignored; the firmware build embeds it). The signature (base64 DER over
SHA-256) covers the canonical string
  chirp-ota-v1|<build>|<version>|<size>|<sha256>
so no field can change without the private key. Publishing without the key is
refused.

Version and build come from the image itself: the firmware embeds
"chirp_fw_version=<FW_VERSION>" and "chirp_fw_build=<unix seconds>" records
(firmware/src/ota.cpp; stamped by firmware/version.py and
firmware/ota_build.py). There is no override: the manifest must agree with
the image. Images stamped "unknown" (built outside git without the FW_VERSION
env) are refused, and so is an image that does not embed this signing key's
public half: devices that installed it could never be updated over WiFi
again. The image is also validated as an ESP32 app before publishing. Needs
the `cryptography` package.

Usage:
  host/publish-firmware.py --init-key      # once
  host/publish-firmware.py [--bin PATH] [--dir PATH]
"""

import argparse
import base64
import hashlib
import json
import os
import re
import stat
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join("firmware", ".pio", "build", "t-embed-cc1101",
                           "firmware.bin")
DEFAULT_DIR = os.environ.get(
    "SHIM_FIRMWARE_DIR",
    os.path.join(os.path.expanduser("~"), ".local", "share", "chirp",
                 "firmware"))
DEFAULT_KEY = os.path.join(os.path.expanduser("~"), ".config", "chirp",
                           "ota-signing.pem")
DEFAULT_PUB = os.path.join(HERE, "..", "firmware", "ota_pub.pem")
# The whole NUL-terminated record, so an over-long or odd stamp is refused
# rather than truncated to a prefix the device would never report.
VERSION_RE = re.compile(rb"chirp_fw_version=([^\x00]{0,128})\x00")
BUILD_RE = re.compile(rb"chirp_fw_build=([^\x00]{0,16})\x00")
VERSION_OK = re.compile(r"[0-9A-Za-z._+-]{1,47}")   # ota_policy.h's version buffer
BUILD_OK = re.compile(r"[1-9][0-9]{0,9}")           # a u32 without leading zeros
MANIFEST_MAX = 512                                  # ota_policy.h OTA_MANIFEST_MAX
U32_MAX = 0xFFFFFFFF
ESP_IMAGE_MAGIC = 0xE9
ESP_APP_DESC_MAGIC = 0xABCD5432


def canonical(build, version, size, sha256):
    """The signed string; firmware/include/ota_policy.h builds the same."""
    return "chirp-ota-v1|%d|%s|%d|%s" % (build, version, size, sha256)


def _crypto():
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
    except ImportError:
        sys.exit("error: the 'cryptography' package is required "
                 "(pip install cryptography)")
    return hashes, serialization, ec


def public_pem(key):
    """The PEM text the firmware build embeds for this private key."""
    _, serialization, _ = _crypto()
    return key.public_key().public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo)


def sign(key, text):
    """base64(DER ECDSA-P256 signature over SHA-256(text))."""
    hashes, _, ec = _crypto()
    der = key.sign(text.encode("ascii"), ec.ECDSA(hashes.SHA256()))
    return base64.b64encode(der).decode("ascii")


def verify(pub_pem_bytes, text, sig_b64):
    """True when sig_b64 is a valid signature of text under the public key."""
    hashes, serialization, ec = _crypto()
    from cryptography.exceptions import InvalidSignature
    pub = serialization.load_pem_public_key(pub_pem_bytes)
    try:
        pub.verify(base64.b64decode(sig_b64, validate=True),
                   text.encode("ascii"), ec.ECDSA(hashes.SHA256()))
        return True
    except (InvalidSignature, ValueError):
        return False


def init_key(key_path, pub_path):
    """Generate the signing key pair. Never overwrites an existing key: the
    devices flashed with its public half would stop accepting updates."""
    _, serialization, ec = _crypto()
    if os.path.exists(key_path):
        sys.exit("error: %s already exists; refusing to replace the signing "
                 "key (devices carrying its public half would reject every "
                 "update). Remove it yourself if you mean to rotate keys "
                 "and re-flash devices over USB." % key_path)
    os.makedirs(os.path.dirname(key_path), mode=0o700, exist_ok=True)
    priv = ec.generate_private_key(ec.SECP256R1())
    pem = priv.private_bytes(serialization.Encoding.PEM,
                            serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    fd = os.open(key_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as fh:
        fh.write(pem)
    os.makedirs(os.path.dirname(pub_path), exist_ok=True)
    write_atomic(os.path.dirname(pub_path), os.path.basename(pub_path),
                 public_pem(priv))
    print("signing key written to %s (keep it private and backed up)" % key_path)
    print("public key written to %s; rebuild the firmware so it embeds it"
          % os.path.normpath(pub_path))


def load_key(key_path):
    """The private key, or exits with what is wrong."""
    _, serialization, ec = _crypto()
    try:
        mode = os.stat(key_path).st_mode
    except OSError:
        sys.exit("error: no signing key at %s; run host/publish-firmware.py "
                 "--init-key first" % key_path)
    if mode & (stat.S_IRWXG | stat.S_IRWXO):
        sys.exit("error: %s is readable by others; chmod 600 it" % key_path)
    try:
        with open(key_path, "rb") as fh:
            priv = serialization.load_pem_private_key(fh.read(), password=None)
    except (OSError, ValueError, TypeError) as exc:
        sys.exit("error: cannot load signing key %s: %s"
                 % (key_path, type(exc).__name__))
    if not isinstance(priv, ec.EllipticCurvePrivateKey) or \
            not isinstance(priv.curve, ec.SECP256R1):
        sys.exit("error: %s is not an ECDSA P-256 key" % key_path)
    return priv


def app_descriptor(data):
    """The ESP32 app descriptor at image offset 0x20, or an error string."""
    if len(data) < 0x100 or data[0] != ESP_IMAGE_MAGIC:
        return "not an ESP32 image (bad magic byte)"
    if struct.unpack("<I", data[0x20:0x24])[0] != ESP_APP_DESC_MAGIC:
        return "no app descriptor at 0x20 (corrupt image?)"
    version = data[0x30:0x50].split(b"\0", 1)[0].decode("ascii", "replace")
    project = data[0x50:0x70].split(b"\0", 1)[0].decode("ascii", "replace")
    idf = data[0x90:0xB0].split(b"\0", 1)[0].decode("ascii", "replace")
    return version, project, idf


def image_version(data):
    """The version this image reports, or an error string."""
    hits = VERSION_RE.findall(data)
    if not len(hits) == 1:
        return "found %d chirp_fw_version records (want exactly 1)" % len(hits)
    version = hits[0].decode(errors="replace")
    if version == "unknown":
        return ("image was built without a version stamp (outside git, no "
                "FW_VERSION env); rebuild with FW_VERSION=<version>")
    if not VERSION_OK.fullmatch(version):
        return ("embedded version %r is not 1-47 chars of [0-9A-Za-z._+-]; "
                "rebuild with FW_VERSION=<version>" % version)
    return version


def image_build(data):
    """The build number this image reports (int), or an error string."""
    hits = BUILD_RE.findall(data)
    if len(hits) != 1:
        return "found %d chirp_fw_build records (want exactly 1)" % len(hits)
    text = hits[0].decode(errors="replace")
    if not BUILD_OK.fullmatch(text) or int(text) > U32_MAX:
        return "embedded build %r is not a 32-bit decimal number" % text
    return int(text)


def image_embeds_key(data, key):
    """True when the image carries this key's public half (the key a device
    running it would verify the next update against)."""
    return public_pem(key) in data


def build_manifest(data, key):
    """The signed manifest dict for an app image, or an error string."""
    version = image_version(data)
    if not VERSION_OK.fullmatch(version):
        return version
    build = image_build(data)
    if isinstance(build, str):
        return build
    if not image_embeds_key(data, key):
        return ("the image does not embed this signing key's public half "
                "(built without firmware/ota_pub.pem, or with another key); "
                "devices that installed it could not be updated over WiFi "
                "again. Rebuild with the matching firmware/ota_pub.pem")
    size = len(data)
    sha256 = hashlib.sha256(data).hexdigest()
    manifest = {
        "build": build,
        "version": version,
        "size": size,
        "sha256": sha256,
        "sig": sign(key, canonical(build, version, size, sha256)),
    }
    if len(manifest_bytes(manifest)) > MANIFEST_MAX:
        return "manifest exceeds %d bytes" % MANIFEST_MAX
    return manifest


def manifest_bytes(manifest):
    return (json.dumps(manifest, separators=(",", ":")) + "\n").encode()


def write_atomic(directory, name, payload):
    """Replace directory/name with payload via a temp file in the same dir."""
    fd, tmp = tempfile.mkstemp(dir=directory, prefix="." + name + ".")
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(payload)
        os.replace(tmp, os.path.join(directory, name))
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def published_build(directory):
    """Build number of the manifest currently published, or None."""
    try:
        with open(os.path.join(directory, "manifest.json"), "rb") as fh:
            return int(json.loads(fh.read())["build"])
    except (OSError, ValueError, KeyError, TypeError):
        return None


def main():
    ap = argparse.ArgumentParser(
        description="Publish a signed firmware image for boot-time OTA.")
    ap.add_argument("--init-key", action="store_true",
                    help="generate the signing key pair (once) and exit")
    ap.add_argument("--key", default=DEFAULT_KEY,
                    help="private signing key (default: %s)" % DEFAULT_KEY)
    ap.add_argument("--pub", default=DEFAULT_PUB,
                    help="where --init-key writes the public key "
                         "(default: firmware/ota_pub.pem)")
    ap.add_argument("--bin", default=DEFAULT_BIN,
                    help="image to publish (default: %s)" % DEFAULT_BIN)
    ap.add_argument("--dir", default=DEFAULT_DIR,
                    help="publish directory (default: %s)" % DEFAULT_DIR)
    args = ap.parse_args()

    if args.init_key:
        init_key(args.key, args.pub)
        return
    signer = load_key(args.key)

    try:
        with open(args.bin, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        sys.exit("error: cannot read %s: %s" % (args.bin, exc))

    desc = app_descriptor(data)
    if isinstance(desc, str):
        sys.exit("error: %s: %s" % (args.bin, desc))
    core_version, project, idf = desc
    print("app descriptor: project=%s app_version=%s idf=%s"
          % (project, core_version, idf))

    manifest = build_manifest(data, signer)
    if isinstance(manifest, str):
        sys.exit("error: %s" % manifest)

    previous = published_build(args.dir)
    if previous is not None and previous >= manifest["build"]:
        print("warning: published build %d is not older than this one (%d); "
              "devices that already run or recorded it will skip this image"
              % (previous, manifest["build"]), file=sys.stderr)
    os.makedirs(args.dir, exist_ok=True)
    write_atomic(args.dir, "chirp.bin", data)     # bin first: manifest follows
    write_atomic(args.dir, "manifest.json", manifest_bytes(manifest))
    print("published %s build=%d version=%s size=%d sha256=%s"
          % (args.dir, manifest["build"], manifest["version"],
             manifest["size"], manifest["sha256"]))


if __name__ == "__main__":
    main()

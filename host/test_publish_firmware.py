#!/usr/bin/env python3
"""Offline tests for host/publish-firmware.py: key handling, the signed
canonical string, and the manifest built from an image. Throwaway keys only.

The golden vector (canonical string + base64 signature + public key) is the
one firmware/test/host/ota_policy_test.cpp parses and canonicalises, so the
C++ and Python sides are pinned to the same bytes. The device's own
signature check is mbedtls and is exercised on the device.

    python3 host/test_publish_firmware.py
"""

import base64
import contextlib
import importlib.util
import io
import json
import os
import stat
import struct
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "publish_firmware", os.path.join(HERE, "publish-firmware.py"))
pf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pf)

SHA = "0123456789abcdef0123456789abcdef" "0123456789abcdef0123456789abcdef"
GOLDEN_CANON = "chirp-ota-v1|1700000000|v0.9-3-gabc1234|1234567|" + SHA
GOLDEN_SIG = ("MEYCIQCLQTVXigzgyKv3XQTYdo5M4ignlngBCwSlMDZGc8FBiAIhAMsDA2kR0x6TxO01"
              "2StueohV4wra/W4uNX/yOMXwYPBB")
GOLDEN_PUB = b"""-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEpBduhIMX/LEhvtHuHcNfISiY46Ty
QhWLreVIlYGrlq4GxgquNHa242TXTsDNUI02TODY2w3MCxxGrGuVw/fAzQ==
-----END PUBLIC KEY-----
"""


def read(path):
    with open(path, "rb") as fh:
        return fh.read()


def make_key(directory):
    path = os.path.join(directory, "k.pem")
    with contextlib.redirect_stdout(io.StringIO()):
        pf.init_key(path, os.path.join(directory, "pub.pem"))
    return pf.load_key(path), path


def make_image(version=b"v9-1-gdeadbee", build=b"1790000000", pub=b"", tail=b""):
    """A minimal ESP32-app-shaped image carrying the records the firmware embeds."""
    data = bytearray(0x200)
    data[0] = pf.ESP_IMAGE_MAGIC
    data[0x20:0x24] = struct.pack("<I", pf.ESP_APP_DESC_MAGIC)
    data += b"chirp_fw_version=" + version + b"\0"
    data += b"chirp_fw_build=" + build + b"\0"
    data += pub + tail
    return bytes(data)


class Signing(unittest.TestCase):
    def test_canonical_matches_the_device(self):
        self.assertEqual(pf.canonical(1700000000, "v0.9-3-gabc1234", 1234567, SHA),
                         GOLDEN_CANON)

    def test_golden_signature_verifies(self):
        self.assertTrue(pf.verify(GOLDEN_PUB, GOLDEN_CANON, GOLDEN_SIG))
        der = base64.b64decode(GOLDEN_SIG)
        self.assertEqual((der[0], len(der)), (0x30, 72))

    def test_golden_signature_rejects_any_changed_field(self):
        for text in (
            GOLDEN_CANON.replace("1700000000", "1700000001"),
            GOLDEN_CANON.replace("v0.9-3", "v0.9-4"),
            GOLDEN_CANON.replace("1234567", "1234568"),
            GOLDEN_CANON[:-1] + "e",
            GOLDEN_CANON.replace("chirp-ota-v1", "chirp-ota-v2"),
        ):
            self.assertFalse(pf.verify(GOLDEN_PUB, text, GOLDEN_SIG), text)

    def test_other_key_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            key, _ = make_key(d)
            sig = pf.sign(key, GOLDEN_CANON)
            self.assertTrue(pf.verify(pf.public_pem(key), GOLDEN_CANON, sig))
            self.assertFalse(pf.verify(GOLDEN_PUB, GOLDEN_CANON, sig))

    def test_bad_signature_text_is_a_failure_not_a_crash(self):
        self.assertFalse(pf.verify(GOLDEN_PUB, GOLDEN_CANON, "not base64!"))
        self.assertFalse(pf.verify(GOLDEN_PUB, GOLDEN_CANON, "AAAA"))


class Keys(unittest.TestCase):
    def test_init_key_writes_private_0600_and_public(self):
        with tempfile.TemporaryDirectory() as d:
            key_path = os.path.join(d, "sub", "k.pem")
            pub_path = os.path.join(d, "pub", "ota_pub.pem")
            with contextlib.redirect_stdout(io.StringIO()) as out:
                pf.init_key(key_path, pub_path)
            self.assertNotIn("PRIVATE", out.getvalue())
            self.assertEqual(stat.S_IMODE(os.stat(key_path).st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(os.stat(os.path.dirname(key_path)).st_mode), 0o700)
            pub = read(pub_path)
            self.assertTrue(pub.startswith(b"-----BEGIN PUBLIC KEY-----\n"))
            self.assertNotIn(b"PRIVATE", pub)
            signer = pf.load_key(key_path)
            self.assertEqual(pf.public_pem(signer), pub)

    def test_init_key_never_replaces_a_key(self):
        with tempfile.TemporaryDirectory() as d:
            _, path = make_key(d)
            before = read(path)
            with self.assertRaises(SystemExit):
                pf.init_key(path, os.path.join(d, "pub2.pem"))
            self.assertEqual(read(path), before)
            self.assertFalse(os.path.exists(os.path.join(d, "pub2.pem")))

    def test_publishing_needs_a_key(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit) as cm:
                pf.load_key(os.path.join(d, "missing.pem"))
            self.assertIn("--init-key", str(cm.exception))

    def test_group_readable_key_refused(self):
        with tempfile.TemporaryDirectory() as d:
            _, path = make_key(d)
            os.chmod(path, 0o640)
            with self.assertRaises(SystemExit):
                pf.load_key(path)

    def test_wrong_curve_refused(self):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "k1.pem")
            pem = ec.generate_private_key(ec.SECP384R1()).private_bytes(
                serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption())
            fd = os.open(path, os.O_WRONLY | os.O_CREAT, 0o600)
            with os.fdopen(fd, "wb") as fh:
                fh.write(pem)
            with self.assertRaises(SystemExit):
                pf.load_key(path)


class Manifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.key, _ = make_key(self.tmp.name)
        self.pub = pf.public_pem(self.key)

    def test_signed_manifest_for_an_image(self):
        img = make_image(pub=self.pub)
        m = pf.build_manifest(img, self.key)
        self.assertIsInstance(m, dict)
        self.assertEqual(list(m), ["build", "version", "size", "sha256", "sig"])
        self.assertEqual((m["build"], m["version"], m["size"]),
                         (1790000000, "v9-1-gdeadbee", len(img)))
        self.assertTrue(pf.verify(self.pub, pf.canonical(
            m["build"], m["version"], m["size"], m["sha256"]), m["sig"]))
        body = pf.manifest_bytes(m)
        self.assertLessEqual(len(body), pf.MANIFEST_MAX)
        self.assertEqual(json.loads(body), m)

    def test_image_without_this_key_refused(self):
        for img in (make_image(), make_image(pub=GOLDEN_PUB)):
            m = pf.build_manifest(img, self.key)
            self.assertIsInstance(m, str)
            self.assertIn("public half", m)

    def test_image_stamps_checked(self):
        k = self.key
        unknown = pf.build_manifest(make_image(version=b"unknown", pub=self.pub), k)
        self.assertIsInstance(unknown, str)
        self.assertIn("version stamp", unknown)
        self.assertIsInstance(pf.build_manifest(make_image(version=b"has space", pub=self.pub), k), str)
        for build in (b"", b"0", b"01", b"-5", b"4294967296", b"12x", b"1" * 11):
            self.assertIsInstance(
                pf.build_manifest(make_image(build=build, pub=self.pub), k), str, build)
        # no build record at all, and two of them
        img = make_image(pub=self.pub)
        self.assertIsInstance(pf.build_manifest(img.replace(b"chirp_fw_build", b"chirp_fw_buile"), k), str)
        self.assertIsInstance(pf.build_manifest(img + b"chirp_fw_build=5\0", k), str)

    def test_u32_max_build_accepted(self):
        m = pf.build_manifest(make_image(build=b"4294967295", pub=self.pub), self.key)
        self.assertEqual(m["build"], 4294967295)

    def test_published_build_reads_the_current_manifest(self):
        d = self.tmp.name
        self.assertIsNone(pf.published_build(d))
        m = pf.build_manifest(make_image(pub=self.pub), self.key)
        pf.write_atomic(d, "manifest.json", pf.manifest_bytes(m))
        self.assertEqual(pf.published_build(d), 1790000000)
        pf.write_atomic(d, "manifest.json", b"garbage")
        self.assertIsNone(pf.published_build(d))


if __name__ == "__main__":
    unittest.main(verbosity=2)

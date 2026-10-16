"""Hermetic contract tests for check-secrets.sh. Synthetic values only."""

import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

HOSTNAME = "synthhost42"
PROFILE_ID = "SynthProf42"
SSID = "SynthSsid42"
PASSWORD = "s3cret-fixture-value-42"
KEY_LIT = "SynthKey4242"
HERMES = "SynthHermes9"
SHIM = "synth.shim.host:9090"
SHIM_HOST = "synth.shim.host"
HEX40 = "0123456789abcdef" * 2 + "01234567"
HEX39 = "0123456789abcdef" * 2 + "0123456"
PEM_BODY = "AAAAB3NzaC1yc2EAAAADAQABAAABAQDsynthfixturekeybody99"
PEM_BODY_ART = "AAAAB3NzaC1yc2EAAAADAQABAAABAQDartifactpemfixture99"
NON_UTC_TZ = "XYZ5"

DERIVED_SECRETS = [
    SSID,
    PASSWORD,
    KEY_LIT,
    HERMES,
    SHIM,
    SHIM_HOST,
    HOSTNAME,
    PROFILE_ID,
]

KEYS = [
    "password",
    "passwd",
    "pass",
    "passphrase",
    "secret",
    "api_key",
    "apikey",
    "api-key",
    "api_secret",
    "client_secret",
    "token",
    "access_token",
    "auth_token",
    "private_key",
]

DEVICE_ENV = """\
# synthetic fixture store — not a real device
ssid=%s
ssid=ab
pass="%s"
pass=your-skip-me
key=%s
key=*
hermes=%s
hermes=xy
shim="%s"
shim=
""" % (
    SSID,
    PASSWORD,
    KEY_LIT,
    HERMES,
    SHIM,
)

PEM_TEXT = (
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "%s\n"
    "-----END RSA PRIVATE KEY-----\n"
)


def locate_script():
    override = os.environ.get("CHECK_SECRETS_SCRIPT")
    if override:
        return Path(override)
    return Path(__file__).resolve().parent / "check-secrets.sh"


def _join(base, rel):
    rel = rel.replace("\\", "/").lstrip("/")
    return base.joinpath(*rel.split("/")) if rel else base


class GuardFixture:
    """Temp script tree, git repo, HOME, and hostname PATH stub."""

    def __enter__(self):
        self._td = tempfile.TemporaryDirectory(prefix="secguard-")
        self.root = Path(self._td.name)
        self.app = self.root / "app"
        self.repo = self.root / "repo"
        self.home = self.root / "home"
        self.bindir = self.root / "bin"
        self.nongit = self.root / "nongit"
        self.script = self.app / "host" / "check-secrets.sh"
        self._build()
        return self

    def __exit__(self, exc_type, exc, tb):
        self._td.cleanup()
        return False

    def _build(self):
        for d in (
            self.app / "host",
            self.app / "firmware",
            self.repo,
            self.home / ".hermes" / "profiles" / PROFILE_ID,
            self.home / ".hermes" / "profiles" / "ab",
            self.bindir,
            self.nongit,
            self.root / "tmp",
            self.root / "no-hooks",
        ):
            d.mkdir(parents=True, exist_ok=True)
        (self.home / ".hermes" / "profiles" / PROFILE_ID / ".keep").write_text("x\n")
        (self.home / ".hermes" / "profiles" / "ab" / ".keep").write_text("x\n")
        stub = self.bindir / "hostname"
        stub.write_text("#!/bin/sh\necho %s\n" % HOSTNAME)
        stub.chmod(0o755)
        src = locate_script()
        if not src.is_file():
            raise FileNotFoundError("check-secrets.sh not found: %s" % src)
        self.script.write_bytes(src.read_bytes())
        self.script.chmod(self.script.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        (self.app / "firmware" / "device.env").write_text(DEVICE_ENV)
        self._run(["git", "config", "--global", "user.email", "fixture@example.com"])
        self._run(["git", "config", "--global", "user.name", "Fixture User"])
        self._run(["git", "config", "--global", "init.defaultBranch", "main"])
        self._run(["git", "config", "--global", "commit.gpgsign", "false"])
        self._run(["git", "config", "--global", "safe.directory", "*"])
        self._run(["git", "config", "--global", "core.hooksPath", str(self.root / "no-hooks")])
        self._run(["git", "init", "-q"], cwd=self.repo)
        if self._offset("UTC") != "+0000":
            raise RuntimeError("TZ=UTC did not yield +0000 from date +%z")
        if self._offset(NON_UTC_TZ) == "+0000":
            raise RuntimeError("non-UTC TZ fixture still reports +0000")

    def env(self, tz):
        path = os.environ.get("PATH", "/usr/bin:/bin")
        return {
            "PATH": str(self.bindir) + os.pathsep + path,
            "HOME": str(self.home),
            "TZ": tz,
            "LC_ALL": "C",
            "LANG": "C",
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CEILING_DIRECTORIES": str(self.root),
            "GIT_TERMINAL_PROMPT": "0",
            "TMPDIR": str(self.root / "tmp"),
        }

    def _offset(self, tz):
        r = subprocess.run(
            ["date", "+%z"],
            env=self.env(tz),
            capture_output=True,
            text=True,
            timeout=15,
            check=True,
        )
        return r.stdout.strip()

    def _run(self, cmd, cwd=None, check=True):
        r = subprocess.run(
            [str(c) for c in cmd],
            cwd=str(cwd) if cwd else None,
            env=self.env("UTC"),
            capture_output=True,
            text=True,
            timeout=30,
        )
        if check and r.returncode != 0:
            raise RuntimeError("%s -> %s\n%s\n%s" % (cmd, r.returncode, r.stdout, r.stderr))
        return r

    def run(self, args, cwd, tz):
        return subprocess.run(
            [str(self.script), *[str(a) for a in args]],
            cwd=str(cwd),
            env=self.env(tz),
            capture_output=True,
            text=True,
            timeout=30,
        )

    def clear_index(self):
        index = self.repo / ".git" / "index"
        if index.exists():
            index.unlink()

    def stage(self, relpath, content):
        relpath = relpath.replace("\\", "/")
        path = _join(self.repo, relpath)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        self._run(["git", "add", "--", relpath], cwd=self.repo)

    def tree(self, name, files):
        d = self.root / name
        if d.exists():
            raise RuntimeError("tree exists: %s" % name)
        d.mkdir(parents=True)
        for rel, content in files.items():
            path = _join(d, rel)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        return d

    def write(self, path, content):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path


class CheckSecretsTest(unittest.TestCase):
    def setUp(self):
        self._fx_cm = GuardFixture()
        self.fx = self._fx_cm.__enter__()

    def tearDown(self):
        self._fx_cm.__exit__(None, None, None)

    def assert_redacted(self, proc, extras=()):
        secrets = [s for s in (*DERIVED_SECRETS, *extras) if s]
        for stream_name, stream in (("stdout", proc.stdout or ""), ("stderr", proc.stderr or "")):
            low = stream.lower()
            for secret in secrets:
                self.assertNotIn(
                    secret.lower(),
                    low,
                    "%s leaked secret %r\nstdout=%r\nstderr=%r"
                    % (stream_name, secret, proc.stdout, proc.stderr),
                )

    def assert_blocked(self, proc, rule, filename=None, extras=(), hook=False, content=False):
        out = (proc.stdout or "") + (proc.stderr or "")
        self.assertEqual(proc.returncode, 1, out)
        self.assertIn("BLOCKED:", out)
        self.assertIn(rule, out)
        if filename is not None:
            self.assertIn(filename, out)
        if hook:
            self.assertIn("Secret guard fired", out)
        self.assertNotIn("commit in UTC", out)
        if hook and content:
            self.assertRegex(out, r"\d")
        self.assert_redacted(proc, extras)

    def assert_allowed(self, proc, extras=()):
        out = (proc.stdout or "") + (proc.stderr or "")
        self.assertEqual(proc.returncode, 0, out)
        self.assertNotIn("BLOCKED", proc.stdout or "")
        self.assertNotIn("BLOCKED", proc.stderr or "")
        self.assertNotIn("Secret guard fired", out)
        self.assertNotIn("commit in UTC", out)
        self.assert_redacted(proc, extras)

    def _block_hook(self, rel, content, rule, extras=(), filename=None, content_rule=True):
        self.fx.clear_index()
        self.fx.stage(rel, content)
        proc = self.fx.run([], cwd=self.fx.repo, tz="UTC")
        self.assert_blocked(
            proc,
            rule,
            filename=rel if filename is None else filename,
            extras=extras,
            hook=True,
            content=content_rule,
        )
        return proc

    def _allow_hook(self, rel, content, extras=()):
        self.fx.clear_index()
        self.fx.stage(rel, content)
        proc = self.fx.run([], cwd=self.fx.repo, tz="UTC")
        self.assert_allowed(proc, extras)
        return proc

    def test_credential_assignments_blocked(self):
        styles = []
        for i, key in enumerate(KEYS):
            qv = "q%02dZ" % i
            uv = "uq%02dsc9a" % i
            jv = "j%02dZ" % i
            cv = "c%02dZ" % i
            styles.append(("quoted-%s" % key, '%s = "%s"\n' % (key.upper(), qv), qv))
            styles.append(("unquoted-%s" % key, "%s = %s\n" % (key, uv), uv))
            styles.append(("json-%s" % key, '"%s": "%s"\n' % (key, jv), jv))
            styles.append(("colon-%s" % key, '%s: "%s"\n' % (key, cv), cv))
        styles.append(("nospace", 'password="abcd1234"\n', "abcd1234"))
        styles.append(("colon-unquoted", "password: uqcolon9z\n", "uqcolon9z"))
        styles.append(("path-value", "password = /opt/sec99/data\n", "/opt/sec99/data"))
        styles.append(
            ("url-value", "token = https://files.example/s3cretpath\n", "https://files.example/s3cretpath")
        )
        for name, content, secret in styles:
            with self.subTest(name=name):
                self._block_hook("cred.txt", content, "credential assignment", extras=[secret])

    def test_credential_assignment_message_and_tree(self):
        msg_secret = "MsgPass99zz"
        tree_secret = "TreePass99z"
        msg = self.fx.write(self.fx.root / "COMMIT_EDITMSG", 'password = "%s"\n' % msg_secret)
        proc = self.fx.run(["--message", msg], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(
            proc, "credential assignment", filename="commit message", extras=[msg_secret]
        )
        d = self.fx.tree("assign-tree", {"sub/creds.txt": 'api_key = "%s"\n' % tree_secret})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(
            proc, "credential assignment", filename="creds.txt", extras=[tree_secret]
        )

    def test_staged_env_local_blocked(self):
        secret = "EnvLocalSec99"
        self._block_hook(
            ".env.local",
            'password = "%s"\n' % secret,
            "secret file",
            extras=[secret],
            content_rule=False,
        )

    def test_staged_other_secret_filenames_blocked(self):
        names = [".env", "prod.env", "certs/app.pem", "certs/id_rsa.key", "secrets.env.production"]
        for name in names:
            with self.subTest(name=name):
                secret = "NameSec%02d" % (abs(hash(name)) % 100)
                self._block_hook(name, "marker %s\n" % secret, "secret file", extras=[secret], content_rule=False)

    def test_scan_tree_secrets_env_production_blocked(self):
        secret = "TreeEnvSec99"
        d = self.fx.tree(
            "env-tree",
            {
                "pkg/secrets.env.production": 'password = "%s"\n' % secret,
                "README": "public\n",
            },
        )
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "secret file", filename="secrets.env.production", extras=[secret])

    def test_scan_tree_secret_filenames_blocked(self):
        d = self.fx.tree(
            "names-tree",
            {
                ".env": "x\n",
                "nested/app.pem": "x\n",
                "nested/id.key": "x\n",
                "prod.env": "x\n",
            },
        )
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        out = (proc.stdout or "") + (proc.stderr or "")
        self.assertEqual(proc.returncode, 1, out)
        self.assertIn("secret file", out)
        for name in (".env", "app.pem", "id.key", "prod.env"):
            self.assertIn(name, out)
        self.assert_redacted(proc)

    def test_staged_firmware_device_env_blocked(self):
        self._block_hook(
            "firmware/device.env",
            DEVICE_ENV,
            "secret file",
            content_rule=False,
        )

    def test_hex_token_blocked(self):
        content = 'hash = "%s"\n' % HEX40
        self._block_hook("hex.txt", content, "hex token", extras=[HEX40])
        msg = self.fx.write(self.fx.root / "hex.msg", content)
        proc = self.fx.run(["--message", msg], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "hex token", filename="commit message", extras=[HEX40])
        d = self.fx.tree("hex-tree", {"sub/hash.txt": content})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "hex token", filename="hash.txt", extras=[HEX40])

    def test_unquoted_and_short_hex_allowed(self):
        self._allow_hook("hex.txt", "hash = %s\n" % HEX40, extras=[HEX40])
        self._allow_hook("hex39.txt", 'hash = "%s"\n' % HEX39, extras=[HEX39])

    def test_private_ipv4_ranges_blocked(self):
        ips = ["10.1.2.3", "172.16.4.5", "172.31.6.7", "192.168.8.9"]
        for ip in ips:
            with self.subTest(ip=ip):
                self._block_hook("net.txt", "server %s up\n" % ip, "private IPv4", extras=[ip])
        d = self.fx.tree("ip-tree", {"firmware/src/x.cpp": "addr 10.2.3.4;\n"})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "private IPv4", filename="x.cpp", extras=["10.2.3.4"])

    def test_public_ipv4_allowed(self):
        self._allow_hook(
            "pub.txt",
            "dns 8.8.8.8\nedge 172.15.5.5\nother 172.32.1.1\nwan 192.169.1.1\n",
            extras=["8.8.8.8", "172.15.5.5", "172.32.1.1", "192.169.1.1"],
        )

    def test_ipv4_in_firmware_lib_allowed(self):
        content = "a 10.9.8.7 b 172.16.1.1 c 172.31.2.2 d 192.168.3.3\n"
        ips = ["10.9.8.7", "172.16.1.1", "172.31.2.2", "192.168.3.3"]
        self._allow_hook("firmware/lib/x.cpp", content, extras=ips)
        d = self.fx.tree(
            "lib-tree",
            {"firmware/lib/x.cpp": content, "README": "ok\n"},
        )
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc, ips)

    def test_artifact_ipv4_not_exempt(self):
        path = self.fx.write(self.fx.root / "out" / "firmware" / "lib" / "x.cpp", "addr 10.4.5.6;\n")
        proc = self.fx.run(["--scan-artifact", path], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "private IPv4", filename="x.cpp", extras=["10.4.5.6"])

    def test_derived_device_env_value_blocked(self):
        content = "see %s today\n" % PASSWORD
        self._block_hook("note.txt", content, "known private value", extras=[PASSWORD])
        d = self.fx.tree("derived-tree", {"sub/note.txt": content})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "known private value", filename="note.txt", extras=[PASSWORD])
        msg = self.fx.write(self.fx.root / "derived.msg", content)
        proc = self.fx.run(["--message", msg], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "known private value", filename="commit message", extras=[PASSWORD])

    def test_word_shaped_literal_whole_word(self):
        self._block_hook("ssid.txt", "see %s now\n" % SSID, "known private value", extras=[SSID])
        self._allow_hook("ssid2.txt", "%sextra\n" % SSID, extras=[SSID])

    def test_non_word_literal_is_substring(self):
        embedded = "xx%syy" % SHIM_HOST
        self._block_hook("shim.txt", embedded + "\n", "known private value", extras=[SHIM_HOST, embedded])

    def test_hostname_word_blocked(self):
        content = "box %s up\n" % HOSTNAME
        self._block_hook("host.txt", content, "known private value", extras=[HOSTNAME])
        d = self.fx.tree("host-tree", {"h.txt": content})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "known private value", filename="h.txt", extras=[HOSTNAME])
        self._allow_hook("host2.txt", "%sextra\n" % HOSTNAME, extras=[HOSTNAME])

    def test_profile_id_blocked(self):
        content = "owner synthprof42 assigned\n"
        self._block_hook("owner.txt", content, "Hermes profile id", extras=[PROFILE_ID, "synthprof42"])
        d = self.fx.tree("prof-tree", {"p.txt": "id %s\n" % PROFILE_ID})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "Hermes profile id", filename="p.txt", extras=[PROFILE_ID])
        self._allow_hook("owner2.txt", "%sextra\n" % PROFILE_ID, extras=[PROFILE_ID])

    def test_message_profile_id_blocked(self):
        msg = self.fx.write(self.fx.root / "COMMIT_EDITMSG", "Release notes for synthprof42\n")
        proc = self.fx.run(["--message", msg], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(
            proc,
            "Hermes profile id",
            filename="commit message",
            extras=[PROFILE_ID, "synthprof42"],
        )

    def test_pem_private_key_block_blocked(self):
        content = PEM_TEXT % PEM_BODY
        extras = [PEM_BODY, "-----BEGIN", "PRIVATE KEY-----"]
        self._block_hook("notes.txt", content, "private key block", extras=extras)
        msg = self.fx.write(self.fx.root / "pem.msg", content)
        proc = self.fx.run(["--message", msg], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "private key block", filename="commit message", extras=extras)
        d = self.fx.tree("pem-tree", {"sub/notes.txt": content})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "private key block", filename="notes.txt", extras=extras)

    def test_non_utc_commit_blocked(self):
        self.fx.stage("a.txt", "ok\n")
        proc = self.fx.run([], cwd=self.fx.repo, tz=NON_UTC_TZ)
        out = (proc.stdout or "") + (proc.stderr or "")
        self.assertEqual(proc.returncode, 1, out)
        self.assertIn("BLOCKED: commit in UTC", out)
        self.assertIn("Secret guard fired", out)
        self.assert_redacted(proc)

    def test_artifact_derived_literal_blocked(self):
        path = self.fx.write(self.fx.root / "release.txt", "bundle %s\n" % PASSWORD)
        proc = self.fx.run(["--scan-artifact", path], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "known private value", filename="release.txt", extras=[PASSWORD])

    def test_artifact_pem_blocked(self):
        content = PEM_TEXT % PEM_BODY_ART
        extras = [PEM_BODY_ART, "-----BEGIN", "PRIVATE KEY-----"]
        path = self.fx.write(self.fx.root / "bundle.txt", content)
        proc = self.fx.run(["--scan-artifact", path], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_blocked(proc, "private key block", filename="bundle.txt", extras=extras)

    def test_artifact_ignores_filename_assignment_and_hex(self):
        path = self.fx.write(
            self.fx.root / ".env.local",
            'password = "ArtifactPass99"\nhash = "%s"\n' % HEX40,
        )
        proc = self.fx.run(["--scan-artifact", path], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc, ["ArtifactPass99", HEX40])

    def test_message_ignores_secret_filename(self):
        msg = self.fx.write(self.fx.root / "id_rsa.key", "Ship the public release.\n")
        proc = self.fx.run(["--message", msg], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc)

    def test_staged_device_env_example_allowed(self):
        self._allow_hook(
            "firmware/device.env.example",
            "ssid=your-ssid\npass=your-password\n# sample\n",
        )

    def test_config_template_allowed(self):
        self._allow_hook("config.template", "name = template\n")

    def test_secret_suffix_exemptions_allowed(self):
        self.fx.clear_index()
        for rel, content in {
            "certs/app.pem.example": "not a key\n",
            "certs/app.key.sample": "not a key\n",
            "certs/app.env.template": "placeholder\n",
            "config.sample": "ok\n",
        }.items():
            self.fx.stage(rel, content)
        proc = self.fx.run([], cwd=self.fx.repo, tz="UTC")
        self.assert_allowed(proc)
        d = self.fx.tree(
            "exempt-tree",
            {
                "firmware/device.env.example": "ssid=your-ssid\n",
                "config.template": "name = template\n",
                "certs/app.pem.sample": "nope\n",
                "certs/app.key.template": "nope\n",
            },
        )
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc)

    def test_placeholder_exemptions_allowed(self):
        content = "\n".join(
            [
                'password = "your-password-here"',
                'password = "YOUR-PASSWORD-HERE"',
                'passwd = "your_password"',
                'pass = "your password"',
                'passphrase = "placeholder-value"',
                'secret = "changeme"',
                'secret = "ChangeMe"',
                'api_key = "xxxx"',
                'apikey = "****"',
                'api-key = "x*x*"',
                'api_secret = "xxxsecret"',
                'client_secret = "sample_value"',
                'token = "example-token"',
                'access_token = "${API_KEY}"',
                'auth_token = "<token>"',
                "password = ab12cde",
                "",
            ]
        )
        self._allow_hook("placeholders.txt", content)

    def test_identifier_shaped_value_allowed(self):
        self._allow_hook("ident.txt", "token = os.environ\n")

    def test_short_quoted_value_allowed(self):
        self._allow_hook("short.txt", 'password = "xy"\n')

    def test_skipped_device_env_placeholders_not_literals(self):
        self._allow_hook("skipped.txt", "ab your-skip-me * xy\n")

    def test_clean_file_allowed(self):
        self._allow_hook("README", "All good.\nNo secrets in this file.\n")

    def test_clean_commit_message_allowed(self):
        msg = self.fx.write(self.fx.root / "clean.msg", "Fix the widget display\n")
        proc = self.fx.run(["--message", msg], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc)

    def test_clean_tree_allowed(self):
        d = self.fx.tree("clean-tree", {"README": "public tree\n", "sub/ok.txt": "nothing to see\n"})
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc)

    def test_clean_artifact_allowed(self):
        path = self.fx.write(self.fx.root / "artifact.txt", "release notes v1 ok\n")
        proc = self.fx.run(["--scan-artifact", path], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc)

    def test_scan_tree_skips_git(self):
        secret = "GitDirSecret99"
        d = self.fx.tree(
            "git-skip",
            {
                ".git/secrets.env": 'password = "%s"\n%s\n' % (secret, PASSWORD),
                ".git/objects/pack/leak.txt": "%s\n%s\n" % (PROFILE_ID, HOSTNAME),
                "sub/.git/hidden.key": PEM_TEXT % PEM_BODY,
                "README": "public\n",
                "sub/ok.txt": "still public\n",
            },
        )
        proc = self.fx.run(["--scan-tree", d], cwd=self.fx.nongit, tz=NON_UTC_TZ)
        self.assert_allowed(proc, [secret, PEM_BODY, "-----BEGIN"])

    def test_hook_ignores_unstaged_changes(self):
        dirty = "DirtyWork99"
        self.fx.stage("clean.txt", "public notes only\n")
        (self.fx.repo / "clean.txt").write_text('password = "%s"\n' % dirty)
        (self.fx.repo / "leak.txt").write_text("%s\n%s\n" % (PASSWORD, PROFILE_ID))
        proc = self.fx.run([], cwd=self.fx.repo, tz="UTC")
        self.assert_allowed(proc, [dirty])

    def test_empty_index_utc_allowed(self):
        proc = self.fx.run([], cwd=self.fx.repo, tz="UTC")
        self.assert_allowed(proc)


if __name__ == "__main__":
    unittest.main()

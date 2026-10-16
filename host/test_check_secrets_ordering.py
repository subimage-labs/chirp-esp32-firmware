# Ordering regression tests for check-secrets.sh. Synthetic values only.
import json
import os
import shutil
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_check_secrets import GuardFixture, NON_UTC_TZ, PASSWORD, locate_script  # noqa: E402

NL = chr(10)
PH_KEY = 'your-api-key-here'
PH_PW = 'your-password-here'
KEY = 'api_key'
PW = 'password'


def val(n):
    return 'Zk%02dRt71Qw9' % n


def assign(key, value, sep=' = '):
    return key + sep + value


def quoted(key, value):
    return key + '=' + chr(39) + value + chr(39)


LEAKS = [
    ('json-hint-before', json.dumps({'hint': PH_KEY, KEY: val(1)}), [val(1)]),
    ('json-hint-after', json.dumps({KEY: val(2), 'hint': PH_KEY}), [val(2)]),
    ('json-placeholder-key-then-password', json.dumps({KEY: PH_KEY, PW: val(3)}), [val(3)]),
    ('json-password-then-placeholder-key', json.dumps({PW: val(4), KEY: PH_KEY}), [val(4)]),
    ('json-multiple-real', json.dumps({KEY: val(5), PW: val(6)}), [val(5), val(6)]),
    ('shell-placeholder-then-real', ' '.join([quoted(KEY, PH_KEY), quoted(PW, val(7))]), [val(7)]),
    ('shell-real-then-placeholder', ' '.join([quoted(PW, val(8)), quoted(KEY, PH_KEY)]), [val(8)]),
    ('unquoted-after-separator', 'name: svc; ' + assign(KEY, val(9)), [val(9)]),
    (
        'nested-placeholder-before',
        json.dumps({'auth': {KEY: PH_KEY}, 'opts': {'retries': 3, 'verbose': True}, PW: val(10)}),
        [val(10)],
    ),
    (
        'nested-placeholder-after',
        json.dumps({PW: val(11), 'auth': {KEY: PH_KEY}, 'opts': {'retries': 3}}),
        [val(11)],
    ),
]

MODE_SUBSET = {
    'json-hint-before',
    'json-multiple-real',
    'shell-real-then-placeholder',
    'unquoted-after-separator',
    'nested-placeholder-before',
}

PLACEHOLDER_ONLY = [
    json.dumps({KEY: PH_KEY, PW: PH_PW}),
    ' '.join([quoted(KEY, PH_KEY), quoted(PW, PH_PW)]),
]
BENIGN_NESTED = json.dumps({'opts': {'retries': 3, 'mode': 'fast'}, 'auth': {KEY: PH_KEY}})

RESIDUALS = [
    ('call-token', assign(KEY, 'secrets.token_urlsafe(32)'), []),
    ('call-hidden', assign(PW, 's3cret(hidden)'), []),
    ('member-cfg', assign('cfg.password', val(20)), [val(20)]),
    ('member-s', assign('s.pass', val(21)), [val(21)]),
    ('member-in', assign('in.pass', val(22)), [val(22)]),
]


class CheckSecretsOrderingTest(unittest.TestCase):
    def setUp(self):
        self._cm = GuardFixture()
        self.fx = self._cm.__enter__()
        self._n = 0

    def tearDown(self):
        self._cm.__exit__(None, None, None)

    def streams_clean(self, proc, secrets):
        for stream in (proc.stdout or '', proc.stderr or ''):
            for secret in secrets:
                self.assertNotIn(secret.lower(), stream.lower())

    def assert_leak(self, proc, secrets):
        out = (proc.stdout or '') + (proc.stderr or '')
        self.assertEqual(proc.returncode, 1, out)
        self.assertIn('BLOCKED', out)
        self.streams_clean(proc, secrets)
        return out

    def assert_ok(self, proc, secrets=()):
        out = (proc.stdout or '') + (proc.stderr or '')
        self.assertEqual(proc.returncode, 0, out)
        self.assertNotIn('BLOCKED', out)
        self.streams_clean(proc, secrets)

    def hook(self, line, rel='conf.txt'):
        self.fx.clear_index()
        self.fx.stage(rel, line)
        return self.fx.run([], cwd=self.fx.repo, tz='UTC')

    def message(self, line, bare):
        self._n += 1
        path = self.fx.write(self.fx.root / ('m%d' % self._n) / 'COMMIT_EDITMSG', line)
        if bare:
            self.fx.clear_index()
            return self.fx.run([path], cwd=self.fx.repo, tz='UTC')
        return self.fx.run(['--message', path], cwd=self.fx.nongit, tz=NON_UTC_TZ)

    def tree(self, line):
        self._n += 1
        d = self.fx.tree('t%d' % self._n, {'sub/conf.txt': line})
        return self.fx.run(['--scan-tree', d], cwd=self.fx.nongit, tz=NON_UTC_TZ)

    def test_hook_blocks_every_ordering(self):
        for name, line, secrets in LEAKS:
            with self.subTest(name=name):
                self.assert_leak(self.hook(line + NL), secrets)

    def test_message_and_tree_modes_block(self):
        for name, line, secrets in LEAKS:
            if name not in MODE_SUBSET:
                continue
            with self.subTest(name=name, mode='message'):
                self.assert_leak(self.message(line + NL, bare=False), secrets)
            with self.subTest(name=name, mode='bare-message'):
                self.assert_leak(self.message(line + NL, bare=True), secrets)
            with self.subTest(name=name, mode='tree'):
                self.assert_leak(self.tree(line + NL), secrets)

    def test_placeholder_only_lines_allowed(self):
        for i, line in enumerate(PLACEHOLDER_ONLY + [BENIGN_NESTED]):
            with self.subTest(i=i):
                self.assert_ok(self.hook(line + NL))
        line = PLACEHOLDER_ONLY[0] + NL
        self.assert_ok(self.message(line, bare=False))
        self.assert_ok(self.message(line, bare=True))
        self.assert_ok(self.tree(line))

    def test_accepted_residuals_allowed(self):
        for name, line, secrets in RESIDUALS:
            with self.subTest(name=name):
                self.assert_ok(self.hook(line + NL), secrets)

    def test_known_private_value_blocks_in_member_access_line(self):
        line = assign('cfg.password', PASSWORD) + NL
        out = self.assert_leak(self.hook(line), [PASSWORD])
        self.assertIn('known private value', out)

    def test_known_private_value_blocks_in_exception_listed_path(self):
        src = locate_script().parent / 'guard-exceptions'
        self.assertTrue(src.is_file(), 'guard-exceptions missing next to guard')
        shutil.copyfile(str(src), str(self.fx.script.parent / 'guard-exceptions'))
        line = assign('cfg.password', PASSWORD) + NL
        out = self.assert_leak(self.hook(line, rel='host/test_check_secrets.py'), [PASSWORD])
        self.assertIn('known private value', out)


if __name__ == '__main__':
    unittest.main()

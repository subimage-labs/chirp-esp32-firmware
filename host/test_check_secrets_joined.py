# Joined-credential regression tests for check-secrets.sh. Synthetic values only.
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_check_secrets import GuardFixture, NON_UTC_TZ  # noqa: E402

NL = chr(10)
DQ = chr(34)
REAL = 'Zk01Rt71Qw9'
REAL2 = 'Qm58Lp20Vn3'
PW = 'password'
TK = 'token'
AK = 'api_key'
PH_PW = 'changeme'
PH_AK = 'your-api-key-here'
PH_TK = 'your-token-here'
PH_AK_LONG = 'changeme12345678'
URL = 'https://gw.example/api?'


def kv(key, value):
    return key + '=' + value


def url_line(query):
    return 'url = ' + DQ + URL + query + DQ


BLOCKED = [
    ('amp-placeholder-first', kv(PW, PH_PW) + '&' + kv(TK, REAL), [REAL]),
    ('amp-real-first', kv(TK, REAL) + '&' + kv(PW, PH_PW), [REAL]),
    ('url-placeholder-first', url_line(kv(PW, PH_PW) + '&' + kv(TK, REAL)), [REAL]),
    ('url-real-first', url_line(kv(TK, REAL) + '&' + kv(PW, PH_PW)), [REAL]),
    ('colon-placeholder-first', kv(PW, PH_PW) + ':' + kv(TK, REAL), [REAL]),
    ('colon-real-first', kv(TK, REAL) + ':' + kv(PW, PH_PW), [REAL]),
    ('space-placeholder-first', kv(AK, PH_AK_LONG) + ' ' + kv(PW, REAL), [REAL]),
    ('space-real-first', kv(PW, REAL) + ' ' + kv(AK, PH_AK_LONG), [REAL]),
    ('amp-two-real-after-placeholder', kv(PW, PH_PW) + '&' + kv(TK, REAL2), [REAL2]),
]

ALLOWED = [
    ('placeholder-then-lang', kv(PW, PH_PW) + '&lang=en'),
    ('url-all-placeholders', url_line(kv(PW, PH_PW) + '&' + kv(TK, PH_TK))),
    ('url-all-placeholders-reversed', url_line(kv(TK, PH_TK) + '&' + kv(PW, PH_PW))),
    ('single-placeholder', kv(AK, PH_AK)),
]


class CheckSecretsJoinedTest(unittest.TestCase):
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

    def assert_ok(self, proc):
        out = (proc.stdout or '') + (proc.stderr or '')
        self.assertEqual(proc.returncode, 0, out)
        self.assertNotIn('BLOCKED', out)

    def hook(self, line):
        self.fx.clear_index()
        self.fx.stage('conf.txt', line + NL)
        return self.fx.run([], cwd=self.fx.repo, tz='UTC')

    def tree(self, line):
        self._n += 1
        d = self.fx.tree('t%d' % self._n, {'sub/conf.txt': line + NL})
        return self.fx.run(['--scan-tree', d], cwd=self.fx.nongit, tz=NON_UTC_TZ)

    def test_hook_blocks_joined_credentials(self):
        for name, line, secrets in BLOCKED:
            with self.subTest(name=name):
                self.assert_leak(self.hook(line), secrets)

    def test_scan_tree_blocks_joined_credentials(self):
        for name, line, secrets in BLOCKED:
            with self.subTest(name=name):
                self.assert_leak(self.tree(line), secrets)

    def test_hook_allows_placeholder_only_lines(self):
        for name, line in ALLOWED:
            with self.subTest(name=name):
                self.assert_ok(self.hook(line))

    def test_scan_tree_allows_placeholder_only_lines(self):
        for name, line in ALLOWED:
            with self.subTest(name=name):
                self.assert_ok(self.tree(line))


if __name__ == '__main__':
    unittest.main()

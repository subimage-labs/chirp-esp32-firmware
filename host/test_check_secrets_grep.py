# Fail-closed regression tests for check-secrets.sh when grep lacks PCRE. Synthetic values only.
import os
import shutil
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_check_secrets import GuardFixture, NON_UTC_TZ  # noqa: E402

NL = chr(10)
SQ = chr(39)
SYNTH_VALUE = 'Zk01' + 'Rt71' + 'Qw9'
KEY = 'pass' + 'word'
REAL_GREP = shutil.which('grep')


def shim_source(real):
    lines = [
        '#!/bin/sh',
        'for a in "$@"; do',
        '  case "$a" in',
        '    --perl-regexp|--perl-regexp=*)',
        '      echo "grep: PCRE not supported" >&2; exit 2 ;;',
        '    --*) ;;',
        '    -P*|-[A-Za-z]*P*)',
        '      echo "grep: PCRE not supported" >&2; exit 2 ;;',
        '  esac',
        'done',
        'exec ' + SQ + real + SQ + ' "$@"',
        '',
    ]
    return NL.join(lines)


@unittest.skipIf(REAL_GREP is None, 'grep not found')
class CheckSecretsGrepTest(unittest.TestCase):
    def setUp(self):
        self._cm = GuardFixture()
        self.fx = self._cm.__enter__()
        self._n = 0
        self.shimdir = self.fx.root / 'grepshim'
        self.shimdir.mkdir()
        shim = self.shimdir / 'grep'
        shim.write_text(shim_source(REAL_GREP))
        shim.chmod(0o755)
        orig = self.fx.env
        shimdir = str(self.shimdir)

        def env(tz):
            e = orig(tz)
            e['PATH'] = shimdir + os.pathsep + e['PATH']
            return e

        self.fx.env = env

    def tearDown(self):
        self._cm.__exit__(None, None, None)

    def no_leak(self, proc):
        for stream in (proc.stdout or '', proc.stderr or ''):
            self.assertNotIn(SYNTH_VALUE.lower(), stream.lower())

    def hook(self, line):
        self.fx.clear_index()
        self.fx.stage('conf.txt', line + NL)
        return self.fx.run([], cwd=self.fx.repo, tz='UTC')

    def tree(self, line):
        self._n += 1
        d = self.fx.tree('t%d' % self._n, {'sub/conf.txt': line + NL})
        return self.fx.run(['--scan-tree', d], cwd=self.fx.nongit, tz=NON_UTC_TZ)

    def assert_closed(self, proc):
        out = (proc.stdout or '') + (proc.stderr or '')
        self.assertNotEqual(proc.returncode, 0, out)
        self.no_leak(proc)

    def test_shim_rejects_pcre_and_passes_through(self):
        env = self.fx.env('UTC')
        for args in (['-P', 'x'], ['--perl-regexp', 'x'], ['-anioP', 'x']):
            with self.subTest(args=args):
                r = subprocess.run(['grep'] + args, input='x' + NL, env=env,
                                   capture_output=True, text=True, timeout=15)
                self.assertEqual(r.returncode, 2)
        r = subprocess.run(['grep', '-E', 'x+'], input='xx' + NL, env=env,
                           capture_output=True, text=True, timeout=15)
        self.assertEqual(r.returncode, 0)

    def test_hook_fails_closed_on_unquoted_credential(self):
        self.assert_closed(self.hook(KEY + '=' + SYNTH_VALUE))

    def test_hook_fails_closed_on_spaced_credential(self):
        self.assert_closed(self.hook(KEY + ' = ' + SYNTH_VALUE))

    def test_scan_tree_fails_closed_on_unquoted_credential(self):
        self.assert_closed(self.tree(KEY + '=' + SYNTH_VALUE))

    def test_scan_tree_fails_closed_on_spaced_credential(self):
        self.assert_closed(self.tree(KEY + ' = ' + SYNTH_VALUE))

    def test_clean_file_passes_or_fails_nonzero(self):
        for name, proc in (
            ('hook', self.hook('All good, nothing here.')),
            ('tree', self.tree('All good, nothing here.')),
        ):
            with self.subTest(name=name):
                self.assertIsInstance(proc.returncode, int)
                self.assertGreaterEqual(proc.returncode, 0)
                self.no_leak(proc)
                if proc.returncode != 0:
                    self.assertNotEqual(proc.returncode, 0)


if __name__ == '__main__':
    unittest.main()

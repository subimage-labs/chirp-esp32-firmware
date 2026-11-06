# Privacy test: running build_paths.py must not print absolute home/project paths,
# and must still remap both through -ffile-prefix-map flags.
import contextlib
import io
import os
import re
import runpy
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


def locate_script():
    override = os.environ.get('BUILD_PATHS_SCRIPT')
    if override:
        return Path(override)
    return Path(__file__).resolve().parents[2] / 'build_paths.py'


class Anything(object):
    # Permissive stand-in for SCons nodes, builders and unknown helpers.
    def __getattr__(self, name):
        return Anything()

    def __call__(self, *args, **kwargs):
        return Anything()

    def __str__(self):
        return ''

    def __iter__(self):
        return iter(())

    def __bool__(self):
        return False


class FakeEnv(dict):
    def __init__(self, project, home):
        super().__init__()
        build = str(Path(project) / '.pio' / 'build' / 'synthenv')
        self.update(
            {
                'PROJECT_DIR': str(project),
                'PROJECT_SRC_DIR': str(Path(project) / 'src'),
                'PROJECT_BUILD_DIR': str(Path(project) / '.pio' / 'build'),
                'BUILD_DIR': build,
                'PROJECT_CORE_DIR': str(Path(home) / '.platformio'),
                'PROJECT_PACKAGES_DIR': str(Path(home) / '.platformio' / 'packages'),
                'PIOENV': 'synthenv',
                'PROGNAME': 'firmware',
                'CPPPATH': [],
                'CCFLAGS': [],
                'CFLAGS': [],
                'CXXFLAGS': [],
                'LINKFLAGS': [],
                'BUILD_FLAGS': [],
                'CPPDEFINES': [],
            }
        )

    def __missing__(self, key):
        return ''

    def _extend(self, kwargs):
        for key, value in kwargs.items():
            current = self.setdefault(key, [])
            if not isinstance(current, list):
                continue
            if isinstance(value, (list, tuple)):
                current.extend(value)
            else:
                current.append(value)

    def Append(self, **kwargs):
        self._extend(kwargs)

    AppendUnique = Append
    Prepend = Append
    PrependUnique = Append

    def subst(self, text, *args, **kwargs):
        # Expand $VAR and ${VAR} from the fake build environment, as SCons does.
        def expand(match):
            value = self.get(match.group(1) or match.group(2), '')
            return value if isinstance(value, str) else ''

        return re.sub(r'\$(?:\{(\w+)\}|(\w+))', expand, text)

    def __getattr__(self, name):
        if name.startswith('__'):
            raise AttributeError(name)
        return Anything()


class BuildPathsPrivacyTest(unittest.TestCase):
    def test_script_maps_build_paths(self):
        script = locate_script()
        self.assertTrue(script.is_file(), 'build_paths.py not found: %s' % script)
        with tempfile.TemporaryDirectory(prefix='bp-') as td:
            home = Path(td) / 'synthhome-user'
            project = home / 'Work' / 'synthproj'
            (project / 'src').mkdir(parents=True)
            (home / '.platformio').mkdir()
            env = FakeEnv(project, home)
            init = {
                'Import': lambda *a, **k: None,
                'env': env,
                'projenv': env,
                'DefaultEnvironment': lambda *a, **k: env,
                'Exit': sys.exit,
            }
            out, err = io.StringIO(), io.StringIO()
            old_cwd = os.getcwd()
            patched_env = {'HOME': str(home), 'USERPROFILE': str(home), 'PWD': str(project)}
            try:
                os.chdir(str(project))
                with mock.patch.dict(os.environ, patched_env), contextlib.redirect_stdout(
                    out
                ), contextlib.redirect_stderr(err):
                    runpy.run_path(str(script), init_globals=init, run_name='__main__')
            finally:
                os.chdir(old_cwd)
            expected_flags = [
                '-ffile-prefix-map=%s=chirp' % os.path.normpath(str(project)),
                '-ffile-prefix-map=%s=~' % os.path.normpath(str(home)),
            ]
            self.assertEqual(env['CCFLAGS'], expected_flags)


if __name__ == '__main__':
    unittest.main()

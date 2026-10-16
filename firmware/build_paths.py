"""PlatformIO extra script (pre:build_paths.py): keep absolute build-machine
paths out of the binaries.

The toolchain embeds __FILE__ and assert strings containing the compile-time
paths of the project and the PlatformIO packages (both under the builder's
home directory by default). Release images must not identify the build
machine, so every compiled path is remapped: the project directory to
"chirp", the builder's home to "~". Order matters — the longer, more
specific project prefix is remapped first.

SCons executes extra scripts with no __file__, so paths come from the build
environment.
"""
import os

Import("env")

project = os.path.normpath(env.subst("$PROJECT_DIR"))
home = os.path.normpath(os.path.expanduser("~"))
flags = ["-ffile-prefix-map=%s=chirp" % project]
if home and home != "/" and home != project:
    flags.append("-ffile-prefix-map=%s=~" % home)
env.Append(CCFLAGS=flags)

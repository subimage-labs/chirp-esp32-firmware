"""PlatformIO extra script (pre:version.py): stamp FW_VERSION into the build.

Order: the FW_VERSION environment variable (builds from a copied tree outside
git, e.g. a /tmp build dir), else `git describe --tags --always --dirty` from
the project directory (the firmware/ checkout), else "unknown". The value is
written to fw_version.h in the build directory — string-valued -D defines
lose their quotes somewhere between CPPDEFINES and the compiler, and ota.cpp
embeds this string in the image, so a header is the reliable vehicle.

The version ends up in the image: ota.cpp logs it at boot and embeds a
"chirp_fw_version=" record that host/publish-firmware.py reads from the
binary. publish-firmware.py refuses an image stamped "unknown" unless
--version is passed.

SCons executes extra scripts with no __file__, so paths come from the build
environment.
"""

import os
import subprocess

Import("env")


def _git_version(project_dir):
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=project_dir, stderr=subprocess.DEVNULL).decode().strip()
        return v or "unknown"
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


version = os.environ.get("FW_VERSION") or _git_version(env.subst("$PROJECT_DIR"))
build_dir = env.subst("$BUILD_DIR")
if not os.path.isdir(build_dir):
    os.makedirs(build_dir)
header_path = os.path.join(build_dir, "fw_version.h")
stamp = '#define FW_VERSION "%s"\n' % version
previous = None
if os.path.exists(header_path):
    with open(header_path) as fh:
        previous = fh.read()
rewritten = previous != stamp
if rewritten:
    with open(header_path, "w") as fh:
        fh.write(stamp)
env.Append(CPPPATH=[build_dir])
# SCons does not rescan the generated header, and a Depends on the object by
# path does not bind in PlatformIO: a new commit shipped the old stamp. So
# when the stamp changes, drop the object and let the normal decider rebuild
# it (ota.cpp embeds the version in the image).
if rewritten:
    obj = os.path.abspath(os.path.join(build_dir, "src", "ota.cpp.o"))
    if os.path.exists(obj):
        os.remove(obj)
print("FW_VERSION %s" % version)

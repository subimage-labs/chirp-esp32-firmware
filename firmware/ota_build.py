"""PlatformIO extra script (pre:ota_build.py): stamp the OTA build number and
the update-signing public key into the build.

Writes fw_ota.h in the build directory:
  FW_BUILD / FW_BUILD_STR   this build's number: unix seconds at build time.
                            Devices install an update only when its build is
                            higher than any they have run or recorded.
  OTA_PUBKEY_PEM            firmware/ota_pub.pem as a C string, only when the
                            file exists. Without it the image still builds,
                            logs "[OTA] no signing key in this build; updates
                            off" and never installs anything over WiFi.

ota_pub.pem is made by `host/publish-firmware.py --init-key` and is gitignored.
The key text is normalised (base64 body re-wrapped at 64 columns) so the image
carries exactly the PEM host/publish-firmware.py looks for.

SCons executes extra scripts with no __file__, so paths come from the build
environment.
"""

import os
import re
import time

Import("env")

PEM_RE = re.compile(
    r"-----BEGIN PUBLIC KEY-----\s*([A-Za-z0-9+/=\s]+?)\s*-----END PUBLIC KEY-----")


def _normalised_pem(path):
    """The key file as 64-column PEM text, or None when absent/unusable."""
    try:
        with open(path) as fh:
            text = fh.read()
    except OSError:
        return None
    m = PEM_RE.search(text)
    if not m:
        print("ota_build: %s holds no PEM public key; OTA is off" % path)
        return None
    body = "".join(m.group(1).split())
    lines = [body[i:i + 64] for i in range(0, len(body), 64)]
    return "-----BEGIN PUBLIC KEY-----\n%s\n-----END PUBLIC KEY-----\n" % "\n".join(lines)


build = int(time.time())
pem = _normalised_pem(os.path.join(env.subst("$PROJECT_DIR"), "ota_pub.pem"))
build_dir = env.subst("$BUILD_DIR")
if not os.path.isdir(build_dir):
    os.makedirs(build_dir)
header = '#define FW_BUILD %du\n#define FW_BUILD_STR "%d"\n' % (build, build)
if pem:
    header += '#define OTA_PUBKEY_PEM "%s"\n' % pem.replace("\n", "\\n")
header_path = os.path.join(build_dir, "fw_ota.h")
previous = None
if os.path.exists(header_path):
    with open(header_path) as fh:
        previous = fh.read()
rewritten = previous != header
if rewritten:
    with open(header_path, "w") as fh:
        fh.write(header)
env.Append(CPPPATH=[build_dir])
# SCons does not rescan the generated header, and a Depends on the object by
# path does not bind in PlatformIO: builds shipped a stale FW_BUILD. FW_BUILD
# changes every build, so ota.cpp recompiles every build and the image always
# carries this build's number (and key).
if rewritten:
    obj = os.path.abspath(os.path.join(build_dir, "src", "ota.cpp.o"))
    if os.path.exists(obj):
        os.remove(obj)
print("FW_BUILD %d, OTA signing key %s" % (build, "embedded" if pem else "absent"))

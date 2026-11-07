#!/usr/bin/env bash
# Assemble the project web site.
#
#   site/build.sh OUT_DIR                      page + media + installer page, no images
#   site/build.sh OUT_DIR RELEASE_DIR TAG      also the release images in OUT_DIR/install/
#
# RELEASE_DIR holds the files of one release (the four flash images and
# SHA256SUMS). Every image that flasher/manifest.json lists is checked against
# SHA256SUMS, copied beside the installer page, and the manifest version is
# set to TAG. The manifest keeps its parts separate: a merged image would pad
# the NVS range and wipe the device settings.
#
# Without RELEASE_DIR the installer page has no images and cannot flash: use
# that form only to look at the site.
set -eu

[ "$#" -eq 1 ] || [ "$#" -eq 3 ] || {
    echo "usage: $0 OUT_DIR [RELEASE_DIR TAG]" >&2
    exit 2
}
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$1"

rm -rf "$out"
mkdir -p "$out/media" "$out/install"

# The page itself (everything in site/ except this script).
tar -C "$repo/site" --exclude=build.sh -cf - . | tar -C "$out" -xf -
cp "$repo"/docs/media/* "$out/media/"
cp "$repo/docs/chirp-logo.png" "$out/"

# The installer page and ESP Web Tools.
cp "$repo/flasher/index.html" "$out/install/"
cp -r "$repo/flasher/esp-web-tools" "$out/install/"
cp "$repo/flasher/manifest.json" "$out/install/"

if [ "$#" -eq 3 ]; then
    python3 - "$repo/flasher/manifest.json" "$2" "$3" "$out/install" <<'PY'
import hashlib
import json
import os
import shutil
import sys

manifest_src, release_dir, tag, install_dir = sys.argv[1:5]

with open(manifest_src) as f:
    manifest = json.load(f)

sums = {}
with open(os.path.join(release_dir, "SHA256SUMS")) as f:
    for line in f:
        line = line.rstrip("\n")
        if not line:
            continue
        digest, _, name = line.partition("  ")
        sums[name.lstrip("*")] = digest

paths = [part["path"] for build in manifest["builds"] for part in build["parts"]]
if len(set(paths)) != len(paths):
    sys.exit("manifest lists an image twice")

for path in paths:
    if path not in sums:
        sys.exit("SHA256SUMS has no entry for " + path)
    src = os.path.join(release_dir, path)
    with open(src, "rb") as f:
        actual = hashlib.sha256(f.read()).hexdigest()
    if actual != sums[path]:
        sys.exit("sha256 mismatch for " + path)
    shutil.copyfile(src, os.path.join(install_dir, path))

# The copies must match too.
for path in paths:
    with open(os.path.join(install_dir, path), "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != sums[path]:
            sys.exit("copied image differs for " + path)

manifest["version"] = tag
with open(os.path.join(install_dir, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
print("installer: %d images, version %s" % (len(paths), tag))
PY
fi

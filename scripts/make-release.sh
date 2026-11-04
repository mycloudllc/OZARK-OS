#!/usr/bin/env bash
# Makes an update release from the last build: the signed update file and the
# update feed that radios check (Settings > Update > Update address).
#
#   ./scripts/make-release.sh [--notes "What's new"] [OUTDIR]
#
# Writes to OUTDIR (default: release/<version>-<build>/):
#   ozark-os-<version>-<build>.swu   the update (from ./scripts/build.sh)
#   ozark-update.json                the feed: version, build, file, checksum, notes
#   ozark-os-<version>-<build>.img.bz2 + .bmap   the SD card image, for new cards
#
# Upload both the .swu and ozark-update.json to the same place. With GitHub
# Releases, attach them to a release; radios then use the address
#   https://github.com/OWNER/REPO/releases/latest/download/ozark-update.json
# which always points at the newest release. Any web server works too.
#
# A radio offers an update when the feed's version is higher than its own, or
# the same version with a newer build. Raise DISTRO_VERSION in
# meta-ozark/conf/distro/ozark.conf for each release.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
machine="raspberrypi4-64"
deploy="$ROOT/build/build/tmp/deploy/images/$machine"
notes=""
out=""

while [ $# -gt 0 ]; do
    case "$1" in
        --notes) notes="$2"; shift ;;
        -h|--help) sed -n '2,/^$/{s/^# \{0,1\}//;p}' "$0"; exit 0 ;;
        *) out="$1" ;;
    esac
    shift
done

swu="$deploy/ozark-swu-$machine.rootfs.swu"
[ -f "$swu" ] || { echo "No update file at $swu: run ./scripts/build.sh first" >&2; exit 1; }

version=$(sed -n 's/^DISTRO_VERSION = "\(.*\)"/\1/p' "$ROOT/meta-ozark/conf/distro/ozark.conf")
build=$(sed -n 's/^BUILD=//p' "$deploy/ozark-image-$machine.rootfs.build")
name="ozark-os-$version-$build"
out="${out:-$ROOT/release/$version-$build}"
mkdir -p "$out"

cp "$(readlink -f "$swu")" "$out/$name.swu"
for f in wic.bz2 wic.bmap; do
    src="$deploy/ozark-image-$machine.rootfs.$f"
    [ -f "$src" ] && cp "$(readlink -f "$src")" "$out/$name.img.${f#wic.}"
done

python3 - "$out/ozark-update.json" "$version" "$build" "$name.swu" "$out/$name.swu" "$machine" "$notes" <<'EOF'
import hashlib, json, os, sys
path, version, build, url, swu, machine, notes = sys.argv[1:]
sha = hashlib.sha256()
with open(swu, "rb") as f:
    for chunk in iter(lambda: f.read(1 << 20), b""):
        sha.update(chunk)
feed = {
    "version": version,
    "build": build,
    "url": url,
    "sha256": sha.hexdigest(),
    "size": os.path.getsize(swu),
    "machine": machine,
    "notes": notes,
}
with open(path, "w") as f:
    json.dump(feed, f, indent=2)
    f.write("\n")
EOF

echo "==> Release $version (build $build) in $out:"
ls -lh "$out"
echo "    Upload $name.swu and ozark-update.json side by side."

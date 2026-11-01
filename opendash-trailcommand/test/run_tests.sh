#!/bin/bash
# Builds the plugin against real OpenDash headers and drives it like a user against a live hub.
#   test/run_tests.sh <dash-checkout> <trailcommand-hub-binary> <hub-web-dir>
# Needs Qt 5 development packages (qtbase5-dev, libqt5websockets5-dev), cmake and a C++ compiler.
set -e
DASH=${1:?path to a checkout of github.com/openDsh/dash}
HUB=${2:?path to the trailcommand hub binary}
WEB=${3:?path to the hub web directory}
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HERE/build" && cd "$HERE/build"
cmake .. -DDASH_INCLUDE="$DASH/include" -DCMAKE_BUILD_TYPE=Release >/dev/null
make -j"$(nproc)"
export DASH_DIR="$DASH"
DB=$(mktemp -u /tmp/tc_hub_XXXXXX.db)
# the hub must be on port 8080, which the network scan tries first
"$HUB" -l http://0.0.0.0:8080 -w "$WEB" -d "$DB" --demo-history --sim-wifi --sim-lte >/dev/null 2>&1 &
HP=$!
trap 'kill $HP 2>/dev/null; rm -f "$DB"*' EXIT
sleep 1.5
for theme in light dark; do
  echo "== $theme mode"
  QT_QPA_PLATFORM=offscreen ./tctest 8080 $theme 2>&1 | grep -v "XDG_RUNTIME\|QStandardPaths\|propagateSizeHints"
done
echo "Screenshots are in $HERE/build/shots"

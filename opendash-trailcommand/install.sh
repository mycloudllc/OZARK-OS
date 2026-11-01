#!/bin/sh
# Copies the TrailCommand plugin into a checkout of OpenDash. Then rebuild Dash as usual.
#   ./install.sh /path/to/dash
set -e
DASH=${1:?usage: ./install.sh /path/to/dash}
[ -d "$DASH/plugins/vehicle" ] || { echo "$DASH does not look like an OpenDash checkout (no plugins/vehicle)"; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$DASH/plugins/vehicle/trailcommand"
cp -r "$HERE/plugins/vehicle/trailcommand" "$DASH/plugins/vehicle/"
echo "Installed to $DASH/plugins/vehicle/trailcommand"
echo "Next: rebuild Dash (see its README). Then, in Dash, open the Vehicle page, tap its settings button,"
echo "choose the plugin 'trailcommand' and press load."

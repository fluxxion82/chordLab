#!/usr/bin/env bash
# Builds the render harness (README.md) into <out dir>/viewharness: swiftc over the editor's views (ChordLab/UI, minus
# the view controller, which needs iPlug2), Core (ChordLab/UI/Core) and this folder's sources. Nothing else is written.
#
#   ChordLab/Tests/viewharness/build.sh <out dir>
set -euo pipefail
if [ $# -ne 1 ]; then
    echo "usage: $0 <out dir>" >&2
    exit 2
fi
here="$(cd "$(dirname "$0")" && pwd -P)"
ui="$(cd "$here/../../UI" && pwd -P)"
out="$1"
[ -d "$out" ] || { echo "build.sh: '$out' is not a directory" >&2; exit 2; }
sources=()
for f in "$ui"/*.swift; do
    [ "$(basename "$f")" = "ChordLabViewController.swift" ] && continue
    sources+=("$f")
done
sources+=("$ui"/Core/*.swift "$here"/*.swift)
# The module cache goes into the out dir, not ~/Library/Caches or $TMPDIR.
exec swiftc -parse-as-library -swift-version 5 -target "$(uname -m)-apple-macos15.0" -Onone \
    -module-name ViewHarness -module-cache-path "$out/ModuleCache" \
    -o "$out/viewharness" "${sources[@]}"

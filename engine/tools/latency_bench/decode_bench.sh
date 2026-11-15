#!/usr/bin/env bash
# Times the Swift decode of a snapshot latency_bench dumped (tracks design D6), with the real decoder, optimised.
#   engine/tools/latency_bench/decode_bench.sh <snapshot.json> [runs]
# Builds into engine/build-bench/ (gitignored) next to the C++ bench.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd -P)"
OUT="$ROOT/engine/build-bench"
mkdir -p "$OUT"
# The whole of UI/Core: Snapshot.swift uses its neighbours (RelationMode, ...), as the Swift package builds it.
swiftc -O -o "$OUT/decode_bench" "$ROOT"/ChordLab/UI/Core/*.swift "$ROOT/engine/tools/latency_bench/main.swift"
"$OUT/decode_bench" "$@"

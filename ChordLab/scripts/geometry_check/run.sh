#!/bin/sh
# Compiles the UI's pure (Foundation-only) logic files with main.swift under plain swiftc and runs the checks
# (no XCTest target): SequenceGeometry, HarmonyText, ChordSymbol and the Snapshot structs they read.
cd "$(dirname "$0")" && swiftc -O -o "${TMPDIR:-/tmp}/chordlab_geometry_check" \
  ../../UI/SequenceGeometry.swift ../../UI/HarmonyText.swift ../../UI/ChordSymbol.swift ../../UI/Snapshot.swift main.swift \
  && "${TMPDIR:-/tmp}/chordlab_geometry_check"

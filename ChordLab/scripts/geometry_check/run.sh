#!/bin/sh
# Compiles UI/SequenceGeometry.swift with main.swift under plain swiftc and runs the checks (no XCTest target).
cd "$(dirname "$0")" && swiftc -O -o "${TMPDIR:-/tmp}/chordlab_geometry_check" ../../UI/SequenceGeometry.swift main.swift && "${TMPDIR:-/tmp}/chordlab_geometry_check"

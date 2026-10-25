# ChordLab

ChordLab is a chord-progression plug-in for macOS in the style of Klimper 2: pick a key, audition
chords from a palette, drag them into a sequence, and let the plug-in play the progression as MIDI in
time with the host or drop it onto a track as a clip. It is a VST3 instrument that outputs silence and
MIDI, built for Ableton Live. Under the hood it is an iPlug2 Objective-C++ shell (`ChordLab/`), a
host-independent C++17 engine that owns all the theory (`engine/`), and a SwiftUI editor
(`ChordLab/UI/`) that draws what the engine's JSON snapshot tells it and computes no theory of its own.

## Features

Phase 1, the basics:

- A project key (12 tonics × 9 modes: the seven church modes, harmonic minor and melodic minor) and a
  separately browsed key, stepped along the circle of fifths or switched to a parallel mode.
- A palette of the browsed key: 7 scale degrees × up to 5 tiers (power chord, triad, sixth/seventh,
  seventh/add9, colour chords). Chords with notes outside the project key are drawn as rings.
- Press a cell to hear it; drag it into the sequence bar to append it.
- A sequence bar of chord blocks over a beat ruler: reorder, resize (quarter-beat snap, 0.25–64 beats),
  select, delete; undo and redo.
- Playback that follows the host transport sample-accurately, with an optional loop, a playhead, and a
  Panic button.
- MIDI drag-out of the progression as a `.mid` clip at the host tempo.
- The document (progression, sections, voicings, keys, follow, loop, relation mode) and the editor
  window size are saved with the host project.

Phase 2, explainable harmony:

- A harmonic function (Tonic / Predominant / Dominant / Other) and a 0–100 tension score on every
  chord, shown as a coloured dot and a tension bar.
- "Why" popovers on sequence blocks: how the chord relates to the one before it (root motion, common
  tones, voice-leading cost, D→T resolution, the parallel mode a chord is borrowed from).
- A suggestions strip: up to 8 ranked next chords for the selected (else the last) chord, each with its
  reasons.
- Chord relations in the palette: every cell is sized and shaded by its relatedness to the selected
  (else the last) chord, under a chooser of relation modes (common tones, voice leading, function, or
  the blended suggestion score). The home row follows.
- Voicing: per-chord inversion and octave (−1/0/+1), and an Auto voice-lead mode that picks them for
  the smoothest motion from the previous chord.
- Sections: named, coloured spans over the progression (8 colours), each exportable as its own clip.
- A home-key compare row: while another key is browsed, the project key's seven triads stay available
  under the palette.

Phase 3, so far:

- Progression presets: 29 well-known progressions (pop, rock, blues, jazz, folk, EDM, R&B, film, gospel,
  classical) stored as roman numerals, transposed into the project key, loaded in one click as a new
  progression or appended to the current one (one undo step, sectioned and named after the preset).
- A tension curve over the sequence bar, and a browsed-mode picker for parallel-mode browsing.
- An Applied view of the palette: the browsed key's secondary dominants (V7/x) and leading-tone sevenths
  (vii°7/x), each above the chord it leads to, previewable and draggable like palette cells.

## Requirements

- macOS 15 or later (the deployment target; the UI uses current SwiftUI APIs).
- Xcode with the command line tools, and CMake 3.20+ for the engine tests.
- An [iPlug2](https://github.com/iPlug2/iPlug2) checkout **next to this repository**, named `iPlug2`:

  ```
  parent/
    iPlug2/
    musicTheoryPlugin/      <- this repository
  ```

  `ChordLab/config/ChordLab-mac.xcconfig` sets `IPLUG2_ROOT = ../../../iPlug2`, relative to
  `ChordLab/projects`, and includes iPlug2's `common-mac.xcconfig` from there. Follow iPlug2's own
  instructions for fetching the VST3 SDK into that checkout.

## Build

Engine tests (no Xcode or iPlug2 needed):

```
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Debug && cmake --build engine/build -j8 && (cd engine/build && ctest --output-on-failure)
```

See `engine/README.md`, and "Testing" below for the other suites and the one-shot `scripts/check.sh`.

The plug-in, from the repository root:

```
xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj -scheme macOS-VST3 -configuration Debug build
```

The build installs the bundle to `~/Library/Audio/Plug-Ins/VST3/ChordLab.vst3`; rescan plug-ins in Live
(or restart it) to pick up a new build.

The standalone app, handy for working on the UI without a host:

```
xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj -scheme macOS-APP -configuration Debug \
  CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY=- DEVELOPMENT_TEAM= PROVISIONING_PROFILE_SPECIFIER= CODE_SIGN_ENTITLEMENTS= build
```

The signing overrides are needed because the iPlug2 project template has its own development team ID
baked into the APP target; they switch it to ad-hoc signing with no entitlements. The app lands in
`~/Applications/ChordLab.app`. The same overrides are harmless on the VST3 scheme.

## Testing

One script runs every suite; with no flags it runs the engine's Debug build and tests only:

```
scripts/check.sh            # engine (Debug) build + ctest
scripts/check.sh --all      # + UBSan, ASan and TSan builds, Swift tests, bridge tests, both Xcode schemes
```

Flags combine (`--ubsan`, `--asan`, `--tsan`, `--swift`, `--bridge`, `--plugin`). The script stops at
the first failure and prints a PASS/FAIL table. Every sanitizer step runs under a hard timeout because
older Apple clang releases hung under ASan and TSan (see "Status").

The suites, each runnable on its own:

| Suite | What it covers | Command |
|---|---|---|
| `engine_tests` (doctest) | theory, document/undo, compiler, scheduler, SMF, JSON API, golden fixtures, mutation fuzz of the JSON readers | `cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Debug && cmake --build engine/build -j8 && (cd engine/build && ctest --output-on-failure)` |
| `engine_rt_alloc_test` | no allocation on the audio-thread surface | same ctest run |
| `ChordLabUICoreTests` (XCTest) | the UI's pure logic in `ChordLab/UI/Core/`: geometry, harmony text, symbols, key names, snapshot decoding | `cd ChordLab && swift test` |
| `bridge_tests` (doctest) | the plug-in shell's pure helpers in `ChordLab/bridge/`: message validation, preview and section-export parsing, export file naming | `cmake -S ChordLab/bridge/tests -B ChordLab/build-bridge-tests && cmake --build ChordLab/build-bridge-tests -j8 && (cd ChordLab/build-bridge-tests && ctest --output-on-failure)` |

**JSON fixtures.** `engine/tests/fixtures/` holds the engine's own `snapshotJson`/`stateJson` output for
a few documents (empty, a four-chord progression with sections and a manual voicing, the same while
browsing another key) plus hand-written Phase 1-shaped files. The engine compares its output to them
byte for byte, and the Swift tests decode them, so the two sides cannot drift apart silently. When the
contract changes on purpose, regenerate the generated files with
`CHORDLAB_UPDATE_FIXTURES=1 engine/build/engine_tests -tc="*golden*"` and review the diff; the legacy
files are maintained by hand. The Swift fixture tests fail if the directory is missing;
`CHORDLAB_UI_SKIP_FIXTURES=1 swift test` skips them and `CHORDLAB_UI_FIXTURES_DIR=<dir>` points them
elsewhere.

**Fuzz.** `test_json_robustness.cpp` mutates valid commands and states with a fixed-seed generator and
checks that the readers never throw, that a rejected message changes nothing (document, counters,
selection, undo/redo history) and that an accepted one leaves the document well-formed. A failure
prints the iteration and the escaped input, so it reproduces exactly.

A new pure UI file belongs in `ChordLab/UI/Core/` so the package tests see it; a new pure plug-in helper
belongs in `ChordLab/bridge/` with a case in `ChordLab/bridge/tests/`.

## Documentation

- [docs/user-guide.md](docs/user-guide.md): setting ChordLab up in Ableton Live, every control in the
  window, chord relations, keyboard shortcuts, what is saved with the set, limits and troubleshooting.
- [docs/architecture.md](docs/architecture.md): how it works inside, for developers: layers, threads,
  the engine module map, the message protocol between engine and UI, persistence, MIDI export, the
  editor's structure, build layout and the test suites.
- [engine/README.md](engine/README.md): the engine's own notes (layout, JSON surface, error handling,
  real-time rules, threading contract); [engine/include/chordlab/json_api.h](engine/include/chordlab/json_api.h)
  is the engine ↔ UI contract.
- [docs/plans/](docs/plans/): the Phase 1, Phase 2 and chord-relations plans with their decision records.
- [docs/testing/](docs/testing/): the first-look test script for Ableton Live.

## Status

- **Live verification is pending.** The engine is covered by tests and both the VST3 and the standalone
  app build cleanly, but the window has not yet been checked end to end inside Live (routing, transport
  follow, drag-out, keyboard shortcuts). The user guide describes the intended behaviour.
- Sanitizers: with Apple clang 17 (Xcode 16) ASan deadlocked before `main()` and TSan hung; since
  Apple clang 21 (Xcode 27.1) UBSan, ASan and TSan all run the engine suite cleanly
  (`scripts/check.sh --ubsan --asan --tsan`, each step under a hard timeout).
- Not yet: piano roll, melody tracks, key detection (Phase 3); chords are contiguous; the ruler assumes
  4/4; one suggestion style (pop); macOS and VST3 only. The Xcode project still carries iPlug2's other
  targets (AU, AUv3, AAX, VST2, iOS) and Windows project files from the template; they are not
  maintained or tested.

## Repository layout

```
engine/                     C++17 core, no host or UI dependency (CMake + doctest); see engine/README.md
  include/chordlab/         public headers; json_api.h is the contract with the UI
  src/  tests/  tests/fixtures/  fixtures/  tools/  third_party/
ChordLab/                   the iPlug2 plug-in
  ChordLab.h / ChordLab.mm  plug-in shell: audio thread, scheduler hand-off, messages, state, MIDI export
  config.h                  plug-in identity and I/O (VST3 "Instrument|Tools", MIDI in and out)
  UI/                       SwiftUI editor (a file-system-synchronized Xcode group)
    Core/                   its pure logic (geometry, text, decoding), also built as the Swift package
  bridge/                   header-only C++ helpers the shell uses (message validation, export naming)
    tests/                  bridge_tests (doctest, standalone CMake project)
  Package.swift             Swift package: library ChordLabUICore (UI/Core) + Tests/ChordLabUICoreTests
  config/                   xcconfig files (IPLUG2_ROOT lives here)
  projects/                 Xcode projects (ChordLab-macOS.xcodeproj)
  resources/                Info.plists, icons, fonts
scripts/check.sh            runs every suite (see Testing)
docs/user-guide.md          how to set it up and use it
docs/architecture.md        how it works inside
docs/plans/                 the Phase 1, Phase 2 and chord-relations plans and decision records
docs/testing/               the first-look test script for Ableton Live
```

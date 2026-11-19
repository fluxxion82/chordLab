# Development

How to build ChordLab from source, run its tests, and find your way around the repository. How the pieces fit
together is in [architecture.md](architecture.md); how releases are made is in [releasing.md](releasing.md).

## Requirements

- macOS 15 or later (the deployment target; the UI uses current SwiftUI APIs).
- Xcode with the command line tools, and CMake 3.20 or later for the engine tests.
- An [iPlug2](https://github.com/iPlug2/iPlug2) checkout **next to this repository**, named `iPlug2`:

  ```
  parent/
    iPlug2/
    chordLab/               <- this repository
  ```

  `ChordLab/config/ChordLab-mac.xcconfig` sets `IPLUG2_ROOT = ../../../iPlug2`, relative to `ChordLab/projects`, and
  includes iPlug2's `common-mac.xcconfig` from there. Follow iPlug2's own instructions to fetch the VST3 SDK into
  that checkout.

The engine and its tests need neither Xcode nor iPlug2.

## Build

Engine and its tests:

```bash
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Debug && cmake --build engine/build -j8 && (cd engine/build && ctest --output-on-failure)
```

See [engine/README.md](../engine/README.md) for the engine's own layout and rules.

The plug-in and the app, from the repository root:

```bash
xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj -scheme macOS-VST3 -configuration Debug build
```

```bash
xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj -scheme macOS-AUv2 -configuration Debug build
```

```bash
xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj -scheme macOS-APP -configuration Debug build
```

| Scheme | Installs to |
|---|---|
| `macOS-VST3` | `~/Library/Audio/Plug-Ins/VST3/ChordLab.vst3` |
| `macOS-AUv2` | `~/Library/Audio/Plug-Ins/Components/ChordLab.component` |
| `macOS-APP` | `~/Applications/ChordLab.app` |

Every target is ad-hoc signed, so building needs no certificate. After a new VST3 build, rescan plug-ins in Live
(Settings → Plug-Ins) or restart it. `auval -strict -v aumi pozR Ster` validates the Audio Unit. The standalone app
is handy for working on the UI without a host.

These development copies carry the same identifiers as the release pkg, which installs to `/Library` and
`/Applications`. Keep one or the other installed, not both.

**Git worktrees.** `IPLUG2_ROOT` must still resolve from a worktree. For worktrees under `.worktrees/<name>`, put a
symlink to your iPlug2 checkout at `.worktrees/iPlug2`.

The Xcode project still carries iPlug2's template targets for AUv3, AAX, VST2 and iOS, and Windows project files.
They are not maintained or tested.

## Testing

One script runs every suite. With no flags it runs the engine's Debug build and tests only:

```bash
scripts/check.sh
```

```bash
scripts/check.sh --all
```

`--all` adds the UBSan, ASan and TSan builds, the Swift tests, the bridge tests, both Xcode schemes and the release
preflight. The flags also combine one by one: `--ubsan`, `--asan`, `--tsan`, `--swift`, `--bridge`, `--plugin`,
`--release` (the release script's preflight in report mode), `--shell` and `--views`. Only `--plugin` and `--all`
install anything (the VST3 into `~/Library/Audio/Plug-Ins/VST3`, the app into `~/Applications`). The script stops
at the first failure and prints a PASS/FAIL table.

ASan and TSan need Apple clang 21 (Xcode 27.1) or later; older releases hung under them, so every sanitizer step
runs under a hard timeout.

| Suite | What it covers | How to run it |
|---|---|---|
| `engine_tests` (doctest) | theory, document and undo, compiler, scheduler, MIDI files, JSON API, golden fixtures, mutation fuzz of the JSON readers | the engine command under [Build](#build) |
| `engine_rt_alloc_test` | no allocation on the audio-thread surface | same ctest run |
| `ChordLabUICoreTests` (XCTest) | the UI's pure logic in `ChordLab/UI/Core/`: geometry, harmony text, symbols, key names, snapshot decoding | `cd ChordLab && swift test` |
| `bridge_tests` (doctest) | the plug-in shell's pure helpers in `ChordLab/bridge/`: message validation, previews, export parsing and naming, link messages | `scripts/check.sh --bridge` |
| `vst3host` | the plug-in shell without a DAW: a command-line VST3 host loads a fresh `ChordLab.vst3` in-process and checks life cycle, state, playback against the engine, and two linked instances | `scripts/check.sh --shell` |
| `viewharness` | the editor's views, offscreen: renders `ContentView` in each state of the per-track-instrument feature at two window sizes and checks the frames | `scripts/check.sh --views` (needs the display awake) |

`--shell` and `--views` build into a scratch directory and install nothing; they can't be combined with
`--plugin` or `--all`. See [ChordLab/Tests/vst3host/README.md](../ChordLab/Tests/vst3host/README.md) and
[ChordLab/Tests/viewharness/README.md](../ChordLab/Tests/viewharness/README.md).

**JSON fixtures.** `engine/tests/fixtures/` holds the engine's own `snapshotJson`, `stateJson` and `linkStateJson`
output for a few documents, plus hand-written files in older shapes. The engine compares its output to them byte
for byte and the Swift tests decode them, so the two sides can't drift apart silently. When the contract changes
on purpose, regenerate the generated ones and review the diff:

```bash
CHORDLAB_UPDATE_FIXTURES=1 engine/build/engine_tests -tc="*golden*"
```

The older-shape files are maintained by hand. The Swift fixture tests fail if the directory is missing;
`CHORDLAB_UI_SKIP_FIXTURES=1 swift test` skips them and `CHORDLAB_UI_FIXTURES_DIR=<dir>` points them elsewhere.

**Fuzz.** `engine/tests/test_json_robustness.cpp` mutates valid commands and states with a fixed-seed generator.
It checks three things: the readers never throw; a rejected message changes nothing (document, counters,
selection, undo history); and an accepted one leaves the document well-formed. A failure prints the iteration and
the escaped input, so it reproduces exactly.

**Where new code goes.** Put a new pure UI file in `ChordLab/UI/Core/` so the package tests see it. Put a new pure
plug-in helper in `ChordLab/bridge/`, with a case in `ChordLab/bridge/tests/`. Logic that unit tests can reach is
preferred over logic that only a host can exercise.

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
  Tests/vst3host/           headless VST3 host test
  Tests/viewharness/        offscreen render harness for the editor
  Package.swift             Swift package: library ChordLabUICore (UI/Core) + Tests/ChordLabUICoreTests
  config/                   xcconfig files (IPLUG2_ROOT lives here)
  projects/                 Xcode projects (ChordLab-macOS.xcodeproj)
  resources/                Info.plists, icons, fonts
docs/                       user guide, tutorial, architecture, development and release notes
scripts/check.sh            runs every suite (see Testing)
scripts/release.sh          signs, notarizes and packages a release (see releasing.md)
scripts/release/resources/  installer panes
LICENSE                     GPL-3.0 text
THIRD_PARTY_NOTICES.txt     third-party notices, shipped in every bundle
```

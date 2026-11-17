# ChordLab

ChordLab is a chord-progression plug-in for macOS in the style of Klimper 2: pick a key, audition
chords from a palette, drag them into a sequence, and let the plug-in play the progression as MIDI in
time with the host or drop it onto a track as a clip. It ships as a VST3 instrument for Ableton Live, an
Audio Unit MIDI FX for Logic Pro and a standalone app; the plug-ins output MIDI and no audio (the VST3
carries a silent stereo bus because Live wants one). Under the hood it is an iPlug2 Objective-C++
shell (`ChordLab/`), a host-independent C++17 engine that owns all the theory (`engine/`), and a SwiftUI
editor (`ChordLab/UI/`) that draws what the engine's JSON snapshot tells it and computes no theory of its
own.

## Features

Phase 1, the basics:

- A project key (12 tonics × 9 modes: the seven church modes, harmonic minor and melodic minor) and a
  separately browsed key, stepped along the circle of fifths or switched to a parallel mode.
- A palette of the browsed key: 7 scale degrees × up to 5 tiers (power chord, triad, sixth/seventh,
  seventh/add9, colour chords), triads at the bottom and power chords on top, every row named, the cells
  sized to show every row when the window is tall enough. Chords with notes outside the project key are
  drawn as rings.
- Press a cell to hear it; drag it into the sequence bar to append it.
- A sequence bar of chord blocks over a beat ruler: reorder, resize (quarter-beat snap, 0.25–64 beats),
  select, delete; undo and redo.
- Keyboard: 1–8 append suggestions, Delete removes the selected chord, ⌘Z/⇧⌘Z undo and redo, whenever
  the plug-in window has the keyboard and you last clicked inside it.
- Playback that follows the host transport sample-accurately, with an optional loop, a playhead, and a
  Panic button.
- MIDI drag-out of the progression as a `.mid` clip at the host tempo.
- The document (progression, sections, voicings, keys, follow, loop, relation mode) and the editor
  window size (900 × 800 for a new instance, resizable down to 700 × 632) are saved with the host project.

Phase 2, explainable harmony:

- A harmonic function (Tonic / Predominant / Dominant / Other) and a 0–100 tension score on every
  chord, shown as a coloured dot and a tension bar.
- Hover tips, drawn inside the window so they never take a click: a palette cell's notes, function and
  tension; a suggestion's reasons; and the "why" on sequence blocks: how the chord relates to the one
  before it (root motion, common tones, voice-leading cost, D→T resolution, the parallel mode a chord is
  borrowed from).
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
- Feel: block, pulse, subdivided, offbeat, low-then-chord and Arpeggio rhythms; the Arpeggio has seventeen
  patterns (Live's Arpeggiator styles plus Alberti, Root first, Chord and a per-chord Random), a rate and a
  one- or two-octave range; strum and humanise; the document's meter.
- Sequence bar editing: ⇧/⌘ multi-select, ⌘C ⌘X ⌘V ⌘D ⌘A, a Length menu with a default length for new chords,
  and a Record toggle that turns palette presses into blocks as long as they were held.

Tracks, on `main` and not in the 1.0.0 release (see Status):

- Up to four Bass, Lead or Other tracks of notes over the progression, in a lane tab row under the sequence bar
  with mute and solo per lane (the chords included). They play with the chords on MIDI channel 1 and are saved
  with the set.
- A piano roll per track in place of the palette: draw, move, resize either end, ⌥-drag to duplicate, a velocity
  mode, rubber-band and ⇧ selection, a right-click menu, ⌘C ⌘X ⌘V ⌘D ⌘A and arrow-key nudges (with buttons for
  hosts that keep the arrows), grid and zoom, one undo history with the chords.
- Harmonic guides: each row shaded by what the pitch is over the chord playing there (root, chord tone, tension,
  avoid note), weighted by the track's role, with degree labels and an optional pitch snap to the chord's scale or
  tones.
- Notes follow their chords: moving, resizing or deleting a chord carries the notes that start in it, a shortened
  chord keeps the notes past its end silent until it is lengthened again, chord copy and duplicate carry their
  notes, and Transpose with key moves them.
- A bass generator with six patterns (Roots, Root pulse, Root–fifth, Octaves, Follow feel, Walking) for the
  selected chords or all of them, in one undo step.
- MIDI drag-out per lane (each tab's ♫), of every lane at once ("♫ All": one file, one clip per lane, muted lanes
  included), and of the open lane from the bottom control and the section handles.

Per-track instruments, on `main`, not in the 1.0.0 release (run in Ableton Live 12, Live Test 16; see Status):

- A second ChordLab in the same Live set can play one track of the main one (its chords, or a Bass, Lead or Other
  track), so each line drives its own instrument track while everything is still written in one window. The main
  ChordLab stops playing a track that another one plays. VST3 in Ableton Live only.
- The usual set-up is one Live track with an Instrument Rack: the main ChordLab in the first chain, one duplicate
  of that chain per extra line; each instrument track takes MIDI From that track and that chain's ChordLab.
- A duplicated or empty ChordLab offers the main one's tracks in a banner; otherwise the Follow menu in the
  transport bar does. A ChordLab that plays another's track shows a small panel instead of the editor (what it
  plays, its state and the way out of each problem, Change and Stop following, its own Thru and Panic), and the
  main ChordLab marks the tabs of tracks that play elsewhere.
- Links are saved with the set and come back in any loading order; a deleted main ChordLab leaves the others
  waiting, and undoing the deletion relinks them.

## Install

Download `ChordLab-<version>.pkg` from the [latest release](https://github.com/fluxxion82/chordLab/releases/latest)
and open it. It is signed and notarized; pick the formats you want:

- **VST3** → `/Library/Audio/Plug-Ins/VST3/ChordLab.vst3` (Ableton Live: rescan in Settings → Plug-Ins, or restart Live).
- **Audio Unit** → `/Library/Audio/Plug-Ins/Components/ChordLab.component` (Logic Pro: MIDI FX slot → Audio Units →
  SterlingAlbury → ChordLab; if it is missing, Plug-in Manager → Full Audio Unit Reset, then reopen Logic)
  (validated with `auval`; Logic Pro itself not yet verified, see Status).
- **App** → `/Applications/ChordLab.app`.

macOS 15 or later, Apple silicon or Intel. The `.sha256` beside the pkg verifies the download, and the VST3 SDK
source tarball if you take it too (`shasum -a 256 -c --ignore-missing ChordLab-<version>.pkg.sha256`); the
`.provenance.txt` names the commits and dependency versions it was built from (see Licence). If you also build from
source, a development copy in `~/Library` carries the same identifiers; keep one or the other installed.

Uninstall: remove the three paths above and the receipts
(`sudo pkgutil --forget com.SterlingAlbury.chordlab.vst3`, `….au`, `….app`).

## Requirements

- macOS 15 or later (the deployment target; the UI uses current SwiftUI APIs).
- Xcode with the command line tools, and CMake 3.20+ for the engine tests.
- An [iPlug2](https://github.com/iPlug2/iPlug2) checkout **next to this repository**, named `iPlug2`:

  ```
  parent/
    iPlug2/
    chordLab/               <- this repository
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

The build installs the bundle to `~/Library/Audio/Plug-Ins/VST3/ChordLab.vst3` (a development copy; the
release pkg installs to `/Library`); rescan plug-ins in Live (or restart it) to pick up a new build.

The standalone app, handy for working on the UI without a host:

```
xcodebuild -project ChordLab/projects/ChordLab-macOS.xcodeproj -scheme macOS-APP -configuration Debug \
  CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY=- DEVELOPMENT_TEAM= PROVISIONING_PROFILE_SPECIFIER= CODE_SIGN_ENTITLEMENTS= build
```

The signing overrides are no longer needed (every target is ad-hoc signed with no team since the release
milestone) and stay harmless; the plain `-scheme macOS-APP … build` works too. The app lands in
`~/Applications/ChordLab.app`. In a git worktree, `IPLUG2_ROOT = ../../../iPlug2` must still resolve: link the
iPlug2 checkout next to the worktrees (`.claude/worktrees/iPlug2` exists for that).

The Audio Unit (`macOS-AUv2` scheme) builds the same way and installs to `~/Library/Audio/Plug-Ins/Components`;
`auval -strict -v aumi pozR Ster` validates it.

## Testing

One script runs every suite; with no flags it runs the engine's Debug build and tests only:

```
scripts/check.sh            # engine (Debug) build + ctest
scripts/check.sh --all      # + UBSan, ASan and TSan builds, Swift tests, bridge tests, both Xcode schemes, release preflight
```

Flags combine (`--ubsan`, `--asan`, `--tsan`, `--swift`, `--bridge`, `--plugin`, `--release`: the release
script's preflight in report mode; `--shell` and `--views`, below). Only `--plugin` and `--all` install anything
(the VST3 into `~/Library/Audio/Plug-Ins/VST3`, the app into `~/Applications`). The script stops at
the first failure and prints a PASS/FAIL table. Every sanitizer step runs under a hard timeout because
older Apple clang releases hung under ASan and TSan (see "Status").

The suites, each runnable on its own:

| Suite | What it covers | Command |
|---|---|---|
| `engine_tests` (doctest) | theory, document/undo, compiler, scheduler, SMF, JSON API, golden fixtures, mutation fuzz of the JSON readers | `cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Debug && cmake --build engine/build -j8 && (cd engine/build && ctest --output-on-failure)` |
| `engine_rt_alloc_test` | no allocation on the audio-thread surface | same ctest run |
| `ChordLabUICoreTests` (XCTest) | the UI's pure logic in `ChordLab/UI/Core/`: geometry, harmony text, symbols, key names, snapshot decoding | `cd ChordLab && swift test` |
| `vst3host` | the plug-in shell itself, without a DAW: a command-line VST3 host loads a freshly built `ChordLab.vst3` in-process and checks life cycle, state, playback against an engine reference, and two linked instances | `scripts/check.sh --shell` (builds the VST3 and the app into a scratch directory, installs nothing, then runs it; not combinable with `--plugin` or `--all`) |
| `viewharness` | the editor's views, offscreen: renders `ContentView` in each state of the follow feature at two window sizes, checks the frames and scripts the shell's answers to the editor | `scripts/check.sh --views` (same scratch directory rules; needs the display awake) |
| `bridge_tests` (doctest) | the plug-in shell's pure helpers in `ChordLab/bridge/`: message validation, preview, section-export, clip-export and link-request parsing, what a link message leads to, export file naming | `cmake -S ChordLab/bridge/tests -B ChordLab/build-bridge-tests && cmake --build ChordLab/build-bridge-tests -j8 && (cd ChordLab/build-bridge-tests && ctest --output-on-failure)` |

**JSON fixtures.** `engine/tests/fixtures/` holds the engine's own `snapshotJson`/`stateJson` output for
a few documents (empty, a four-chord progression with sections and a manual voicing, the same while
browsing another key), its `linkStateJson` output for linked instances (`link-state-*.json`: a leader
with followers, a copy nobody follows, a playing follower, a waiting one), plus hand-written Phase
1-shaped files. The engine compares its output to them byte for byte, and the Swift tests decode them, so
the two sides cannot drift apart silently. When the contract changes on purpose, regenerate the generated
ones with `CHORDLAB_UPDATE_FIXTURES=1 engine/build/engine_tests -tc="*golden*"` and review the diff; the legacy
files are maintained by hand. The Swift fixture tests fail if the directory is missing;
`CHORDLAB_UI_SKIP_FIXTURES=1 swift test` skips them and `CHORDLAB_UI_FIXTURES_DIR=<dir>` points them
elsewhere.

**Fuzz.** `test_json_robustness.cpp` mutates valid commands and states with a fixed-seed generator and
checks that the readers never throw, that a rejected message changes nothing (document, counters,
selection, undo/redo history) and that an accepted one leaves the document well-formed. A failure
prints the iteration and the escaped input, so it reproduces exactly.

A new pure UI file belongs in `ChordLab/UI/Core/` so the package tests see it; a new pure plug-in helper
belongs in `ChordLab/bridge/` with a case in `ChordLab/bridge/tests/`.

## Release

`scripts/release.sh` turns a clean, tagged checkout into the pkg on the Releases page. Once, on the signing
Mac: store the notary credentials (`xcrun notarytool store-credentials chordlab --apple-id <id> --team-id
GPX68LJ584 --password <app-specific password>`). Per release: bump `PLUG_VERSION_STR`/`PLUG_VERSION_HEX` in
`ChordLab/config.h`, build once (`scripts/check.sh --plugin`) so the six tracked Info.plists in `ChordLab/resources`
carry the new version, commit the bump with all six regenerated plists, merge to `main`, then

```
scripts/release.sh preflight --rehearsal   # tools, identities, profile, versions, plists current (before the tag exists)
git tag -a v<version> -m "ChordLab <version>: ..."   # the message becomes the release notes
scripts/release.sh preflight               # now strict: clean trees (ChordLab and iPlug2), iPlug2 commit public on GitHub, HEAD is the tag
scripts/release.sh build                   # Release build of VST3, AU and app into dist/stage, signed, auval, pkgs, notarized, stapled, SDK source tarball
sudo installer -pkg dist/ChordLab-<version>.pkg -target /    # the smoke test with the dev copies moved aside, then uninstall (see Install)
git push origin main v<version>            # publish needs the tag on GitHub
scripts/release.sh publish                 # checks the pkg belongs to HEAD and the tag, then gh release create with four assets
```

`publish` uploads four assets: the pkg, its `.sha256`, the `.provenance.txt` and the VST3 SDK source tarball.
`build --rehearsal --skip-notarize` rehearses on a dirty or untagged tree (never publishable). The Xcode project
itself stays ad-hoc signed; the Developer ID is injected by the script, so building from source needs no certificate.
`build` starts cold (it wipes `dist/`), takes several minutes per format, and installs the staged Audio Unit into
`~/Library/Audio/Plug-Ins/Components` for `auval`, replacing whatever copy was there. Preflight refuses a build while
`/Library/Audio/Plug-Ins/Components/ChordLab.component` exists (`auval` names no path, so it could validate that
copy instead of the staged one): move it aside first, and uninstall after the smoke test. Install Rosetta on the
signing Mac (`softwareupdate --install-rosetta --agree-to-license`) so `auval` also validates the Intel slice and
the provenance records `auval: yes` rather than `yes (arm64 only)`, which `publish` accepts with a warning. The
first `productbuild --sign` may raise a keychain prompt for the Developer ID Installer key, and the build waits
until it is answered: click "Always Allow".

## Documentation

- [engine/README.md](engine/README.md): the engine's own notes (layout, JSON surface, error handling,
  real-time rules, threading contract); [engine/include/chordlab/json_api.h](engine/include/chordlab/json_api.h)
  is the engine ↔ UI contract.

## Licence

ChordLab is free software under the [GNU General Public License v3.0](LICENSE) (GPL-3.0-only),
copyright 2026 Sterling Albury. It is built on third-party code under its own terms, reproduced in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt), which ships inside every bundle:

- [iPlug2](https://github.com/iPlug2/iPlug2) and Cockos WDL: zlib-style licence.
- [RtAudio](https://github.com/thestk/rtaudio) 5.1.0 and [RtMidi](https://github.com/thestk/rtmidi) 4.0.0 (the
  standalone app's audio and MIDI I/O): MIT-style.
- [nlohmann/json](https://github.com/nlohmann/json) 3.11.2 (`engine/third_party/nlohmann`): MIT.
  [doctest](https://github.com/doctest/doctest) (MIT) is used by the tests only.
- The Steinberg [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) 3.7.14: dual-licensed; ChordLab uses it under the
  GPLv3 option, which is why the VST3 binary and this repository are GPL. Its `base` and `public.sdk` sources carry
  a BSD-style notice, reproduced in the notices file.
- Apple SDKs and the Audio Unit headers: Apple's SDK terms.

**Corresponding source.** Each release ships the vendored VST3 SDK's source as
`ChordLab-<version>-vst3sdk-source.tar.gz` beside the pkg. Its `.provenance.txt` names the ChordLab commit and tag
on GitHub, the iPlug2 commit on its public GitHub (`release.sh` refuses a commit GitHub cannot show), the VST3 SDK
version, its tag on Steinberg's GitHub, a hash of the vendored SDK tree and the tarball's digest, and the RtAudio,
RtMidi and nlohmann/json versions; "Build" above is the recipe. Together they are the complete source of the
binaries.

## Status

- **Verified in Live** through Test 11 (`docs/testing/`, 2026-11-06); the Logic check (Test 12) waits for a
  Logic install. The Audio Unit passes `auval -strict` (arm64; the x86_64 slice is validated only on a Mac
  with Rosetta), which proves it loads, publishes its properties and view and round-trips its state, not
  that MIDI flows through it in Logic.
- Sanitizers: with Apple clang 17 (Xcode 16) ASan deadlocked before `main()` and TSan hung; since
  Apple clang 21 (Xcode 27.1) UBSan, ASan and TSan all run the engine suite cleanly
  (`scripts/check.sh --ubsan --asan --tsan`, each step under a hard timeout).
- Tracks (the engine model and playback, the editor's logic, the lane tabs and piano roll, and the bass
  generator, milestones M1–M4) are merged on `main` but not released: the 1.0.0 pkg does not have them. They pass
  the engine, Swift and bridge suites, and Live Test 15 (2026-11-16, Live 12: lanes, note editing, the generator,
  playback with mute and solo, the drag-outs, saving); modifier clicks, pinch and Logic are not checked yet.
- Per-track instruments (a second ChordLab playing one track of the main one; engine, shell and editor) are
  merged on `main` but not released. They pass the engine suite with its two-instance simulation, the headless
  host test (two linked instances exact to the sample against the engine), the editor's render harness, and Live
  Test 16 (2026-11-17, Live 12, through an Instrument Rack): the instruments of two ChordLabs sounded identical
  to the sample at buffers of 64, 256 and 512, a saved set reopens linked in either load order, an audio export
  equals a real-time recording, and deleting the main ChordLab while playing, undoing and reordering chains
  leave no note hanging. Live refuses to freeze a track routed this way (record the line and set Monitor to Auto
  first), and a note can hang when a ChordLab device is switched off while it sounds (Panic ends it). Not
  checked yet: modifier clicks, pasted groups, the sound of a change while playing, the Audio Unit and the app
  with the editor's new hosting, Logic. Not built: auditions from a track's piano roll sounding on that track's
  own instrument; the Audio Unit and the app do not link.
- Not yet: a lead generator, feel on
  tracks; chords are contiguous; one suggestion style (pop); macOS only (VST3, Audio Unit, app). The Xcode project still carries
  iPlug2's AUv3, AAX, VST2 and iOS targets and Windows project files from the template; they are not
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
scripts/release.sh          signs, notarizes and packages a release (see Release)
scripts/release/resources/  installer panes
LICENSE                     GPL-3.0 text
THIRD_PARTY_NOTICES.txt     third-party notices, shipped in every bundle (see Licence)
```

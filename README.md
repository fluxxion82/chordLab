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
  separately browsed key, stepped along the circle of fifths (parallel-mode browsing is currently available only through the engine JSON command).
- A palette of the browsed key: 7 scale degrees × up to 5 tiers (power chord, triad, sixth/seventh,
  seventh/add9, colour chords). Chords with notes outside the project key are drawn as rings.
- Press a cell to hear it; drag it into the sequence bar to append it.
- A sequence bar of chord blocks over a beat ruler: reorder, resize (quarter-beat snap, 0.25–64 beats),
  select, delete; undo and redo.
- Playback that follows the host transport sample-accurately, with an optional loop, a playhead, and a
  Panic button.
- MIDI drag-out of the progression as a `.mid` clip at the host tempo.
- Everything is saved with the host project (the document, and the editor window size).

Phase 2, explainable harmony:

- A harmonic function (Tonic / Predominant / Dominant / Other) and a 0–100 tension score on every
  chord, shown as a coloured dot and a tension bar.
- "Why" popovers on sequence blocks: how the chord relates to the one before it (root motion, common
  tones, voice-leading cost, D→T resolution, the parallel mode a chord is borrowed from).
- A suggestions strip: up to 8 ranked next chords for the selected (else the last) chord, each with its
  reasons.
- Voicing: per-chord inversion and octave (−1/0/+1), and an Auto voice-lead mode that picks them for
  the smoothest motion from the previous chord.
- Sections: named, coloured spans over the progression (8 colours), each exportable as its own clip.
- A home-key compare row: while another key is browsed, the project key's seven triads stay available
  under the palette.

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
cmake -S engine -B engine/build && cmake --build engine/build -j8 && (cd engine/build && ctest --output-on-failure)
```

Two CTest targets run: `engine_tests` (the doctest suite) and `engine_rt_alloc_test` (no allocation on
the audio-thread surface). See `engine/README.md`.

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

UI logic checks (there is no XCTest target; this compiles the Foundation-only UI files with `swiftc`):

```
sh ChordLab/scripts/geometry_check/run.sh      # must end with ALL OK
```

## Using it in Ableton Live

ChordLab is an instrument that makes no sound: it emits MIDI, which a second track receives.

1. Create a MIDI track (track A) and drop **ChordLab** (Plug-Ins → VST3) on it.
2. Create a second MIDI track (track B) holding the instrument you want to hear.
3. On track B, set **MIDI From** to track A. In the second chooser directly below it, pick **ChordLab**
   (the MIDI coming out of the plug-in) instead of the default *Pre FX*.
4. Set track B's **Monitor** to **In** (or arm the track).
5. Press a palette cell: track B's instrument should sound, with Live's transport stopped or running.

Notes:

- With **Follow Live** on, the progression plays whenever Live's transport runs, locked to Live's beat
  position (the progression's first beat sits at the start of the arrangement); with **Loop** on it
  repeats from there, otherwise it plays once and is silent past its end.
  With Follow Live off the progression is silent and cell previews still work.
- Live merges all MIDI channels on internal track-to-track routing, so channels cannot be used to
  separate parts; use one ChordLab instance per receiving track.
- Use the VST3. The AU build is not useful in Live: Live cannot route MIDI out of an AU instrument.
- To commit a progression to a track, drag the **♫ … drag to Live** control (bottom right) onto a MIDI
  track: it lands as a MIDI clip at Live's tempo. The **♫** at the right end of a section span does the
  same for that section alone.

## A tour of the window

Top to bottom:

- **Key bar.** *Key*: tonic and mode pickers for the project key (choosing one also brings the browsed
  key home). *Browse*: `<` and `>` step the browsed key a fifth down or up, *Home* returns to the
  project key. Undo / redo buttons (⌘Z / ⇧⌘Z). *Names / Numerals / Both* chooses how chords are
  labelled in palette cells, the home row, sequence blocks and cell drag previews (default Both: the symbol over the numeral and function, e.g. `C` / `I · T`).
- **Transport row.** *Follow Live*, *Loop*, *Panic* (silences every sounding note), *Auto voice-lead*,
  and *+ Section* (starts a section on the selected chord, else the last one).
- **Sequence bar.** A sections header, the chord blocks, and a ruler with bar numbers.
  - Drop a palette or home cell anywhere on the bar to append the chord.
  - Click a block to select it (the suggestions follow the selection); click the empty bar to deselect.
  - Drag a block sideways to reorder; drag the handle on its right edge to change its length.
  - Delete or Forward Delete removes the selected block.
  - Click the small **v** badge for the voicing popover (inversion, octave, Auto). Right-click a block
    for *Voicing…*, *Inversion*, *Octave*, *Auto voicing* and *Remove chord*. A chord with its own
    voicing is marked "manual".
  - Rest the mouse on a block for its details and the "why" against the previous chord.
  - Sections header: double-click a span to rename it (Return commits, Escape cancels); right-click for
    *Rename…*, *Colour* and *Remove section*; drag a span's left edge to move where the section starts
    (the first section always starts at the first chord); drag its **♫** to a MIDI track to export it.
- **Suggestions strip.** "Start with" (empty progression) or "Next": up to 8 chips numbered 1–8. Click a
  chip, or press its digit 1–8, to append that chord; rest the mouse on it for the reasons.
- **Palette.** The browsed key's chords, one column per scale degree, power chords at the bottom and
  richer chords above. Press and hold a cell to hear it, release to stop; drag it to the sequence bar;
  rest the mouse on it for its notes, function, tension and any out-of-key notes. A filled circle is
  entirely in the project key, a ring is borrowed.
- **Home row.** Appears under the palette only while the browsed key differs from the project key:
  "Home · <project key>" and that key's seven triads. The cells behave exactly like palette cells
  (press to preview, drag to the sequence bar, hover for details).
- **Bottom line.** A status readout (cell and event counts, end beat, follow/loop state, errors), a
  development "Append C" button, and the MIDI drag-out control.

## Known limits and what is not yet verified

- **Live verification is pending.** The engine is covered by tests and both the VST3 and the standalone
  app build cleanly, but the Phase 2 UI and the home row have not yet been checked end to end inside
  Live (routing, transport follow, drag-out, keyboard shortcuts).
- Live may swallow the bare digit keys 1–8 (and possibly ⌘Z / ⇧⌘Z) before the plug-in window sees
  them; clicking the chips and buttons always works. The digits are also only active while the plug-in
  window is the key window and no section name is being edited.
- AddressSanitizer and ThreadSanitizer are unusable with the current Apple clang toolchain on the
  development machine (they hang or crash); the engine has not been run under them. UBSan works.
- No piano roll, melody tracks or key detection yet (planned for Phase 3); chords are contiguous, with
  no gaps or rests between them.
- macOS and VST3 only. The Xcode project still carries iPlug2's other targets (AU, AUv3, AAX, VST2, iOS)
  and Windows project files from the template; they are not maintained or tested.
- One suggestion style ("pop"); the ruler assumes 4 beats per bar.

## Repository layout

```
engine/                     C++17 core, no host or UI dependency (CMake + doctest); see engine/README.md
  include/chordlab/         public headers; json_api.h is the contract with the UI
  src/  tests/  fixtures/  tools/  third_party/
ChordLab/                   the iPlug2 plug-in
  ChordLab.h / ChordLab.mm  plug-in shell: audio thread, scheduler hand-off, messages, state, MIDI export
  config.h                  plug-in identity and I/O (VST3 "Instrument|Tools", MIDI in and out)
  UI/                       SwiftUI editor (a file-system-synchronized Xcode group)
  config/                   xcconfig files (IPLUG2_ROOT lives here)
  projects/                 Xcode projects (ChordLab-macOS.xcodeproj)
  resources/                Info.plists, icons, fonts
  scripts/geometry_check/   swiftc-compiled checks of the UI's pure logic
docs/plans/                 the Phase 1 and Phase 2 plans and decision records
```

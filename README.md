# ChordLab

ChordLab is a chord-progression plug-in for macOS in the style of Klimper 2. Pick a key, audition chords from a
palette and drag them into a sequence. ChordLab plays the progression as MIDI in time with your host, or drops it
onto a track as a clip. Over the chords you can write bass and lead lines in a piano roll that shows how each note
sits against the harmony, or let ChordLab generate them.

It comes as a VST3 instrument for Ableton Live, an Audio Unit MIDI FX for Logic Pro and a standalone app. The
plug-ins send MIDI and make no sound of their own, so put an instrument after them.

## Features

- **Keys and palette.** 12 tonics × 9 modes (the church modes, harmonic and melodic minor). A palette of the key's
  chords, from power chords to colour chords. Borrow chords from neighbouring keys and parallel modes, with
  out-of-key chords marked. An Applied view shows secondary dominants and leading-tone sevenths.
- **Sequence.** Chord blocks over a beat ruler: reorder, resize, multi-select, copy and paste, undo. Palette presses
  can be recorded as blocks. Named, coloured sections. 29 progression presets, transposed into your key.
- **Harmony, explained.** Function and tension for every chord, a tension curve, "why" tips on how each chord
  relates to the one before, ranked suggestions for the next chord, and palette cells sized by how related they
  are to the selected chord.
- **Voicing and feel.** Per-chord inversion and octave, or automatic voice leading. Block, pulse, offbeat and
  arpeggio rhythms (seventeen arpeggio patterns), strum, humanise and any meter.
- **Listening.** Play into ChordLab's track and it names the key and chord it hears. Add the chord or adopt the key
  in one click; Thru passes your playing on.
- **Tracks and a piano roll.** Up to four bass, lead or other tracks over the chords. Each is edited in a piano roll
  whose rows are shaded by what each pitch is over the current chord: root, chord tone, tension or avoid note.
  Notes move with their chords.
- **Generated lines.** Six bass patterns, from roots to a walking bass, and six lead patterns, including motifs
  with 64 takes, guide tones, arpeggios and stepwise runs. Each is one undo step.
- **An instrument per track** (VST3 in Ableton Live). A second ChordLab plays one track of the first, so bass, lead
  and chords each drive their own instrument while you write everything in one window.
- **Host sync and MIDI out.** Sample-accurate playback that follows the host transport, with loop and Panic. Drag
  the progression, a section, one track or all of them out as MIDI clips. Everything is saved with the host project.

## Install

> No release has been published yet. Until the first one, build ChordLab from source: see
> [docs/development.md](docs/development.md).

Download `ChordLab-<version>.pkg` from the [latest release](https://github.com/fluxxion82/chordLab/releases/latest)
and open it. It is signed and notarized. Pick the formats you want:

- **VST3**, for Ableton Live: `/Library/Audio/Plug-Ins/VST3/ChordLab.vst3`. Rescan in Live's Settings → Plug-Ins, or
  restart Live.
- **Audio Unit**, for Logic Pro: `/Library/Audio/Plug-Ins/Components/ChordLab.component`. It shows in a track's MIDI
  FX slot under Audio Units → SterlingAlbury → ChordLab.
- **App**: `/Applications/ChordLab.app`.

It needs macOS 15 or later, on Apple silicon or Intel. To verify the download, run
`shasum -a 256 -c --ignore-missing ChordLab-<version>.pkg.sha256` next to the pkg.

To uninstall, remove those three paths and forget the receipts:

```bash
sudo pkgutil --forget com.SterlingAlbury.chordlab.vst3 && sudo pkgutil --forget com.SterlingAlbury.chordlab.au && sudo pkgutil --forget com.SterlingAlbury.chordlab.app
```

Then start with the [tutorial](docs/tutorial.md).

## Documentation

- [Tutorial](docs/tutorial.md): a first session in Ableton Live, from a key to a sketch with chords, bass and lead
  on their own instruments.
- [User guide](docs/user-guide.md): every control and shortcut, what is saved, limits and troubleshooting.
- [Development](docs/development.md): building from source, running the tests, the repository layout.
- [Architecture](docs/architecture.md): how the engine, the plug-in shell and the editor fit together.
- [Releasing](docs/releasing.md): how releases are signed, notarized and published.
- [engine/README.md](engine/README.md): the engine's internals.
  [engine/include/chordlab/json_api.h](engine/include/chordlab/json_api.h) is the contract between the engine and the UI.

## Status

- Tested in Ableton Live 12 with the VST3. The Audio Unit passes Apple's `auval -strict` validation but has not
  yet been tested end to end in Logic Pro.
- An instrument per track works in the VST3 in Ableton Live only, not in the Audio Unit or the app.
- Not there yet:
  - feel on tracks (track notes play exactly as written);
  - gaps between chords;
  - suggestion styles other than pop;
  - Windows and Linux.

## Licence

ChordLab is free software under the [GNU General Public License v3.0](LICENSE) (GPL-3.0-only), copyright 2026
Sterling Albury. It is built on third-party code under its own terms, reproduced in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt), which ships inside every bundle:

- [iPlug2](https://github.com/iPlug2/iPlug2) and Cockos WDL: zlib-style licence.
- [RtAudio](https://github.com/thestk/rtaudio) 5.1.0 and [RtMidi](https://github.com/thestk/rtmidi) 4.0.0 (the
  standalone app's audio and MIDI I/O): MIT-style.
- [nlohmann/json](https://github.com/nlohmann/json) 3.11.2 (`engine/third_party/nlohmann`): MIT.
  [doctest](https://github.com/doctest/doctest) (MIT) is used by the tests only.
- The Steinberg [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) 3.7.14: dual-licensed. ChordLab uses it under
  the GPLv3 option, which is why the VST3 binary and this repository are GPL. Its `base` and `public.sdk` sources
  carry a BSD-style notice, reproduced in the notices file.
- Apple SDKs and the Audio Unit headers: Apple's SDK terms.

**Corresponding source.** Each release ships the vendored VST3 SDK's source
(`ChordLab-<version>-vst3sdk-source.tar.gz`) and a `.provenance.txt` beside the pkg. The provenance names the exact
ChordLab, iPlug2, SDK and library versions the binaries were built from. With
[docs/development.md](docs/development.md) as the recipe, these make up the complete source of the binaries.

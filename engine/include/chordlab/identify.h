#pragma once

#include "chordlab/chord.h"

#include <optional>
#include <vector>

namespace chordlab {

/// A chord read off held notes (design D9): `chord`, the lowest held pitch `bass` (a MIDI note number),
/// and `inversion`, the index of the bass's pitch class in `pitchClasses(chord)` (interval order, root
/// first: the meaning `voicing` gives it, so 0 is root position).
struct HeardChord {
    Chord chord;
    int bass;
    int inversion;
};

/// The dictionary chord whose pitch-class set equals that of `heldPitches`, or nullopt.
/// `heldPitches` are MIDI note numbers (0..127) in any order, duplicates and octave doublings allowed;
/// they must span at least two distinct pitch classes, otherwise the result is nullopt. The match is
/// exact: every held class belongs to the chord and every chord tone is held (no near matches, no
/// omitted fifths). A set matched by several (root, type) pairs is resolved by rank: a root equal to the
/// class of the lowest held pitch first, then fewer tones, then dictionary order of the type
/// (`allChordTypes()`), so A-C-E-G reads Am7 over A but C6 over C, and a symmetric chord (augmented,
/// diminished seventh) takes the bass as its root. Pure: no state, no allocation beyond the result.
std::optional<HeardChord> identifyChord(const std::vector<int>& heldPitches);

}  // namespace chordlab

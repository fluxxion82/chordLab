#pragma once

#include <array>

namespace chordlab {

/// A pitch class in the range 0..11, with C = 0.
using PitchClass = int;

/// The seven diatonic modes plus harmonic and melodic (ascending) minor.
enum class Mode {
    Ionian,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Aeolian,
    Locrian,
    HarmonicMinor,
    MelodicMinor
};

constexpr int kNumModes = 9;

/// Semitone offsets of each scale degree from the tonic, e.g. Ionian {0,2,4,5,7,9,11}.
const std::array<int, 7>& modeIntervals(Mode mode);

/// The seven pitch classes of the scale built on `tonic`, in degree order.
std::array<PitchClass, 7> scalePitchClasses(PitchClass tonic, Mode mode);

/// True if `pc` is one of the scale's pitch classes.
bool inScale(PitchClass pc, PitchClass tonic, Mode mode);

/// Scale degree (0..6) of `pc` in the scale, or -1 if `pc` is not in the scale.
int degreeOf(PitchClass pc, PitchClass tonic, Mode mode);

/// Pitch class of a MIDI note number. Handles negative inputs (always returns 0..11).
PitchClass pitchClassOf(int midi);

}  // namespace chordlab

#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <optional>
#include <vector>

namespace chordlab {

/// How the root moves from one chord to the next, classified by the interval up from the first root to
/// the second, `(to - from) mod 12`:
///   0 Same, 1 SemitoneUp, 2 StepUp, 3 or 4 ThirdUp, 5 DownFifth, 6 Tritone, 7 UpFifth,
///   8 or 9 ThirdDown, 10 StepDown, 11 SemitoneDown.
/// Five semitones up is the same root as a fifth down (V -> I, the strongest motion), which is the name
/// it takes; seven up is "up a fifth". Thirds and steps do not distinguish major from minor.
/// A `RootMotion` passed to any engine function must be one of the declared enumerators.
enum class RootMotion {
    Same,
    DownFifth,
    UpFifth,
    StepUp,
    StepDown,
    ThirdUp,
    ThirdDown,
    Tritone,
    SemitoneUp,
    SemitoneDown
};

/// Lowercase phrase for the UI's "why" sentence: "same root", "down a fifth", "up a fifth", "up a step",
/// "down a step", "up a third", "down a third", "tritone", "up a semitone", "down a semitone".
/// The pointer is to a string literal and never goes stale.
const char* rootMotionName(RootMotion motion);

/// The motion from root `from` to root `to` (see `RootMotion`). Both roots are reduced modulo 12, so
/// the result depends only on the interval between them.
RootMotion rootMotion(PitchClass from, PitchClass to);

/// Upper bound on the number of notes `voiceLeadingCost` accepts per voicing. The dictionary's largest
/// chord has five; the search is over every bijection (n!), which stays trivial up to six.
constexpr int kMaxVoiceLeadingNotes = 6;

/// Total displacement, in semitones, of the cheapest way to move the voices of `a` onto the voices of
/// `b`: the minimum over every one-to-one pairing of the sum of |a[i] - b[j]|. The order of the notes
/// in either voicing is irrelevant. When the voicings differ in size, the shorter is first padded up to
/// the longer's size with its lowest note an octave up (the same doubled note as often as needed), so a
/// triad against a seventh chord moves three real voices plus one doubled root; the result is therefore
/// symmetric for any two voicings. Both voicings are MIDI note numbers (0..127), non-empty, and hold at
/// most `kMaxVoiceLeadingNotes` notes; anything else is a precondition violation.
int voiceLeadingCost(const std::vector<int>& a, const std::vector<int>& b);

/// The parallel mode `chord` is borrowed from: nullopt when `chord` is fully in `projectKey`; otherwise
/// the first key, on the project tonic, in the order Aeolian, Dorian, Mixolydian, Lydian, Phrygian,
/// HarmonicMinor, MelodicMinor, Locrian, Ionian (skipping `projectKey.mode`) whose scale holds every
/// pitch class of the chord, spelled with `canonicalTonic` for that mode (as `parallelModes` does); or
/// nullopt when no parallel mode holds it. The order is the same for every project mode: minor family
/// first, the parallel major last. `chord.type` must be non-null.
std::optional<Key> borrowedFrom(const Chord& chord, const Key& projectKey);

/// The structured "why" between two consecutive chords (P2-D2), as data for the UI to render.
struct Relation {
    /// Number of pitch classes the two chords share (`commonPcs.size()`).
    int commonTones;
    /// The shared pitch classes, ascending.
    std::vector<PitchClass> commonPcs;
    /// Motion from the first root to the second.
    RootMotion motion;
    /// `voiceLeadingCost` of the two chords' default `closeVoicing`s.
    int voiceLeadingCost;
    /// `borrowedFrom` of the second chord against the project key.
    std::optional<Key> borrowedFrom;
};

/// Relates `to` to the chord `from` that precedes it, in `projectKey`. Both chords must have a non-null
/// type.
Relation relate(const Chord& from, const Chord& to, const Key& projectKey);

}  // namespace chordlab

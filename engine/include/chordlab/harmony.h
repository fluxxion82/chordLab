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

/// Upper bound on the number of notes `voiceLeadingCost` and `pcVoiceLeadingCost` accept per side. The
/// dictionary's largest chord has five; the search is over every bijection (n!), which stays trivial
/// up to six.
constexpr int kMaxVoiceLeadingNotes = 6;

/// Cost of moving one concrete voicing onto another: the total displacement, in semitones, of the
/// cheapest one-to-one pairing of the MIDI notes of `a` with those of `b` (the minimum over every
/// bijection of the sum of |a[i] - b[j]|). This is the register-aware cost for actual voicings, as the
/// auto voice-leading of P2-D5 compares them; the smoothness of one chord into another regardless of
/// register is `pcVoiceLeadingCost`. The order of the notes in either voicing is irrelevant. When the
/// voicings differ in size, the shorter is first padded up to the longer's size with its lowest note an
/// octave up (the same doubled note as often as needed), so a triad against a seventh chord moves three
/// real voices plus one doubled root; the result is therefore symmetric for any two voicings. Both
/// voicings are MIDI note numbers (0..127), non-empty, and hold at most `kMaxVoiceLeadingNotes` notes;
/// anything else is a precondition violation.
int voiceLeadingCost(const std::vector<int>& a, const std::vector<int>& b);

/// Octave-free cost of moving the pitch classes of `a` onto those of `b`: the minimum over every
/// bijection of the sum of circular distances min(d, 12 - d), so C -> F is 3 (E->F, G->A), C -> Am is 2
/// (G->A) and C -> Db is 3, whatever register either chord would be voiced in. When the sets differ in
/// size, the shorter is first padded to the longer's size by cycling through its own entries in the
/// given order (C E G against a seventh chord becomes C E G C, against a ninth chord C E G C E), so with
/// pitch classes root first, as `pitchClasses` returns them, the root is the first note doubled. The
/// padding depends only on the shorter side, so the result is symmetric for any two sets. Entries are
/// reduced modulo 12. Both sets must be non-empty and hold at most `kMaxVoiceLeadingNotes` entries.
int pcVoiceLeadingCost(const std::vector<PitchClass>& a, const std::vector<PitchClass>& b);

/// The parallel mode `chord` is borrowed from: nullopt when `chord` is fully in `projectKey`; otherwise
/// the first key on the project tonic, in the search order of the project mode's family (skipping
/// `projectKey.mode` itself), whose scale holds every pitch class of the chord, spelled with
/// `canonicalTonic` for that mode (as `parallelModes` does); or nullopt when no parallel mode holds it.
/// The orders put the opposite family first and the closest modes before the remote ones:
/// - major family (Ionian, Lydian, Mixolydian): Aeolian, Mixolydian, Lydian, Dorian, Phrygian,
///   HarmonicMinor, MelodicMinor, Locrian, Ionian — so in C major Ab and Bb are Aeolian's, F#° is
///   Lydian's and Db is Phrygian's;
/// - minor family (Aeolian, Dorian, Phrygian, HarmonicMinor, MelodicMinor) and Locrian: HarmonicMinor,
///   MelodicMinor, Ionian, Aeolian, Dorian, Mixolydian, Phrygian, Lydian, Locrian — so in C minor G and
///   G7 are harmonic minor's, C major is the parallel major's and Dm is melodic minor's.
/// `chord.type` must be non-null.
std::optional<Key> borrowedFrom(const Chord& chord, const Key& projectKey);

/// The structured "why" between two consecutive chords (P2-D2), as data for the UI to render.
struct Relation {
    /// Number of pitch classes the two chords share (`commonPcs.size()`).
    int commonTones;
    /// The shared pitch classes, ascending.
    std::vector<PitchClass> commonPcs;
    /// Motion from the first root to the second.
    RootMotion motion;
    /// `pcVoiceLeadingCost` of the two chords' pitch classes (root first): how smoothly one chord moves
    /// into the next, octave-free and independent of how either is voiced. The register-aware cost of
    /// two actual voicings is `voiceLeadingCost`'s business, not this field's.
    int voiceLeadingCost;
    /// `borrowedFrom` of the second chord against the project key.
    std::optional<Key> borrowedFrom;
};

/// Relates `to` to the chord `from` that precedes it, in `projectKey`. Both chords must have a non-null
/// type.
Relation relate(const Chord& from, const Chord& to, const Key& projectKey);

}  // namespace chordlab

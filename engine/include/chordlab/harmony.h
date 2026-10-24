#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/voicing.h"

#include <optional>
#include <string>
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

/// Octave-free cost of moving the pitch classes of `a` onto those of `b`: the minimum over every
/// bijection of the sum of circular distances min(d, 12 - d), so C -> F is 3 (E->F, G->A), C -> Am is 2
/// (G->A) and C -> Db is 3, whatever register either chord would be voiced in. When the sets differ in
/// size, the shorter is first padded to the longer's size by cycling through its own entries in the
/// given order (C E G against a seventh chord becomes C E G C, against a ninth chord C E G C E), so with
/// pitch classes root first, as `pitchClasses` returns them, the root is the first pitch class doubled
/// (a pitch-class set has no bass to double; the register-aware `voiceLeadingCost` of voicing.h doubles
/// the lowest note instead). The padding depends only on the shorter side, so the result is symmetric
/// for any two sets. Entries are
/// reduced modulo 12. Both sets must be non-empty and hold at most `kMaxVoiceLeadingNotes` (voicing.h)
/// entries. The register-aware counterpart for two actual voicings is `voiceLeadingCost` in voicing.h.
int pcVoiceLeadingCost(const std::vector<PitchClass>& a, const std::vector<PitchClass>& b);

/// The parallel mode `chord` is borrowed from: nullopt when `chord` is fully in `projectKey`; otherwise
/// the first key on the project tonic, in the search order of the project mode's family (skipping
/// `projectKey.mode` itself), whose scale holds every pitch class of the chord, spelled with
/// `canonicalTonic` for that mode (as `parallelModes` does); or nullopt when no parallel mode holds it.
/// The orders are fixed, family-dependent priorities, not a distance measure: each puts the opposite
/// family first and then the modes a chord is conventionally said to be borrowed from before the remote
/// ones, so a chord two modes hold is attributed to the earlier one (Dorian's bVI goes to harmonic
/// minor, which precedes Aeolian in the minor-family order):
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
    /// two actual voicings is `voiceLeadingCost`'s (voicing.h) business, not this field's.
    int voiceLeadingCost;
    /// `borrowedFrom` of the second chord against the project key.
    std::optional<Key> borrowedFrom;
};

/// Relates `to` to the chord `from` that precedes it, in `projectKey`. Both chords must have a non-null
/// type.
Relation relate(const Chord& from, const Chord& to, const Key& projectKey);

// --- Sentences ---------------------------------------------------------------------------------
//
// The pieces every "why" the UI shows is built from, so that the suggestion popover, the event's `why.text`
// and a palette cell's `relationText` (json_api.h) word the same facts the same way. All allocate: not for
// the audio thread.

/// A key in prose: the spelled tonic (ASCII `b`/`#`) and the mode as `Mode` spells it, except the two
/// altered minor scales: "C Aeolian", "Bb Dorian", "A harmonic minor", "D melodic minor". What every
/// "borrowed from ..." clause names. (The snapshot's `keyName`, json_api.h, is the machine form
/// "A HarmonicMinor".)
std::string keyProse(const Key& key);

/// The common-tone phrase of `relation`: the count, "tone" or "tones", and `commonPcs` spelled in
/// `projectKey` (`spell`, pitch.h), comma-separated: "1 common tone (G)", "2 common tones (C, E)";
/// "no common tones" when the chords share none.
std::string commonTonesText(const Relation& relation, const Key& projectKey);

/// The short sentence for `relation`, as a palette cell's `relationText` shows it: `commonTonesText`, the
/// `rootMotionName`, "cost <voiceLeadingCost>" and, when `borrowedFrom` is set, "borrowed from <keyProse>",
/// joined by " · " (a literal UTF-8 middle dot between spaces):
/// "2 common tones (C, E) · down a third · cost 2",
/// "1 common tone (C) · down a third · cost 2 · borrowed from C Aeolian".
std::string relationText(const Relation& relation, const Key& projectKey);

}  // namespace chordlab

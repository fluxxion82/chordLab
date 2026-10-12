#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"

#include <string>
#include <vector>

namespace chordlab {

/// An applied (secondary) chord: the dominant seventh or the leading-tone diminished seventh of one
/// diatonic degree of a key, the chord that tonicises that degree for a moment.
struct AppliedChord {
    /// The applied chord: type `7` a perfect fifth above the target's root, or type `dim7` a semitone
    /// below it.
    Chord chord;
    /// Scale degree (1..6) of the key whose diatonic triad is tonicised; never 0, the key's own tonic.
    int targetDegree;
    /// `V7/` or `vii°7/` followed by the Standard-style `romanNumeral` of the target triad in the key:
    /// `V7/ii`, `vii°7/V`, and in A minor `V7/bVI`, since numerals read against the parallel major.
    std::string label;
    /// A key in which `chordSymbol(chord, spellingKey)` spells the chord as the theory writes it: the
    /// dominant on the target's fifth and the leading-tone chord on the letter below the target
    /// (`C#°7` for vii°7/ii in C major, where spelling in C itself would write the chromatic root as
    /// `Db`). It is the major key on the target's root, spelled as that degree of `key`, in which both
    /// roots are scale degrees; when that key would need eight or more accidentals it is the minor key
    /// on the same tonic (the leading tone is then a double sharp, which `spell` folds to its natural:
    /// vii°7/vi in C# Lydian is `A°7`, not `Gx°7`), and when that is beyond seven too, the canonical
    /// major key of the target's pitch class.
    Key spellingKey;
};

/// The applied chords of `key`: for each scale degree 1..6 whose diatonic triad is not diminished (a
/// diminished chord is not tonicised), the dominant seventh `V7/x` on the pitch class a perfect fifth
/// above the target's root and the leading-tone seventh `vii°7/x` on the pitch class a semitone below
/// it. Ordered by target degree, `V7/x` before `vii°7/x`. In C major: A7, C#°7, B7, D#°7, C7, E°7, D7,
/// F#°7, E7, G#°7 (ii, iii, IV, V, vi; nothing for vii°). An applied chord usually has a note outside
/// the key, but not always: G7 in A Aeolian is both V7/bIII and the diatonic VII7.
std::vector<AppliedChord> appliedChords(const Key& key);

}  // namespace chordlab

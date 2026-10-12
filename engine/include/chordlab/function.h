#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"

namespace chordlab {

/// Harmonic function of a chord in a key: the tonic group, the predominants, the dominants, or none of
/// those.
enum class Function { Tonic, Predominant, Dominant, Other };

/// The label the UI shows: "T", "PD", "D" or "O".
const char* functionShortName(Function function);

/// The function of `chord` in `projectKey` (decision P2-D1).
///
/// The key's mode picks a family: **major** is Ionian, Lydian and Mixolydian; **minor** is Aeolian,
/// Dorian, Phrygian, harmonic minor and melodic minor; **Locrian** stands alone. Degrees are those of
/// `numeralDegree` (symbol.h): the parallel-major degree of the root plus its `b`/`#` alteration, which
/// is also the scale degree of the mode for every root in the mode's scale (Locrian's b5 excepted).
///
/// Rules, first match wins:
///
/// 1. Locrian: the diatonic tonic chord (fully in key, root on the tonic) is Tonic; every other chord,
///    diatonic or not, is Other. A key without a perfect fifth above its tonic has no dominant worth
///    naming, and the chromatic rules below assume one.
///
/// 2. A chord fully in the key is labelled by its degree, whatever its quality or extension:
///
///        degree   I/i   ii/ii°   iii/III   IV/iv   V/v   vi/VI   vii°/VII
///        major    T     PD       T         PD      D     T       D
///        minor    T     PD       T         PD      D     T       D
///
///    The two rows coincide (I iii vi / i III VI are the tonic group, ii IV / ii° iv the predominants,
///    V vii° / V v VII vii° the dominants), so the family only decides Locrian; both are spelled out
///    because each is a claim about its own chords (Dorian's IV and Mixolydian's v or VII are pinned
///    by this table, not by analogy).
///
/// 3. A chord with a note outside the key, in order:
///    a. applied dominant: a chord with a major third and no major seventh (`maj`, `+`, `6`, `7`,
///       `add9`, `9`, `6/9`, `7#11`) whose root is a perfect fifth above a scale degree whose diatonic
///       triad is not diminished → Dominant. This is V/x and V7/x of any non-diminished degree, and
///       also the major V and V7 of a minor key (V/i). One exception: without a minor seventh, a chord
///       whose target would be the subtonic (bVII, a minor seventh above the tonic) is read as the
///       borrowed IV of its minor-family key under rule d (`D` in A minor is Dorian's IV, not V/VII);
///       with the seventh it is V7/VII, as `appliedChords` (applied.h) lists it.
///    b. applied leading-tone: a chord with a minor third and a diminished fifth (`dim`, `dim7`,
///       `m7b5`) whose root is a semitone below such a degree → Dominant (vii°7/V; vii° and vii°7 of a
///       minor key).
///    c. a chromatic root (alteration ≠ 0) carrying no minor third: bII and bVI → Predominant
///       (Neapolitan, borrowed submediant); bIII and bVII → Tonic (borrowed mediant and subtonic are
///       tonic-group chords, labelled "T" like any other); #IV → Other.
///    d. a diatonic root (alteration 0) whose chord some parallel mode of the tonic contains — a
///       borrowed chord such as iv, iiø7 or i in major, IV, ii or v in harmonic minor, iii or vi in
///       Aeolian → the degree table of rule 2.
///    e. anything else (a minor or diminished chord on a chromatic root, or a chord no parallel mode
///       holds, such as `F#` in A minor or `AΔ7` in C major) → Other.
///
/// Preconditions: `chord.type` non-null and `chord.root` in 0..11; `projectKey` as documented in pitch.h.
Function functionOf(const Chord& chord, const Key& projectKey);

}  // namespace chordlab

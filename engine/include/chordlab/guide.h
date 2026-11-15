#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"

#include <array>
#include <cstdint>
#include <string>

namespace chordlab {

/// What a pitch class is over a chord, from most to least stable (tracks design D9): the chord's root, one
/// of its other tones, a tension the chord scale offers, a scale tone that clashes (an avoid note), or a
/// pitch class outside the chord scale.
enum class GuideRole { Root, ChordTone, Tension, Avoid, Outside };

/// The role's wire name: "root", "chord", "tension", "avoid", "outside".
const char* guideRoleName(GuideRole role);

/// The harmonic guide of one chord: per pitch class, its role, its degree above the root and its lettered
/// name, plus the chord scale. Every array is indexed by ABSOLUTE pitch class 0..11 (C = 0), not relative to
/// the root. `scaleMask` has bit `pc` set exactly when `pc` is in the chord scale; it is separate from the roles
/// on purpose (a Tension need not be in the scale: G over E7 in C major). `degree` and `name` are ASCII
/// ("b9", "#11", "bb7"; "F#", "Bb", "Fx", "E#"); the UI maps them to glyphs.
struct Guide {
    std::array<GuideRole, 12> role;
    std::uint16_t scaleMask;
    std::array<std::string, 12> degree;
    std::array<std::string, 12> name;
};

/// The harmonic guide of `chord` heard in `projectKey` (tracks design D9; the readings below are the rules).
///
/// 1. **Letters are structural, never display spelling.** The root's letter is `spell(chord.root,
///    projectKey).letter`; each chord tone (interval `i` of the type) takes the letter `chordToneDegree(type, i)`
///    steps above it (symbol.h), with the accidental that reaches its pitch class from that letter, unfolded:
///    C# major's own third is E# on letter E, and in C# major a D major chord's third is F# on letter F. (Should
///    no spelling on that letter lie within a double accidental, the tone keeps `spell`'s; no dictionary chord
///    reaches that in a key the engine names canonically.) The key's seven scale tones carry their letters from
///    the key signature (`spellDegree`, every mode, harmonic and melodic minor included).
/// 2. **Chord scale.** It starts as the key's seven lettered scale tones. Each chord tone, in interval order,
///    whose pitch class is not among them replaces the scale tone on its letter (E7 in C: G# on G replaces G,
///    giving E F G# A B C D; D7 in C: F# replaces F); when that letter's scale tone was already replaced or is
///    itself a chord tone, the chord tone is added without replacing.
/// 3. **Roles.** `chord.root` is Root, the chord's other tones ChordTone; a chord-scale tone outside the chord
///    is Avoid when it lies a semitone above a chord tone (F over C, C over G7, F over Am7, F and C over Em7),
///    else Tension; a key tone the chord scale replaced is Avoid (G over E7 in C); every other pitch class is
///    Outside.
/// 4. **Dominant exception** (major third and minor seventh among the chord tones): a pitch class a semitone,
///    three semitones or eight semitones above the root (b9, #9, b13) that is one of the project key's seven
///    tones and that step 3 called Avoid is Tension (E7 in C: F, G and C; A, the 11 over the major third, stays
///    Avoid). Chromatic ones stay Outside (G7 in C: Ab, Bb, Eb).
/// 5. **Suspended exception** (no third, minor or major, and a 2 or a 4: sus2, sus4, 7sus4; the power chord is
///    not suspended): a chord-scale tone three or four semitones above the root, the third the suspension
///    replaces, is Avoid (Csus4 in C: E Avoid, D Tension). Over a power chord the key's third stays a Tension.
/// 6. **Degrees.** A chord tone is named by its structural degree and its alteration against the major or
///    perfect interval of that degree: "R", "b3", "3", "b5", "5", "#5", "bb7" (dim7), "b7", "7"; "2", "4",
///    "6" for tones below the octave (sus2, sus4, the 6 of 6/m6/69); "9", "#11" and the like for tones the type
///    writes at 12 semitones or more (add9's 9, 7#11's #11). A non-chord tone `i` semitones (mod 12) above the
///    root, where "has a seventh" means a chord tone of interval class 10 or 11 or dim7's diminished seventh:
///    1 "b9"/"b2" and 2 "9"/"2" (with/without a seventh); 3 "#9"/"#2" when the chord has a major third, else
///    "b3"; 4 "3"; 5 "11"/"4"; 6 "#11"/"#4" when the chord has a perfect fifth, else "b5"; 7 "5"; 8 "b13"/"b6"
///    when it has a perfect fifth, else "#5"; 9 "13"/"6"; 10 "b7"; 11 "7".
/// 7. **Names.** A chord tone by its structural letter (step 1); a chord-scale tone and a replaced key tone by
///    its key letter (or the letter it was added on); an Outside pitch class by `spell(pc, projectKey)`.
///
/// `chord.type` must be non-null and `chord.root` in 0..11. Allocates (the strings): not for the audio thread.
Guide harmonicGuide(const Chord& chord, const Key& projectKey);

}  // namespace chordlab

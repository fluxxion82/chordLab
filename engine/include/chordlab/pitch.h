#pragma once

#include "chordlab/scale.h"

#include <string>

namespace chordlab {

/// A spelled tonic: a letter name 'A'..'G' plus a single accidental (-1 flat, 0 natural, +1 sharp).
/// A pitch class alone cannot carry enharmonic intent (Eb Locrian needs 8 flats, D# Locrian 4 sharps),
/// so keys hold a spelled tonic and all note spelling is derived from letters, not from the signature's sign.
/// A `Tonic` passed to any engine function must have `letter` in 'A'..'G' and `alter` in -1..1; values
/// outside those ranges (e.g. a lowercase letter or a double accidental) are a precondition violation.
struct Tonic {
    char letter;
    int alter;
};

constexpr bool operator==(const Tonic& a, const Tonic& b) noexcept {
    return a.letter == b.letter && a.alter == b.alter;
}

constexpr bool operator!=(const Tonic& a, const Tonic& b) noexcept {
    return !(a == b);
}

/// A key: a spelled tonic and a mode.
struct Key {
    Tonic tonic;
    Mode mode;

    /// Pitch class (0..11) of the tonic.
    PitchClass tonicPc() const;
};

constexpr bool operator==(const Key& a, const Key& b) noexcept {
    return a.tonic == b.tonic && a.mode == b.mode;
}

constexpr bool operator!=(const Key& a, const Key& b) noexcept {
    return !(a == b);
}

/// The tonic spelling of `pc` for `mode` whose key signature has the fewest accidentals.
/// Candidates use at most one accidental: the natural letter, the sharp of the letter below and the
/// flat of the letter above (pc 6 -> F# or Gb; pc 0 -> C or B#). Ties prefer the natural spelling, then
/// the sharp (F Locrian over E# Locrian, B Lydian over Cb Lydian, F# Ionian over Gb Ionian). The chosen
/// key always satisfies |signatureOf| <= 6.
Tonic canonicalTonic(PitchClass pc, Mode mode);

/// Circle-of-fifths index of the key signature: positive counts sharps, negative counts flats.
/// It is the Ionian index of the tonic (C=0, G=1, D=2, A=3, E=4, B=5, F#=6, C#=7, F=-1, Bb=-2, ...,
/// Cb=-7) plus the mode offset (Lydian +1, Mixolydian -1, Dorian -2, Aeolian/HarmonicMinor/
/// MelodicMinor -3, Phrygian -4, Locrian -5). Unbounded for arbitrary keys (Eb Locrian is -8);
/// canonical keys stay within -6..6.
int signatureOf(const Key& key);

/// A spelled note: a letter 'A'..'G' plus an accidental count (-2..2 for any single-accidental tonic).
struct Spelling {
    char letter;
    int alter;

    /// ASCII rendering: "C", "F#", "Bb", "Fx", "Bbb". The UI maps these to glyphs.
    std::string text() const;
};

constexpr bool operator==(const Spelling& a, const Spelling& b) noexcept {
    return a.letter == b.letter && a.alter == b.alter;
}

constexpr bool operator!=(const Spelling& a, const Spelling& b) noexcept {
    return !(a == b);
}

/// Spelling of scale degree `degree` of `key`: the tonic letter advanced `degree` steps through the
/// cycle A..G, with the accidental needed to reach that degree's pitch class (normalised to -2..2).
/// `degree` is taken modulo 7, so 7 is the tonic again and -1 is the seventh.
Spelling spellDegree(const Key& key, int degree);

/// Degree (0..6) of the major scale that an interval of `semitones` above the tonic alters: a
/// major-scale member is its own degree, and every other interval is the degree it holds in a parallel
/// mode (1 is b2, 3 is b3, 6 is #4, 8 is b6, 10 is b7). The tritone is the only interval two modes spell
/// differently (Lydian #4, Locrian b5); it is taken as #4. `semitones` is reduced modulo 12. This is
/// the one table behind both chromatic spelling (`spell`) and the roman numerals (`romanNumeral`).
int majorDegreeOfInterval(int semitones);

/// Spelling of any pitch class in `key`. Scale members spell as their degree (`spellDegree`). A
/// chromatic tone is read against the parallel major of the tonic through `majorDegreeOfInterval`: it
/// takes the letter of the major degree it alters, with the accidental that reaches it from that
/// letter, so in C major pc 1, 3, 6, 8 and 10 are Db, Eb, F#, Ab and Bb, in D major pc 10 is Bb (bVI)
/// rather than A#, and in C Aeolian pc 4, 9 and 11 are E, A and B. A result on a white key with an
/// accidental (Cb, Fb, E#, B#, or a double accidental such as Fx in C# major) folds to its natural
/// name; a double accidental on a black key (Ex in B# major) falls back to the single accidental of
/// the same sign (F#). Chromatic tones therefore never carry more than one accidental.
Spelling spell(PitchClass pc, const Key& key);

}  // namespace chordlab

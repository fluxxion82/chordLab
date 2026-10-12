#include "chordlab/pitch.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>

namespace chordlab {

namespace {

constexpr int kNumLetters = 7;

// Natural pitch class of each letter, indexed 'A'..'G'.
constexpr std::array<PitchClass, kNumLetters> kNaturalPc{9, 11, 0, 2, 4, 5, 7};

// Circle-of-fifths index of each natural Ionian key, indexed 'A'..'G' (A=3, B=5, C=0, D=2, E=4, F=-1, G=1).
constexpr std::array<int, kNumLetters> kFifthsIndex{3, 5, 0, 2, 4, -1, 1};

// Major degree (0..6) altered by each interval above the tonic; see `majorDegreeOfInterval`.
constexpr std::array<int, 12> kMajorDegreeOfInterval{0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};

// Signature offset of each mode relative to Ionian on the same tonic, indexed by Mode.
constexpr std::array<int, kNumModes> kModeSignatureOffset{
    0,   // Ionian
    -2,  // Dorian
    -4,  // Phrygian
    1,   // Lydian
    -1,  // Mixolydian
    -3,  // Aeolian
    -5,  // Locrian
    -3,  // HarmonicMinor
    -3,  // MelodicMinor
};

int letterIndex(char letter) {
    assert(letter >= 'A' && letter <= 'G');
    return letter - 'A';
}

/// Letter at `index` steps above 'A', wrapping through the cycle A..G (any int is accepted).
char letterAt(int index) {
    return static_cast<char>('A' + ((index % kNumLetters) + kNumLetters) % kNumLetters);
}

PitchClass naturalPitchClass(char letter) {
    return kNaturalPc[static_cast<std::size_t>(letterIndex(letter))];
}

int fifthsIndex(char letter) {
    return kFifthsIndex[static_cast<std::size_t>(letterIndex(letter))];
}

int modeSignatureOffset(Mode mode) {
    const int index = static_cast<int>(mode);
    assert(index >= 0 && index < kNumModes);
    return kModeSignatureOffset[static_cast<std::size_t>(index)];
}

/// Index ('A' = 0) of the letter whose natural pitch class is `pc`, or -1 if `pc` is a black key.
int letterWithNaturalPc(PitchClass pc) {
    for (int i = 0; i < kNumLetters; ++i) {
        if (kNaturalPc[static_cast<std::size_t>(i)] == pc) {
            return i;
        }
    }
    return -1;
}

/// Accidental count that moves `letter` to `pc`, as the representative of the difference mod 12 in
/// -2..9. The formula alone does not bound the result to -2..2: that holds only because every caller
/// passes a letter within two semitones of `pc`, which is true for the degrees of any valid key
/// (single-accidental tonic, declared mode) and for a chromatic tone read from the major degree it
/// alters (`spell`), and is what keeps `Spelling::alter` in -2..2.
int alterToReach(char letter, PitchClass pc) {
    const int diff = pc - naturalPitchClass(letter);
    return ((diff % 12) + 12 + 2) % 12 - 2;
}

/// Tie-break order for enharmonic tonic spellings: natural, then sharp, then flat (lower wins).
int tonicSpellingRank(int alter) {
    switch (alter) {
        case 0: return 0;
        case 1: return 1;
        default: return 2;
    }
}

/// Checks the `Tonic` preconditions documented in pitch.h.
void assertValidTonic([[maybe_unused]] const Tonic& tonic) {
    assert(tonic.letter >= 'A' && tonic.letter <= 'G');
    assert(tonic.alter >= -1 && tonic.alter <= 1);
}

}  // namespace

PitchClass Key::tonicPc() const {
    assertValidTonic(tonic);
    return pitchClassOf(naturalPitchClass(tonic.letter) + tonic.alter);
}

int signatureOf(const Key& key) {
    assertValidTonic(key.tonic);
    return fifthsIndex(key.tonic.letter) + 7 * key.tonic.alter + modeSignatureOffset(key.mode);
}

Tonic canonicalTonic(PitchClass pc, Mode mode) {
    const PitchClass target = pitchClassOf(pc);
    bool found = false;
    Tonic best{'C', 0};
    int bestSignature = 0;
    for (int i = 0; i < kNumLetters; ++i) {
        for (int alter = -1; alter <= 1; ++alter) {
            const Key candidate{Tonic{letterAt(i), alter}, mode};
            if (candidate.tonicPc() != target) {
                continue;
            }
            const int signature = signatureOf(candidate);
            const bool fewerAccidentals = std::abs(signature) < std::abs(bestSignature);
            const bool simplerSpellingOnTie = std::abs(signature) == std::abs(bestSignature) &&
                                              tonicSpellingRank(alter) < tonicSpellingRank(best.alter);
            if (!found || fewerAccidentals || simplerSpellingOnTie) {
                found = true;
                best = candidate.tonic;
                bestSignature = signature;
            }
        }
    }
    assert(found);
    assert(std::abs(bestSignature) <= 6);
    return best;
}

std::string Spelling::text() const {
    std::string out(1, letter);
    if (alter == 2) {
        out += 'x';
    } else if (alter > 0) {
        out.append(static_cast<std::size_t>(alter), '#');
    } else if (alter < 0) {
        out.append(static_cast<std::size_t>(-alter), 'b');
    }
    return out;
}

Spelling spellDegree(const Key& key, int degree) {
    assertValidTonic(key.tonic);
    const int d = ((degree % 7) + 7) % 7;
    const char letter = letterAt(letterIndex(key.tonic.letter) + d);
    const PitchClass pc = scalePitchClasses(key.tonicPc(), key.mode)[static_cast<std::size_t>(d)];
    return Spelling{letter, alterToReach(letter, pc)};
}

int majorDegreeOfInterval(int semitones) {
    return kMajorDegreeOfInterval[static_cast<std::size_t>(pitchClassOf(semitones))];
}

Spelling spell(PitchClass pc, const Key& key) {
    const PitchClass target = pitchClassOf(pc);
    const int degree = degreeOf(target, key.tonicPc(), key.mode);
    if (degree >= 0) {
        return spellDegree(key, degree);
    }
    // A chromatic tone: the letter of the parallel-major degree it alters (b2, b3, #4, b6, b7), from the
    // same table as the roman numerals, with the accidental that reaches it from that letter.
    const Key major{key.tonic, Mode::Ionian};
    const Spelling base = spellDegree(major, majorDegreeOfInterval(target - major.tonicPc()));
    const int alter = alterToReach(base.letter, target);
    assert(alter >= -2 && alter <= 2);  // the major degree is always within a semitone of the tone
    const int natural = letterWithNaturalPc(target);
    if (alter != 0 && natural >= 0) {
        // Cb, Fb, E#, B# and any double accidental on a white key fold to the natural name.
        return Spelling{letterAt(natural), 0};
    }
    if (alter == 2 || alter == -2) {
        // A double accidental on a black key (Ex in B# major): the single accidental of the same sign.
        const int sign = alter > 0 ? 1 : -1;
        const int letter = letterWithNaturalPc(pitchClassOf(target - sign));
        assert(letter >= 0);  // every black key is a semitone from a white key on both sides
        return Spelling{letterAt(letter), sign};
    }
    return Spelling{base.letter, alter};
}

}  // namespace chordlab

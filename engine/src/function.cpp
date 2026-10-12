#include "chordlab/function.h"

#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

namespace chordlab {

namespace {

enum class ModeFamily { Major, Minor, Locrian };

ModeFamily familyOf(Mode mode) {
    switch (mode) {
        case Mode::Ionian:
        case Mode::Lydian:
        case Mode::Mixolydian:
            return ModeFamily::Major;
        case Mode::Dorian:
        case Mode::Phrygian:
        case Mode::Aeolian:
        case Mode::HarmonicMinor:
        case Mode::MelodicMinor:
            return ModeFamily::Minor;
        case Mode::Locrian:
            return ModeFamily::Locrian;
    }
    assert(false && "chordlab: mode outside the enum");
    return ModeFamily::Major;
}

// Rule 2 of function.h: the label of each scale degree of a diatonic chord, per family. The rows are
// identical (see the header for why both are written out).
constexpr std::array<Function, 7> kMajorByDegree{Function::Tonic,       Function::Predominant, Function::Tonic,
                                                 Function::Predominant, Function::Dominant,    Function::Tonic,
                                                 Function::Dominant};
constexpr std::array<Function, 7> kMinorByDegree{Function::Tonic,       Function::Predominant, Function::Tonic,
                                                 Function::Predominant, Function::Dominant,    Function::Tonic,
                                                 Function::Dominant};

Function byDegree(ModeFamily family, int degree) {
    assert(degree >= 0 && degree < 7);
    const auto& table = family == ModeFamily::Minor ? kMinorByDegree : kMajorByDegree;
    return table[static_cast<std::size_t>(degree)];
}

bool hasInterval(const ChordType& type, int semitones) {
    return std::find(type.intervals.begin(), type.intervals.end(), semitones) != type.intervals.end();
}

/// A major third and no major seventh: maj, aug, 6, 7, add9, 9, 6/9, 7#11 (rule 3a).
bool isDominantQuality(const ChordType& type) {
    return hasInterval(type, 4) && !hasInterval(type, 11);
}

/// A minor third over a diminished fifth: dim, dim7, m7b5 (rule 3b).
bool isLeadingToneQuality(const ChordType& type) {
    return hasInterval(type, 3) && hasInterval(type, 6);
}

/// A major third and a minor seventh: 7, 9, 7#11 and any dominant seventh added later (rule 3c).
bool isDominantSeventhQuality(const ChordType& type) {
    return hasInterval(type, 4) && hasInterval(type, 10);
}

/// True if `root` is a scale degree of `key` whose diatonic triad is not diminished: a degree an
/// applied dominant or leading-tone chord can tonicise.
bool isAppliedTarget(PitchClass root, const Key& key) {
    const int degree = degreeOf(root, key.tonicPc(), key.mode);
    if (degree < 0) {
        return false;
    }
    return std::string_view(diatonicTriad(key, degree).type->id) != "dim";
}

/// The subtonic: a minor seventh above the tonic (bVII).
bool isSubtonic(PitchClass pc, const Key& key) {
    return pitchClassOf(pc - key.tonicPc()) == 10;
}

/// True if some parallel mode of the key's tonic contains every note of the chord (rule 3e).
bool isBorrowed(const Chord& chord, const Key& key) {
    for (int m = 0; m < kNumModes; ++m) {
        if (fullyInKey(chord, Key{key.tonic, Mode(m)})) {
            return true;
        }
    }
    return false;
}

/// Checks the `Chord` preconditions documented in chord.h.
void assertValidChord([[maybe_unused]] const Chord& chord) {
    assert(chord.type != nullptr);
    assert(chord.root >= 0 && chord.root < 12);
}

}  // namespace

const char* functionShortName(Function function) {
    switch (function) {
        case Function::Tonic:
            return "T";
        case Function::Predominant:
            return "PD";
        case Function::Dominant:
            return "D";
        case Function::Other:
            return "O";
    }
    assert(false && "chordlab: function outside the enum");
    return "O";
}

Function functionOf(const Chord& chord, const Key& projectKey) {
    assertValidChord(chord);
    const ModeFamily family = familyOf(projectKey.mode);
    const NumeralDegree position = numeralDegree(chord, projectKey);
    const bool diatonic = fullyInKey(chord, projectKey);

    // Rule 1: Locrian labels only its own tonic chord.
    if (family == ModeFamily::Locrian) {
        return diatonic && position == NumeralDegree{0, 0} ? Function::Tonic : Function::Other;
    }
    // Rule 2: a diatonic chord is its degree.
    if (diatonic) {
        return byDegree(family, position.degree);
    }
    // Rule 3a/3b: applied dominants and leading-tone chords name a target a fifth below / a semitone above.
    const PitchClass fifthBelow = pitchClassOf(chord.root - 7);
    if (isDominantQuality(*chord.type) && isAppliedTarget(fifthBelow, projectKey) &&
        (hasInterval(*chord.type, 10) || !isSubtonic(fifthBelow, projectKey))) {
        return Function::Dominant;
    }
    if (isLeadingToneQuality(*chord.type) && isAppliedTarget(pitchClassOf(chord.root + 1), projectKey)) {
        return Function::Dominant;
    }
    // Rule 3c: a dominant seventh on bII is the tritone substitute of V7.
    if (position == NumeralDegree{1, -1} && isDominantSeventhQuality(*chord.type)) {
        return Function::Dominant;
    }
    // Rule 3d/3f: a chromatic root is one of the four borrowed major chords or nothing.
    if (position.alter != 0) {
        if (hasInterval(*chord.type, 3)) {
            return Function::Other;
        }
        switch (position.degree) {
            case 1:  // bII
            case 5:  // bVI
                return Function::Predominant;
            case 2:  // bIII
            case 6:  // bVII
                return Function::Tonic;
            default:  // #IV
                return Function::Other;
        }
    }
    // Rule 3e/3f: a borrowed chord keeps its degree; anything else leads nowhere.
    return isBorrowed(chord, projectKey) ? byDegree(family, position.degree) : Function::Other;
}

}  // namespace chordlab

#include "chordlab/symbol.h"

#include "chordlab/scale.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

namespace chordlab {

namespace {

constexpr std::array<std::string_view, 7> kUpperNumerals{"I", "II", "III", "IV", "V", "VI", "VII"};
constexpr std::array<std::string_view, 7> kLowerNumerals{"i", "ii", "iii", "iv", "v", "vi", "vii"};

/// Suffixes of the three qualities whose spelling differs between styles. Every other type uses its
/// dictionary `symbol` in both styles.
struct StyledSuffix {
    std::string_view id;
    std::string_view standard;
    std::string_view klimper;
};

constexpr std::array<StyledSuffix, 3> kSymbolOverrides{{
    {"dim", "°", "mb5"},
    {"m7b5", "ø7", "m7b5"},
    {"dim7", "°7", "dim7"},
}};

/// Numeral suffixes: the minor quality moves into the numeral's case, so `m`, `m6`, `m7`, `mΔ7`,
/// `madd9` and `m9` drop their `m`; the diminished qualities follow the style.
constexpr std::array<StyledSuffix, 23> kNumeralSuffixes{{
    {"5", "5", "5"},
    {"maj", "", ""},
    {"min", "", ""},
    {"dim", "°", "b5"},
    {"aug", "+", "+"},
    {"sus2", "sus2", "sus2"},
    {"sus4", "sus4", "sus4"},
    {"6", "6", "6"},
    {"m6", "6", "6"},
    {"maj7", "Δ7", "Δ7"},
    {"7", "7", "7"},
    {"m7", "7", "7"},
    {"m7b5", "ø7", "7b5"},
    {"dim7", "°7", "dim7"},
    {"mMaj7", "Δ7", "Δ7"},
    {"augMaj7", "+Δ7", "+Δ7"},
    {"add9", "add9", "add9"},
    {"madd9", "add9", "add9"},
    {"9", "9", "9"},
    {"maj9", "Δ9", "Δ9"},
    {"m9", "9", "9"},
    {"69", "6/9", "6/9"},
    {"7sus4", "7sus4", "7sus4"},
}};

std::string_view pick(const StyledSuffix& suffix, SymbolStyle style) {
    return style == SymbolStyle::Klimper ? suffix.klimper : suffix.standard;
}

std::string_view symbolSuffix(const ChordType& type, SymbolStyle style) {
    const std::string_view id = type.id;
    for (const StyledSuffix& s : kSymbolOverrides) {
        if (s.id == id) {
            return pick(s, style);
        }
    }
    return type.symbol;
}

std::string_view numeralSuffix(const ChordType& type, SymbolStyle style) {
    const std::string_view id = type.id;
    for (const StyledSuffix& s : kNumeralSuffixes) {
        if (s.id == id) {
            return pick(s, style);
        }
    }
    assert(false && "chordlab: chord type has no numeral suffix");
    return type.symbol;
}

bool hasInterval(const ChordType& type, int semitones) {
    return std::find(type.intervals.begin(), type.intervals.end(), semitones) != type.intervals.end();
}

/// Lowercase numerals carry a minor third; anything else (major third, no third) is uppercase.
bool isMinorQuality(const ChordType& type) {
    return hasInterval(type, 3) && !hasInterval(type, 4);
}

/// Semitones from `from` up to `to`, as the representative of the difference mod 12 in -6..5.
int signedSemitones(PitchClass from, PitchClass to) {
    return ((to - from) % 12 + 12 + 6) % 12 - 6;
}

/// Checks the `Chord` preconditions documented in chord.h.
void assertValidChord([[maybe_unused]] const Chord& chord) {
    assert(chord.type != nullptr);
    assert(chord.root >= 0 && chord.root < 12);
}

}  // namespace

std::string chordSymbol(const Chord& chord, const Key& spellingKey, SymbolStyle style) {
    assertValidChord(chord);
    std::string out = spell(chord.root, spellingKey).text();
    out += symbolSuffix(*chord.type, style);
    return out;
}

std::string romanNumeral(const Chord& chord, const Key& projectKey, SymbolStyle style) {
    assertValidChord(chord);
    // Numerals are read against the parallel major of the project tonic, whatever the project mode, with
    // the same degree selection as `spell` (pitch.h). The alteration below is key-relative, unlike the
    // symbol's letter-relative accidental, so the two need not be equal (see symbol.h).
    const PitchClass tonic = projectKey.tonicPc();
    const int degree = majorDegreeOfInterval(chord.root - tonic);
    const PitchClass degreePc = scalePitchClasses(tonic, Mode::Ionian)[static_cast<std::size_t>(degree)];
    // Zero for a major-scale member; otherwise +1 (#4) or -1 (b2, b3, b6, b7).
    const int alteration = signedSemitones(degreePc, chord.root);
    assert(alteration >= -1 && alteration <= 1);

    std::string out;
    if (alteration > 0) {
        out += '#';
    } else if (alteration < 0) {
        out += 'b';
    }
    const auto& numerals = isMinorQuality(*chord.type) ? kLowerNumerals : kUpperNumerals;
    out += numerals[static_cast<std::size_t>(degree)];
    out += numeralSuffix(*chord.type, style);
    return out;
}

}  // namespace chordlab

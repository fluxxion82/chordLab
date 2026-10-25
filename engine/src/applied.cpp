#include "chordlab/applied.h"

#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <cstdlib>
#include <string_view>

namespace chordlab {

namespace {

/// A key whose seven degrees all spell with at most one accidental: the eighth sharp is Fx, the
/// eighth flat Bbb.
constexpr int kMaxSingleAccidentalSignature = 7;

bool withinSingleAccidentals(const Key& key) {
    return std::abs(signatureOf(key)) <= kMaxSingleAccidentalSignature;
}

/// See `AppliedChord::spellingKey`: the major key on degree `degree` of `key`, then the minor one, then
/// the canonical major key of the target's pitch class, whichever first stays within seven accidentals.
Key spellingKeyOf(const Key& key, int degree, const Chord& target) {
    const Spelling spelled = spellDegree(key, degree);
    if (spelled.alter >= -1 && spelled.alter <= 1) {
        const Tonic tonic{spelled.letter, spelled.alter};
        for (const Mode mode : {Mode::Ionian, Mode::Aeolian}) {
            const Key candidate{tonic, mode};
            if (withinSingleAccidentals(candidate)) {
                return candidate;
            }
        }
    }
    return Key{canonicalTonic(target.root, Mode::Ionian), Mode::Ionian};
}

}  // namespace

std::vector<AppliedChord> appliedChords(const Key& key) {
    std::vector<AppliedChord> out;
    out.reserve(12);
    for (int degree = 1; degree < 7; ++degree) {
        const Chord target = diatonicTriad(key, degree);
        const std::string_view quality = target.type->id;
        if (quality == "dim" || quality == "aug") {  // only major and minor triads are tonicised
            continue;
        }
        const std::string numeral = romanNumeral(target, key, SymbolStyle::Standard);
        const Key spellingKey = spellingKeyOf(key, degree, target);
        out.push_back(AppliedChord{Chord{pitchClassOf(target.root + 7), &chordType("7")}, degree,
                                   "V7/" + numeral, spellingKey});
        out.push_back(AppliedChord{Chord{pitchClassOf(target.root - 1), &chordType("dim7")}, degree,
                                   "vii°7/" + numeral, spellingKey});
    }
    return out;
}

}  // namespace chordlab
